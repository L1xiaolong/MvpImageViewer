#include "browser/local_file_drop.h"

#include <QDir>
#include <QFileInfo>
#include <QMimeData>
#include <QUrl>

namespace ispview {
namespace {

void appendLocalUrl(const QUrl& url, QStringList* paths) {
    if (!url.isLocalFile()) {
        return;
    }
    const QString path = QFileInfo(url.toLocalFile()).absoluteFilePath();
    if (!path.isEmpty() && !paths->contains(path)) {
        paths->append(path);
    }
}

void appendUriList(const QByteArray& encoded, QStringList* paths) {
    for (const QByteArray& rawLine : encoded.split('\n')) {
        const QByteArray line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith('#')) {
            continue;
        }
        appendLocalUrl(QUrl::fromEncoded(line), paths);
    }
}

} // namespace

QStringList localFileDropPaths(const QMimeData* mimeData) {
    QStringList paths;
    if (!mimeData) {
        return paths;
    }

    for (const QUrl& url : mimeData->urls()) {
        appendLocalUrl(url, &paths);
    }

    // QMimeData::urls() normally decodes text/uri-list. Reading the bytes as a fallback also
    // covers native platform drags whose converter exposes the MIME payload but not hasUrls().
    if (paths.isEmpty() && mimeData->hasFormat(QStringLiteral("text/uri-list"))) {
        appendUriList(mimeData->data(QStringLiteral("text/uri-list")), &paths);
    }

    // Some drag sources publish file URLs or absolute paths only as plain text. Never interpret
    // arbitrary relative text as a path: it would turn normal text drags into unexpected folder
    // navigation.
    if (paths.isEmpty() && mimeData->hasText()) {
        for (const QString& rawLine : mimeData->text().split(QLatin1Char('\n'))) {
            const QString line = rawLine.trimmed();
            if (line.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive)) {
                appendLocalUrl(QUrl(line), &paths);
                continue;
            }
            if (QDir::isAbsolutePath(line) && QFileInfo::exists(line)) {
                const QString path = QFileInfo(line).absoluteFilePath();
                if (!paths.contains(path)) {
                    paths.append(path);
                }
            }
        }
    }
    return paths;
}

} // namespace ispview
