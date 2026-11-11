#!/usr/bin/env python3
"""Measure Windows viewer memory with fixed fixtures and a six-second screenshot/exit run.

Usage: python scripts/benchmark_memory.py EXE FIXTURES OUTPUT_JSON [--repeats 3]
Use --loader with memory_benchmark.exe to measure preview/full/close/reopen phases.
The environment must resolve the selected build's matching Qt runtime.
"""
import argparse
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import statistics
import subprocess
import time


class Counters(ctypes.Structure):
    _fields_ = [('cb', wintypes.DWORD), ('PageFaultCount', wintypes.DWORD)] + [
        (name, ctypes.c_size_t) for name in (
            'PeakWorkingSetSize', 'WorkingSetSize', 'QuotaPeakPagedPoolUsage',
            'QuotaPagedPoolUsage', 'QuotaPeakNonPagedPoolUsage', 'QuotaNonPagedPoolUsage',
            'PagefileUsage', 'PeakPagefileUsage', 'PrivateUsage')]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe', type=Path)
    parser.add_argument('fixtures', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--repeats', type=int, default=3)
    parser.add_argument('--loader', action='store_true')
    args = parser.parse_args()
    if os.name != 'nt':
        raise RuntimeError('Windows process counters are required')
    if args.loader:
        rows = []
        for repeat in range(args.repeats):
            for scenario in ('jpeg', 'png16'):
                result = subprocess.run([str(args.exe.resolve()), scenario, str(args.fixtures.resolve())],
                                        capture_output=True, text=True, check=True, timeout=90)
                for line in result.stdout.splitlines():
                    row = json.loads(line)
                    row.update(repeat=repeat, scenario=scenario)
                    rows.append(row)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(rows, indent=2))
        return
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    psapi = ctypes.WinDLL('psapi', use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    psapi.GetProcessMemoryInfo.argtypes = [wintypes.HANDLE, ctypes.POINTER(Counters), wintypes.DWORD]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    empty = args.output.parent / 'empty'
    empty.mkdir(exist_ok=True)
    rows = []
    for repeat in range(args.repeats):
        for scenario, paths in [('idle', []), ('jpeg', list(args.fixtures.glob('jpeg-*.jpg'))),
                                ('png16', list(args.fixtures.glob('png16-*.png')))]:
            shot = args.output.parent / f'{args.output.stem}-{scenario}-{repeat}.png'
            command = [str(args.exe.resolve())]
            command += ['--qml-compare', *map(lambda p: str(p.resolve()), sorted(paths))] if paths else [str(empty.resolve())]
            command += ['--screenshot', str(shot.resolve()), '--screenshot-delay', '6000']
            startup = subprocess.STARTUPINFO()
            startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = 0
            started = time.monotonic()
            process = subprocess.Popen(command, startupinfo=startup, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            handle = kernel.OpenProcess(0x0400 | 0x0010, False, process.pid)
            if not handle:
                process.kill()
                raise ctypes.WinError(ctypes.get_last_error())
            samples = []
            try:
                while process.poll() is None:
                    elapsed = time.monotonic() - started
                    if elapsed > 20:
                        process.kill()
                        raise RuntimeError(f'{scenario} did not exit')
                    c = Counters(); c.cb = ctypes.sizeof(c)
                    if elapsed < 5.5 and psapi.GetProcessMemoryInfo(handle, ctypes.byref(c), c.cb):
                        samples.append({'seconds': elapsed, 'privateBytes': c.PrivateUsage, 'workingSetBytes': c.WorkingSetSize})
                    time.sleep(.05)
            finally:
                kernel.CloseHandle(handle)
                if process.poll() is None:
                    process.kill()
                process.wait()
            if process.returncode != 0 or not shot.exists():
                raise RuntimeError(f'{scenario} failed: {process.returncode}')
            stable = [s for s in samples if s['seconds'] >= 3]
            if len(stable) < 10:
                raise RuntimeError('Insufficient stable memory samples')
            row = {'scenario': scenario, 'repeat': repeat,
                   'privateBytes': statistics.median(s['privateBytes'] for s in stable),
                   'workingSetBytes': statistics.median(s['workingSetBytes'] for s in stable),
                   'sampledPeakPrivateBytes': max(s['privateBytes'] for s in samples), 'samples': samples}
            rows.append(row)
            args.output.write_text(json.dumps(rows, indent=2))
            print(scenario, repeat, 'private MiB:', round(row['privateBytes']/1048576, 2), flush=True)


if __name__ == '__main__':
    main()
