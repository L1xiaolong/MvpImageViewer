#include "browser/file_clipboard.h"

#include "browser/local_file_drop.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QUrl>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <array>
#endif

namespace mvpview {
namespace {
bool filesCached = false;
bool cachedHasFiles = false;
#ifdef Q_OS_WIN
bool mayContainFilePaths() {
    // Offscreen and other QPA plugins have their own clipboard, independent of Win32.
    if (QGuiApplication::platformName() != QStringLiteral("windows")) return true;
    static const std::array<UINT, 10> formats{
        CF_HDROP, CF_UNICODETEXT, CF_TEXT,
        RegisterClipboardFormatW(L"UniformResourceLocatorW"),
        RegisterClipboardFormatW(L"UniformResourceLocator"),
        RegisterClipboardFormatW(L"FileNameW"), RegisterClipboardFormatW(L"FileName"),
        RegisterClipboardFormatW(L"text/uri-list"), RegisterClipboardFormatW(L"text/plain"),
        RegisterClipboardFormatW(L"Shell IDList Array")
    };
    // Format availability does not retrieve delayed-rendered data or enumerate an
    // external IDataObject. Positive/uncertain cases retain Qt's existing URL/text parsing.
    for (const UINT format : formats) {
        if (!format) return true;
        SetLastError(ERROR_SUCCESS);
        if (IsClipboardFormatAvailable(format)) return true;
        if (GetLastError() != ERROR_SUCCESS) return true;
    }
    return false;
}
#endif
}

void FileClipboard::initialize() {
    // Connect before browser bindings: one clipboard query per change, not per menu item/pane.
    auto* clipboard = QGuiApplication::clipboard();
    if (clipboard->property("mvpviewCacheInstalled").toBool()) return;
    clipboard->setProperty("mvpviewCacheInstalled", true);
    QObject::connect(clipboard, &QClipboard::dataChanged, clipboard, [] { filesCached = false; });
}

void FileClipboard::setPaths(const QStringList& paths, bool cut) {
    QList<QUrl> urls;
    urls.reserve(paths.size());
    for (const QString& path : paths) {
        urls.append(QUrl::fromLocalFile(path));
    }
    auto* mimeData = new QMimeData;
    mimeData->setUrls(urls);
    if (cut) {
        mimeData->setData(QString::fromLatin1(CutMimeType), QByteArrayLiteral("1"));
    }
    QGuiApplication::clipboard()->setMimeData(mimeData);
}

FileClipboardContents FileClipboard::contents() {
    const QMimeData* mimeData = QGuiApplication::clipboard()->mimeData();
    return {localFileDropPaths(mimeData),
            mimeData && mimeData->hasFormat(QString::fromLatin1(CutMimeType))};
}

bool FileClipboard::hasFiles() {
    initialize();
    if (!filesCached) {
#ifdef Q_OS_WIN
        if (!mayContainFilePaths()) {
            cachedHasFiles = false;
            filesCached = true;

            return false;
        }
#endif
        // Snapshot advertised formats once. Querying absent Windows clipboard formats
        // repeatedly can retry OpenClipboard and stall the first UI bindings.
        QMimeData snapshot;
        const QMimeData* source = QGuiApplication::clipboard()->mimeData();
        const QStringList formats = source ? source->formats() : QStringList{};
        for (const QString& format : {QStringLiteral("text/uri-list"), QStringLiteral("text/plain")}) {
            if (formats.contains(format)) snapshot.setData(format, source->data(format));
        }
        cachedHasFiles = !localFileDropPaths(&snapshot).isEmpty();
        filesCached = true;
    }
    return cachedHasFiles;
}

void FileClipboard::clear() { QGuiApplication::clipboard()->clear(); }
} // namespace mvpview
