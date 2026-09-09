"""Behavioral checks for the distribution license gate; no Qt installation needed."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "collector", Path(__file__).resolve().parents[1] / "scripts/collect_runtime_licenses.py")
collector = importlib.util.module_from_spec(spec)
spec.loader.exec_module(collector)


class RuntimeLicenseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.package = self.root / "package with spaces"
        self.package.mkdir()
        self.output = self.root / "notices"
        self.msys = self.root / "msys"
        db = self.msys / "var/lib/pacman/local/example-1.2-3"
        db.mkdir(parents=True)
        (db / "desc").write_text(
            "%NAME%\nexample\n\n%VERSION%\n1.2-3\n\n%BASE%\nmingw-w64-example\n\n"
            "%URL%\nhttps://example.org\n\n%LICENSE%\nMIT\n", encoding="utf-8")
        (db / "files").write_text(
            "%FILES%\nucrt64/bin/example.dll\nucrt64/share/example/LICENSE\n", encoding="utf-8")
        self.source = self.msys / "ucrt64/bin/example.dll"
        self.source.parent.mkdir(parents=True)
        self.source.write_bytes(b"example runtime bytes")
        (self.package / "example.dll").write_bytes(self.source.read_bytes())
        self.notice = self.msys / "ucrt64/share/example/LICENSE"
        self.notice.parent.mkdir(parents=True)
        self.notice.write_text("Example license notice", encoding="utf-8")

    def collect(self):
        return collector.collect(self.package, self.output, self.msys)

    def test_matches_bytes_and_preserves_notice_outside_standard_license_directory(self):
        report = self.collect()
        self.assertEqual(report["status"], "collected")
        evidence = report["components"][0]["evidence"][0]
        self.assertEqual((self.output / evidence["path"]).read_bytes(), self.notice.read_bytes())
        self.assertEqual(report["files"][0]["sha256"], collector.digest(self.source))

    def test_same_filename_with_different_bytes_is_blocked(self):
        (self.package / "example.dll").write_bytes(b"different build")
        report = self.collect()
        self.assertEqual(report["status"], "blocked")
        self.assertEqual(report["files"][0]["status"], "unresolved")

    def test_missing_notice_blocks_identified_library(self):
        self.notice.unlink()
        report = self.collect()
        self.assertEqual(report["files"][0]["status"], "identified")
        self.assertTrue(any("No license text" in problem for problem in report["problems"]))

    def test_nested_qml_and_vendor_dll_are_not_silently_skipped(self):
        folder = self.package / "qml/Example"
        folder.mkdir(parents=True)
        (folder / "Main.qml").write_text("import QtQuick", encoding="utf-8")
        (self.package / "vendor.dll").write_bytes(b"vendor")
        report = self.collect()
        unresolved = {f["path"] for f in report["files"] if f["status"] == "unresolved"}
        self.assertEqual(unresolved, {"qml/Example/Main.qml", "vendor.dll"})

    def catalog(self):
        (self.root / "vendor-notice.txt").write_text("Vendor terms", encoding="utf-8")
        data = {"schema_version": 1, "components": [{
            "id": "example-vendor", "version": "1", "homepage": "https://example.org",
            "declared_license": "LicenseRef-Vendor", "source_reference": "https://example.org/sdk/1",
            "notices": ["vendor-notice.txt"],
            "files": [{"path": "example.dll", "sha256": collector.digest(self.source)}],
        }]}
        path = self.root / "catalog.json"
        path.write_text(json.dumps(data), encoding="utf-8")
        return path, data

    def test_vendor_catalog_is_hash_bound(self):
        path, _ = self.catalog()
        report = collector.collect(self.package, self.output, catalog_path=path)
        self.assertEqual(report["status"], "collected")
        (self.package / "example.dll").write_bytes(b"updated vendor build")
        report = collector.collect(self.package, self.output, catalog_path=path)
        self.assertEqual(report["status"], "blocked")

    def test_stale_catalog_entry_and_path_traversal_are_rejected(self):
        path, data = self.catalog()
        data["components"][0]["files"][0]["path"] = "absent.dll"
        path.write_text(json.dumps(data), encoding="utf-8")
        report = collector.collect(self.package, self.output, catalog_path=path)
        self.assertTrue(any("not in this package" in p for p in report["problems"]))
        data["components"][0]["notices"] = ["../outside.txt"]
        path.write_text(json.dumps(data), encoding="utf-8")
        with self.assertRaises(ValueError):
            collector.load_catalog(path)

    def test_macos_framework_binary_detected_without_suffix(self):
        folder = self.package / "Contents/Frameworks/Example.framework/Versions/A"
        folder.mkdir(parents=True)
        (folder / "Example").write_bytes(bytes.fromhex("cffaedfe") + b"binary")
        report = self.collect()
        self.assertTrue(any(f["path"].endswith("/Example") for f in report["files"]))
        self.assertEqual(report["status"], "blocked")

    def test_application_excluded_but_other_executables_audited(self):
        (self.package / "MVPImageViewer.exe").write_bytes(b"application")
        (self.package / "helper.exe").write_bytes(b"third party helper")
        report = self.collect()
        self.assertEqual({f["path"] for f in report["files"]}, {"example.dll", "helper.exe"})

    def test_empty_directory_is_not_successful_audit(self):
        (self.package / "example.dll").unlink()
        self.assertEqual(self.collect()["status"], "blocked")


if __name__ == "__main__":
    unittest.main()
