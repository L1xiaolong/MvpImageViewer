#!/usr/bin/env python3
"""Archive exact build symbols before staging is stripped/signed; never alters build outputs."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys


def pe_identity(binary):
    data = Path(binary).read_bytes()
    if data[:2] != b'MZ':
        return None
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    optional = pe + 24
    magic = struct.unpack_from('<H', data, optional)[0]
    image_base = struct.unpack_from('<Q' if magic == 0x20b else '<I', data, optional + (24 if magic == 0x20b else 28))[0]
    return dict(timestamp=struct.unpack_from('<I', data, pe + 8)[0],
                imageSize=struct.unpack_from('<I', data, optional + 56)[0], imageBase=image_base,
                machine=struct.unpack_from('<H', data, pe + 4)[0])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--staged-binary', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    digest = hashlib.sha256(binary.read_bytes()).hexdigest()
    destination = args.output.resolve() / (binary.stem + '-' + digest[:16])
    destination.mkdir(parents=True, exist_ok=True)
    shutil.copy2(binary, destination / binary.name)
    manifest = {'binary': binary.name, 'sha256': digest, 'pe': pe_identity(binary)}
    import re
    project = Path(__file__).resolve().parents[1] / 'CMakeLists.txt'
    match = re.search(r'project\(ISPImageViewer VERSION ([0-9.]+)', project.read_text(encoding='utf8'))
    manifest['version'] = match[1] if match else 'unknown'
    try:
        manifest['commit'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
        manifest['dirty'] = bool(subprocess.check_output(['git', 'status', '--porcelain'], text=True).strip())
    except (subprocess.CalledProcessError, FileNotFoundError):
        manifest['commit'] = 'source-archive'
    pdb = binary.with_suffix('.pdb')
    dsym = Path(str(binary) + '.dSYM')
    if pdb.exists():
        shutil.copy2(pdb, destination / pdb.name)
        manifest['symbols'] = pdb.name
    elif dsym.exists():
        shutil.copytree(dsym, destination / dsym.name, dirs_exist_ok=True)
        manifest['symbols'] = dsym.name
        manifest['uuid'] = subprocess.check_output(['dwarfdump', '--uuid', str(binary)], text=True).strip()
        if args.staged_binary:
            subprocess.run(['strip', '-S', str(args.staged_binary)], check=True)
    else:
        objcopy = shutil.which('objcopy') or shutil.which('llvm-objcopy')
        if not objcopy:
            raise RuntimeError('No PDB/dSYM and objcopy is unavailable; cannot archive release symbols')
        debug = destination / (binary.name + '.debug')
        subprocess.run([objcopy, '--only-keep-debug', str(binary), str(debug)], check=True)
        # Validate actual debug sections rather than silently retaining an empty symbol file.
        objdump = shutil.which('objdump') or shutil.which('llvm-objdump')
        sections = subprocess.check_output([objdump, '-h', str(debug)], text=True)
        if '.debug_info' not in sections or '.debug_line' not in sections:
            raise RuntimeError('Release binary has no DWARF line information')
        manifest['symbols'] = debug.name
        if args.staged_binary:
            subprocess.run([objcopy, '--strip-debug', str(args.staged_binary)], check=True)
    if args.staged_binary:
        manifest['stagedSha256BeforeSigning'] = hashlib.sha256(args.staged_binary.read_bytes()).hexdigest()
        manifest['stagedPe'] = pe_identity(args.staged_binary)
    (destination / 'symbols.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf8')
    print(destination)


if __name__ == '__main__':
    main()
