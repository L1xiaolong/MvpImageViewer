#include "crash_shared_win.h"
#include <dbghelp.h>
#include <shellapi.h>
#include <cstdlib>
#include <cstdint>
using ispview::diagnostics::CrashShared;
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0; auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv || argc != 6) return 1;
    const auto handle = [&](int i) { return reinterpret_cast<HANDLE>(static_cast<uintptr_t>(_wcstoui64(argv[i], nullptr, 10))); };
    HANDLE mapping = handle(1), request = handle(2), done = handle(3), ready = handle(4);
    auto* shared = static_cast<CrashShared*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(CrashShared)));
    if (!shared) return 2;
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE, FALSE, shared->processId);
    shared->result = process ? ERROR_SUCCESS : GetLastError(); SetEvent(ready);
    if (!process) return 3;
    HANDLE waits[]{request, process};
    if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) == WAIT_OBJECT_0 && !shared->shutdown) {
        HANDLE file = CreateFileW(argv[5], GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            EXCEPTION_POINTERS pointers{&shared->exception, &shared->context};
            MINIDUMP_EXCEPTION_INFORMATION exception{shared->threadId, &pointers, FALSE};
            const bool ok = MiniDumpWriteDump(process, shared->processId, file,
                static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules),
                &exception, nullptr, nullptr) != FALSE;
            shared->result = ok ? ERROR_SUCCESS : GetLastError();
            FlushFileBuffers(file); CloseHandle(file);
            if (!ok) DeleteFileW(argv[5]);
        } else shared->result = GetLastError();
        SetEvent(done);
    }
    CloseHandle(process); UnmapViewOfFile(shared); LocalFree(argv);
    return 0;
}
