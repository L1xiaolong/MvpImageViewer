"""Measure the benchmark child, independently of Qt/cache allocation estimates."""
import ctypes
import os
import threading


class ProcessMemoryProbe:
    def __init__(self, pid):
        self.result = {"backend": "unavailable"}
        self._stop = threading.Event()
        self._thread = None
        self._handle = None
        if os.name != "nt":
            return
        from ctypes import wintypes

        class Counters(ctypes.Structure):
            _fields_ = [("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD)] + [
                (name, ctypes.c_size_t) for name in (
                    "PeakWorkingSetSize", "WorkingSetSize", "QuotaPeakPagedPoolUsage",
                    "QuotaPagedPoolUsage", "QuotaPeakNonPagedPoolUsage", "QuotaNonPagedPoolUsage",
                    "PagefileUsage", "PeakPagefileUsage", "PrivateUsage")]

        self._kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        self._kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        self._kernel.OpenProcess.restype = wintypes.HANDLE
        self._kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        self._read = self._kernel.K32GetProcessMemoryInfo
        self._read.argtypes = [wintypes.HANDLE, ctypes.POINTER(Counters), wintypes.DWORD]
        self._read.restype = wintypes.BOOL
        self._handle = self._kernel.OpenProcess(0x0410, False, pid)
        if not self._handle:
            self.result["error"] = ctypes.get_last_error()
            return
        self.result = {"backend": "Windows GetProcessMemoryInfo", "samples": 0,
                       "peakWorkingSetBytes": 0, "peakPrivateCommitBytes": 0}

        def sample():
            counters = Counters()
            counters.cb = ctypes.sizeof(counters)
            while True:
                if self._read(self._handle, ctypes.byref(counters), counters.cb):
                    self.result["samples"] += 1
                    self.result["peakWorkingSetBytes"] = max(self.result["peakWorkingSetBytes"], counters.PeakWorkingSetSize)
                    self.result["peakPrivateCommitBytes"] = max(self.result["peakPrivateCommitBytes"], counters.PeakPagefileUsage)
                if self._stop.wait(.05):
                    break

        self._thread = threading.Thread(target=sample, daemon=True)
        self._thread.start()

    def finish(self):
        self._stop.set()
        if self._thread:
            self._thread.join()
        if self._handle:
            self._kernel.CloseHandle(self._handle)
            self._handle = None
        return dict(self.result)
