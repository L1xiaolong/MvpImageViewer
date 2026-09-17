#include "diagnostics/crash_capture.h"
#include <QDir>
#include <QFileInfo>
#include <exception>
#include <map>
#include <csignal>
#ifndef ISPVIEW_HAS_CRASHPAD
#define ISPVIEW_HAS_CRASHPAD 0
#endif

#ifdef Q_OS_WIN
#include "diagnostics/crash_shared_win.h"
#elif defined(Q_OS_MACOS) && ISPVIEW_HAS_CRASHPAD
#include <client/crashpad_client.h>
#include <client/crash_report_database.h>
#include <client/settings.h>
#include <base/files/file_path.h>
#include <mach-o/dyld.h>
#endif
namespace ispview::diagnostics {
#ifdef Q_OS_WIN
namespace {
CrashShared* shared = nullptr;
HANDLE requestEvent = nullptr, doneEvent = nullptr;
volatile LONG crashing = 0;
LONG WINAPI capture(EXCEPTION_POINTERS* exception) {
    if (!shared || InterlockedCompareExchange(&crashing, 1, 0) != 0) return EXCEPTION_CONTINUE_SEARCH;
    shared->threadId = GetCurrentThreadId();
    shared->exception = *exception->ExceptionRecord;
    shared->exception.ExceptionRecord = nullptr;
    shared->context = *exception->ContextRecord;
    MemoryBarrier();
    SetEvent(requestEvent);
    WaitForSingleObject(doneEvent, 10000);
    return EXCEPTION_EXECUTE_HANDLER;
}
void fatalSignal(int signal) {
    CONTEXT context{}; RtlCaptureContext(&context);
    EXCEPTION_RECORD record{};
    record.ExceptionCode = signal == SIGABRT ? 0xE0000001 : 0xC0000005;
    record.ExceptionAddress = reinterpret_cast<void*>(context.Rip);
    EXCEPTION_POINTERS exception{&record, &context}; capture(&exception);
    TerminateProcess(GetCurrentProcess(), record.ExceptionCode);
}
void terminateHandler() { fatalSignal(SIGABRT); }
}
struct CrashCapture::Impl {
    HANDLE mapping = nullptr, request = nullptr, done = nullptr, ready = nullptr, process = nullptr;
    CrashShared* data = nullptr;
    LPTOP_LEVEL_EXCEPTION_FILTER previous = nullptr;
    std::terminate_handler terminate = nullptr;
    using SignalHandler = void (*)(int);
    SignalHandler abortHandler = SIG_DFL;
    bool installed = false;
    ~Impl() {
        if (installed) { SetUnhandledExceptionFilter(previous); std::set_terminate(terminate); std::signal(SIGABRT, abortHandler); }
        shared = nullptr;
        if (data) { data->shutdown = 1; if (request) SetEvent(request); }
        if (process) { WaitForSingleObject(process, 2000); CloseHandle(process); }
        if (data) UnmapViewOfFile(data);
        for (HANDLE h : {mapping, request, done, ready}) if (h) CloseHandle(h);
    }
};
#elif defined(Q_OS_MACOS) && ISPVIEW_HAS_CRASHPAD
struct CrashCapture::Impl { crashpad::CrashpadClient client; };
#else
struct CrashCapture::Impl {};
#endif
void captureFatalMessage() {
#ifdef Q_OS_WIN
    // Qt may use fast-fail after returning from its message handler, bypassing SEH/SIGABRT.
    if (shared) fatalSignal(SIGABRT);
#endif
}
CrashCapture::CrashCapture() : impl_(std::make_unique<Impl>()) {}
CrashCapture::~CrashCapture() = default;
QString CrashCapture::start(const QString& directory, const QString& executableDirectory, const QString& session) {
#ifdef Q_OS_WIN
    Q_UNUSED(session);
    auto& d = *impl_;
    QString bin = executableDirectory;
    if (bin.isEmpty()) {
        wchar_t path[32768]{}; const DWORD count = GetModuleFileNameW(nullptr, path, 32768);
        bin = QFileInfo(QString::fromWCharArray(path, static_cast<int>(count))).absolutePath();
    }
    const auto executable = QDir::toNativeSeparators(bin + QStringLiteral("/ispview_crash_handler.exe"));
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    d.mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, 0, sizeof(CrashShared), nullptr);
    d.request = CreateEventW(&sa, FALSE, FALSE, nullptr);
    d.done = CreateEventW(&sa, TRUE, FALSE, nullptr);
    d.ready = CreateEventW(&sa, TRUE, FALSE, nullptr);
    if (!d.mapping || !d.request || !d.done || !d.ready) return QStringLiteral("Cannot allocate crash handler IPC");
    d.data = static_cast<CrashShared*>(MapViewOfFile(d.mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(CrashShared)));
    if (!d.data) return QStringLiteral("Cannot map crash handler IPC");
    *d.data = {}; d.data->processId = GetCurrentProcessId();
    QString command = QStringLiteral("\"%1\" %2 %3 %4 %5 \"%6\"").arg(executable)
        .arg(reinterpret_cast<quintptr>(d.mapping)).arg(reinterpret_cast<quintptr>(d.request))
        .arg(reinterpret_cast<quintptr>(d.done)).arg(reinterpret_cast<quintptr>(d.ready))
        .arg(QDir::toNativeSeparators(directory + QStringLiteral("/crash.dmp")));
    auto wide = command.toStdWString();
    STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup); PROCESS_INFORMATION pi{};
    SIZE_T attributeBytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
    QByteArray attributes(static_cast<qsizetype>(attributeBytes), '\0');
    startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &attributeBytes))
        return QStringLiteral("Cannot initialize crash helper handle list");
    HANDLE inherited[]{d.mapping, d.request, d.done, d.ready};
    const bool attributesReady = UpdateProcThreadAttribute(startup.lpAttributeList, 0,
        PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr) != FALSE;
    const bool created = attributesReady && CreateProcessW(reinterpret_cast<LPCWSTR>(executable.utf16()),
        wide.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
        nullptr, nullptr, &startup.StartupInfo, &pi);
    const DWORD startError = GetLastError();
    DeleteProcThreadAttributeList(startup.lpAttributeList);
    if (!created) return QStringLiteral("Crash helper could not start (error %1)").arg(startError);
    d.process = pi.hProcess; CloseHandle(pi.hThread);
    if (WaitForSingleObject(d.ready, 5000) != WAIT_OBJECT_0 || d.data->result != ERROR_SUCCESS)
        return QStringLiteral("Crash helper failed to initialize");
    shared = d.data; requestEvent = d.request; doneEvent = d.done; crashing = 0;
    d.previous = SetUnhandledExceptionFilter(capture);
    d.terminate = std::set_terminate(terminateHandler);
    d.abortHandler = std::signal(SIGABRT, fatalSignal);
    d.installed = true;
    return {};
#elif defined(Q_OS_MACOS) && ISPVIEW_HAS_CRASHPAD
    QString bin = executableDirectory;
    if (bin.isEmpty()) {
        uint32_t size = 0; _NSGetExecutablePath(nullptr, &size);
        QByteArray path(static_cast<qsizetype>(size), '\0');
        if (_NSGetExecutablePath(path.data(), &size) != 0) return QStringLiteral("Cannot resolve crash handler path");
        bin = QFileInfo(QString::fromUtf8(path.constData())).absolutePath();
    }
    const base::FilePath database(directory.toStdString() + "/crashpad");
    auto db = crashpad::CrashReportDatabase::Initialize(database);
    if (!db || !db->GetSettings()->SetUploadsEnabled(false)) return QStringLiteral("Cannot initialize local crash database");
    const base::FilePath handler((bin + QStringLiteral("/../Helpers/crashpad_handler")).toStdString());
    std::map<std::string, std::string> annotations{{"session", session.toStdString()},
        {"version", ISPVIEW_PROJECT_VERSION}, {"build", ISPVIEW_BUILD_ID}};
    if (!impl_->client.StartHandler(handler, database, database, "", annotations,
                                   {"--no-rate-limit"}, true, false))
        return QStringLiteral("Crashpad helper failed to initialize");
    return {};
#else
    Q_UNUSED(directory); Q_UNUSED(executableDirectory); Q_UNUSED(session);
    return QStringLiteral("Crash capture is unavailable in this platform or build");
#endif
}
bool CrashCapture::alive() const {
#if defined(Q_OS_WIN)
    auto& d = *impl_;
    return d.process != nullptr && WaitForSingleObject(d.process, 0) == WAIT_TIMEOUT;
#elif defined(Q_OS_MACOS) && ISPVIEW_HAS_CRASHPAD
    // Crashpad starts and supervises its own handler; the client reports the configured state.
    return true;
#else
    return false;
#endif
}
}
