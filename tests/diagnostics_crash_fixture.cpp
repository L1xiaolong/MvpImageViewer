#include "diagnostics/diagnostics.h"
#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QThread>
#include <thread>
#include <stdexcept>
#include <cstdlib>
#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#endif
#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif
NOINLINE void diagnosticTestCrash(const QString& mode) {
    if (mode == QStringLiteral("abort")) std::abort();
    if (mode == QStringLiteral("fatal")) qFatal("diagnostic test fatal");
    if (mode == QStringLiteral("throw")) throw std::runtime_error("diagnostic test exception");
    *reinterpret_cast<volatile int*>(quintptr(1)) = 42;
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
#ifdef Q_OS_WIN
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    if (app.arguments().size() < 4) return 2;
    ispview::diagnostics::Options options;
    options.root = app.arguments()[1]; options.crashEnabled = app.arguments()[3] != QStringLiteral("off");
    ispview::diagnostics::Service service(options);
    service.startCrashCapture(QCoreApplication::applicationDirPath());
    if (options.crashEnabled && service.crashStatus() != QStringLiteral("active")) return 3;
    service.record(ispview::diagnostics::Level::Info, "fixture", QStringLiteral("before.crash"), {}, true);
    service.flush();
    QFile ready(options.root + QStringLiteral("/ready")); if (!ready.open(QIODevice::WriteOnly)) return 4; ready.close();
    const QString mode = app.arguments()[2];
    if (mode == QStringLiteral("helperkill")) {
#ifdef Q_OS_WIN
        // Terminate the crash helper so the client has to notice the lost crash capture.
        bool killed = false;
        if (HANDLE processes = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            processes != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
            if (Process32FirstW(processes, &entry)) {
                do {
                    if (QString::fromWCharArray(entry.szExeFile)
                            .compare(QStringLiteral("ispview_crash_handler.exe"), Qt::CaseInsensitive) == 0) {
                        if (HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, entry.th32ProcessID)) {
                            killed = TerminateProcess(process, 0) != FALSE;
                            CloseHandle(process);
                        }
                    }
                } while (!killed && Process32NextW(processes, &entry));
            }
            CloseHandle(processes);
        }
        if (!killed) return 5;
        QString status = service.crashStatus();
        for (int attempt = 0; attempt < 40 && status == QStringLiteral("active"); ++attempt) {
            QThread::msleep(50);
            status = service.crashStatus();
        }
        QFile report(options.root + QStringLiteral("/helper-status.txt"));
        if (!report.open(QIODevice::WriteOnly)) return 4;
        report.write(status.toUtf8()); report.close();
        return 0;
#else
        return 6;
#endif
    }
    if (mode == QStringLiteral("wait")) { QThread::sleep(60); return 0; }
    if (mode == QStringLiteral("clean")) { service.markCleanExit(); return 0; }
    if (app.arguments().contains(QStringLiteral("worker"))) {
        std::thread worker([&] { diagnosticTestCrash(mode); }); worker.join();
    } else diagnosticTestCrash(mode);
}
