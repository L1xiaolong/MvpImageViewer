#!/usr/bin/env python3
"""Resolve the faulting Windows module/address against a matching MinGW symbol archive.
For complete stacks use WinDbg (PDB) or minidump_stackwalk with matching Breakpad symbols.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
from archive_diagnostic_symbols import pe_identity


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('dump', type=Path)
    p.add_argument('--binary', type=Path, required=True, help='Matching unstripped EXE from the symbol archive')
    args = p.parse_args()
    data = args.dump.read_bytes()
    if data[:4] != b'MDMP':
        p.error('Not a minidump')
    count, directory = struct.unpack_from('<II', data, 8)
    streams = {}
    for i in range(count):
        kind, size, rva = struct.unpack_from('<III', data, directory + 12 * i)
        if rva + size > len(data):
            p.error('Truncated minidump stream')
        streams[kind] = (rva, size)
    if 6 not in streams or 4 not in streams:
        p.error('Dump lacks exception/module streams')
    exception = streams[6][0]
    thread, = struct.unpack_from('<I', data, exception)
    code, = struct.unpack_from('<I', data, exception + 8)
    address, = struct.unpack_from('<Q', data, exception + 24)
    modules = streams[4][0]
    module_count, = struct.unpack_from('<I', data, modules)
    for i in range(module_count):
        pos = modules + 4 + 108 * i
        base, size, checksum, timestamp, name_rva = struct.unpack_from('<QIIII', data, pos)
        if not base <= address < base + size:
            continue
        length, = struct.unpack_from('<I', data, name_rva)
        name = data[name_rva + 4:name_rva + 4 + length].decode('utf-16-le').replace('\\', '/').split('/')[-1]
        identity = pe_identity(args.binary)
        print(json.dumps(dict(thread=thread, exception=hex(code), module=name,
                              address=hex(address), moduleOffset=hex(address-base)), indent=2))
        candidates = [identity] if identity else []
        manifest_path = args.binary.parent / 'symbols.json'
        if manifest_path.exists():
            manifest = json.loads(manifest_path.read_text(encoding='utf8'))
            if manifest['sha256'] != hashlib.sha256(args.binary.read_bytes()).hexdigest():
                p.error('Archived binary hash does not match symbols.json')
            if manifest.get('stagedPe'):
                candidates.append(manifest['stagedPe'])
        if (not identity or name.lower() != args.binary.name.lower() or not any(
                item['timestamp'] == timestamp and item['imageSize'] == size for item in candidates)):
            p.error('Symbols do not match the faulting module; select its exact archived binary')
        tool = shutil.which('addr2line') or shutil.which('llvm-addr2line')
        if not tool:
            p.error('Install addr2line for DWARF source lookup (MSVC users should use WinDbg + PDB)')
        result = subprocess.check_output([tool, '-f', '-C', '-e', str(args.binary),
                                         hex(address - base + identity['imageBase'])], text=True)
        print(result)
        if '??' in result:
            p.error('Address could not be resolved with these symbols')
        return
    p.error('Exception address is outside the captured modules')


if __name__ == '__main__':
    main()
