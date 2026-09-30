#include "browser/file_clipboard.h"

#include "browser/local_file_drop.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QUrl>

namespace mvpview {

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

bool FileClipboard::hasFiles() { return !contents().paths.isEmpty(); }

void FileClipboard::clear() { QGuiApplication::clipboard()->clear(); }

} // namespace mvpview
