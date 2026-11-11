#!/usr/bin/env python3
"""Remove unreachable top-level Qt DLLs after plugin pruning (MSYS2 packages)."""
import argparse
from pathlib import Path
import re
import subprocess


def unreachable_qt_dlls(root, imports):
    root = root.resolve(strict=True)
    binaries = sorted(p for p in root.rglob('*') if p.is_file() and p.suffix.lower() in ('.exe', '.dll'))
    # Plugins/QML modules are loaded dynamically: every nested DLL is a root.
    # Keep all non-Qt runtime DLLs too (e.g. dynamically loaded TLS/GL backends).
    candidates = {p.name.lower(): p for p in binaries
                  if p.parent == root and p.name.lower().startswith('qt6') and p.suffix.lower() == '.dll'}
    queue = [p for p in binaries if p not in candidates.values()]
    visited = set()
    while queue:
        binary = queue.pop()
        if binary in visited:
            continue
        visited.add(binary)
        for dependency in imports(binary):
            candidate = candidates.get(dependency.lower())
            if candidate is not None:
                queue.append(candidate)
    return sorted(set(candidates.values()) - visited)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package', type=Path)
    parser.add_argument('--objdump', required=True)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    root = args.package.resolve(strict=True)
    if not (root / 'MVPImageViewer.exe').is_file() or not (root / 'qml').is_dir():
        raise RuntimeError('Expected a staged application with deployed QML modules')

    def imports(binary):
        output = subprocess.check_output([args.objdump, '-p', str(binary)], text=True, errors='replace')
        # Do not prune based on incomplete dependency information.
        if not re.search(r'file format pei?-', output):
            raise RuntimeError(f'Not a recognized PE binary: {binary}')
        # GNU objdump does not decode every toolchain\'s delay import tables.
        # Preserve the package rather than risk removing a delayed dependency.
        delay = re.search(r'^Entry d\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)', output, re.MULTILINE)
        if delay and int(delay[1], 16):
            raise RuntimeError(f'Delay imports require manual dependency review: {binary}')
        return re.findall(r'DLL Name:\s*([^\r\n]+)', output)

    # Finish the entire audit before mutating the staged package.
    unused = unreachable_qt_dlls(root, imports)
    saved = sum(p.stat().st_size for p in unused)
    for path in unused:
        print(f'{"Removing" if args.apply else "Unused"}: {path.name}')
        if args.apply:
            path.unlink()
    print(f'Qt runtime pruning: {len(unused)} DLLs, {saved / 1048576:.2f} MiB')


if __name__ == '__main__':
    main()
