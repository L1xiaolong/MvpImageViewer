#!/usr/bin/env python3
"""Inventory deployed runtime files and copy their package/vendor license evidence.

Uses only the Python standard library. No network requests, inferred license grants,
or filename-only ownership matches. A nonzero exit leaves a report for remediation.
"""

import argparse
from collections import defaultdict
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import sys


RUNTIME_SUFFIXES = {".dll", ".exe", ".dylib", ".so", ".qml", ".js", ".mjs",
                    ".qmltypes", ".rcc", ".qm", ".ttf", ".otf", ".woff", ".woff2"}
MACHO_MAGIC = {bytes.fromhex(x) for x in
               ("feedface", "cefaedfe", "feedfacf", "cffaedfe", "cafebabe", "bebafeca",
                "cafebabf", "bfbafeca")}
APPLICATION_FILES = {"MVPImageViewer.exe", "Contents/MacOS/MVPImageViewer"}


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def relative_path(value):
    # Catalog and package database paths are data, never traversal instructions.
    if not isinstance(value, str) or not value or "\\" in value:
        raise ValueError(f"Invalid relative path: {value!r}")
    path = Path(value)
    if path.is_absolute() or ":" in value or any(p in {"..", "."} for p in value.split("/")):
        raise ValueError(f"Unsafe relative path: {value!r}")
    return path


def inside(root, value):
    path = root / relative_path(value)
    if not path.resolve().is_relative_to(root.resolve()):
        raise ValueError(f"Path escapes its root: {value}")
    return path


def runtime_files(root):
    for path in sorted(root.rglob("*")):
        if not path.is_file():
            continue
        rel = path.relative_to(root).as_posix()
        if rel in APPLICATION_FILES or "licenses" in path.relative_to(root).parts:
            continue
        if not path.resolve().is_relative_to(root):
            raise ValueError(f"Runtime symlink escapes package: {rel}")
        if path.suffix.lower() in RUNTIME_SUFFIXES or path.name == "qmldir":
            yield path
        else:
            with path.open("rb") as stream:
                if stream.read(4) in MACHO_MAGIC:
                    yield path


def database_fields(path):
    result = {}
    field = None
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("%") and line.endswith("%"):
            field = line.strip("%")
            result[field] = []
        elif line and field:
            result[field].append(line)
    return result


class MsysDatabase:
    def __init__(self, root):
        self.root = root.resolve()
        self.candidates = defaultdict(list)
        self.packages = {}
        database = self.root / "var/lib/pacman/local"
        if not database.is_dir():
            raise ValueError(f"MSYS2 package database not found: {database}")
        for folder in sorted(database.iterdir()):
            if not (folder / "desc").is_file() or not (folder / "files").is_file():
                continue
            fields = database_fields(folder / "desc")
            files = database_fields(folder / "files").get("FILES", [])
            name = fields["NAME"][0]
            self.packages[name] = (fields, files)
            for filename in files:
                if filename.endswith("/"):
                    continue
                self.candidates[Path(filename).name.lower()].append((name, filename))

    def match(self, path, sha256):
        matches = []
        for name, filename in self.candidates[path.name.lower()]:
            source = inside(self.root, filename)
            if source.is_file() and source.stat().st_size == path.stat().st_size and digest(source) == sha256:
                matches.append((name, filename))
        owners = {name for name, _ in matches}
        if len(owners) != 1:
            return None
        name, source_path = matches[0]
        fields, files = self.packages[name]
        base = fields.get("BASE", [name])[0]
        info = {
            "id": name, "version": fields["VERSION"][0],
            "homepage": fields.get("URL", [""])[0],
            "declared_license": " AND ".join(fields.get("LICENSE", [])),
            "license_scope": "Installed package metadata; not a per-file license conclusion",
            "provider": "msys2-pacman", "source_reference":
                f"https://github.com/msys2/MINGW-packages/tree/master/{base}",
            "source_reference_scope": "Recipe discovery link; not a pinned corresponding-source offer",
        }
        evidence = []
        for filename in files:
            is_notice = ("/share/licenses/" in filename or
                         re.fullmatch(r"(?:LICENSE|LICENCE|COPYING|COPYRIGHT|NOTICE|EULA)(?:[._-].*)?",
                                      Path(filename).name, re.IGNORECASE))
            if not filename.endswith("/") and (is_notice or "/sbom/" in filename):
                source = inside(self.root, filename)
                evidence.append((filename, source, "license" if is_notice else "sbom"))
        return info, evidence, source_path


def load_catalog(path):
    """Explicit, hash-bound evidence for vendor SDKs, Homebrew and custom builds."""
    if path is None:
        return {}
    data = json.loads(path.read_text(encoding="utf-8"))
    if data.get("schema_version") != 1:
        raise ValueError("Catalog schema_version must be 1")
    matches = {}
    ids = set()
    for component in data["components"]:
        for key in ("id", "version", "homepage", "declared_license", "source_reference"):
            if not isinstance(component.get(key), str) or not component[key].strip():
                raise ValueError(f"Catalog component requires {key}")
        if component["id"] in ids:
            raise ValueError(f"Duplicate catalog component: {component['id']}")
        ids.add(component["id"])
        info = {key: component[key] for key in
                ("id", "version", "homepage", "declared_license", "source_reference")}
        info.update(provider="reviewed-catalog", license_scope="Catalog declaration supplied by distributor")
        evidence = []
        for filename in component["notices"]:
            evidence.append((filename, inside(path.parent, filename), "license"))
        for entry in component["files"]:
            relative_path(entry["path"])
            if not re.fullmatch(r"[a-f0-9]{64}", entry["sha256"]):
                raise ValueError("Catalog files require lowercase SHA-256")
            if entry["path"] in matches:
                raise ValueError(f"Duplicate catalog file: {entry['path']}")
            matches[entry["path"]] = (entry["sha256"], info, evidence)
    return matches


def collect(package, output, msys_root=None, catalog_path=None):
    package = package.resolve()
    if not package.is_dir():
        raise ValueError(f"Package directory not found: {package}")
    # Snapshot before writing reports, so output can be under the package.
    deployed = list(runtime_files(package))
    database = MsysDatabase(msys_root) if msys_root else None
    catalog = load_catalog(catalog_path)
    components = {}
    files = []
    problems = []
    if not deployed:
        problems.append("No third-party runtime files found; verify the package path")
    for path in deployed:
        rel = path.relative_to(package).as_posix()
        sha256 = digest(path)
        record = {"path": rel, "sha256": sha256, "size": path.stat().st_size}
        match = None
        if rel in catalog:
            expected, info, evidence = catalog[rel]
            if expected == sha256:
                match = info, evidence, "catalog SHA-256"
            else:
                problems.append(f"Catalog hash mismatch: {rel}")
        elif database:
            match = database.match(path, sha256)
        if match is None:
            record["status"] = "unresolved"
            problems.append(f"No unambiguous, hash-matched provenance: {rel}")
        else:
            info, evidence, source_path = match
            component_id = info["id"]
            if not re.fullmatch(r"[A-Za-z0-9_.+-]+", component_id):
                raise ValueError(f"Unsafe component id: {component_id}")
            record.update(component=component_id, status="identified", provenance=source_path)
            if component_id in components:
                if components[component_id]["version"] != info["version"] or components[component_id]["provider"] != info["provider"]:
                    raise ValueError(f"Conflicting identity: {component_id}")
            else:
                item = dict(info, evidence=[])
                if not info["declared_license"]:
                    problems.append(f"Missing license declaration: {component_id}")
                license_count = 0
                for filename, source, kind in evidence:
                    if not source.is_file() or source.stat().st_size == 0:
                        problems.append(f"Missing or empty {kind}: {component_id}/{filename}")
                        continue
                    target_rel = f"licenses/runtime/{component_id}/{filename}"
                    target = inside(output, target_rel)
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(source, target)
                    item["evidence"].append({"path": target_rel, "kind": kind, "sha256": digest(target)})
                    license_count += kind == "license"
                if not license_count:
                    problems.append(f"No license text collected: {component_id}")
                components[component_id] = item
        files.append(record)
    for stale in sorted(set(catalog) - {item["path"] for item in files}):
        problems.append(f"Catalog file is not in this package: {stale}")
    report = {
        "schema_version": 1,
        "hash_scope": "File bytes at collection time; macOS packaging collects after relocation and before code signing",
        "status": "blocked" if problems else "collected",
        "scope": "Deployed binaries, QML/JS, translations, resource archives and fonts. "
                 "Static code inside libraries requires upstream SBOM/source review. "
                 "Collected evidence is not a complete license-compatibility or source-delivery approval.",
        "components": sorted(components.values(), key=lambda item: item["id"]),
        "files": files, "problems": problems,
    }
    output.mkdir(parents=True, exist_ok=True)
    (output / "RUNTIME_DEPENDENCIES.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    lines = ["# Runtime dependency inventory", "", f"Collection status: **{report['status']}**", "",
             report["scope"], "", "| Component | Version | Declared license |", "| --- | --- | --- |"]
    for item in report["components"]:
        cells = [item[key].replace("|", "\\|").replace("\n", " ") for key in ("id", "version", "declared_license")]
        lines.append("| " + " | ".join(cells) + " |")
    lines += ["", "See RUNTIME_DEPENDENCIES.json for per-file SHA-256, provenance, source references,",
              "and copied license/SBOM paths. Runtime license evidence is under licenses/runtime/.", ""]
    if problems:
        lines += ["## Blocking findings", ""] + [f"- {problem}" for problem in problems] + [""]
    (output / "RUNTIME_DEPENDENCIES.md").write_text("\n".join(lines), encoding="utf-8")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("--output", type=Path, help="Notice directory; defaults to package root")
    parser.add_argument("--msys-root", type=Path)
    parser.add_argument("--catalog", type=Path, default=os.environ.get("ISPVIEW_RUNTIME_CATALOG") or None,
                        help="Reviewed vendor/custom-runtime catalog, also read from ISPVIEW_RUNTIME_CATALOG")
    args = parser.parse_args()
    try:
        report = collect(args.package, args.output or args.package, args.msys_root, args.catalog)
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"Runtime license collection failed: {error}", file=sys.stderr)
        return 2
    print(f"Runtime license collection: {len(report['components'])} components, "
          f"{len(report['files'])} files, {len(report['problems'])} blocking findings")
    for problem in report["problems"]:
        print(problem, file=sys.stderr)
    return 1 if report["problems"] else 0


if __name__ == "__main__":
    sys.exit(main())
