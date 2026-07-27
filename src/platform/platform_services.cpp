#include "platform/platform_services.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QUrl>

#ifdef Q_OS_WIN
#include <malloc.h>
#include <windows.h>
#endif

namespace ispview {

bool PlatformServices::revealInFileManager(const QString& path) {
#if defined(Q_OS_MACOS)
    return QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-R"), path});
#elif defined(Q_OS_WINDOWS)
    return QProcess::startDetached(
        QStringLiteral("explorer.exe"),
        {QStringLiteral("/select,%1").arg(QDir::toNativeSeparators(path))});
#else
    return QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
}

bool PlatformServices::openDirectoryInFileManager(const QString& path) {
    const QFileInfo info(path);
    const QString directory = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    return !directory.isEmpty() && QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
}

void PlatformServices::releaseUnusedMemory() {
#ifdef Q_OS_WIN
    (void)_heapmin();
    const SIZE_T releaseAll = static_cast<SIZE_T>(-1);
    (void)SetProcessWorkingSetSize(GetCurrentProcess(), releaseAll, releaseAll);
#endif
}

} // namespace ispview
