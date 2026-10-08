#include "browser/file_clipboard.h"

#include "browser/local_file_drop.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QUrl>

namespace mvpview {
namespace { bool filesCached = false; bool cachedHasFiles = false; }

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
