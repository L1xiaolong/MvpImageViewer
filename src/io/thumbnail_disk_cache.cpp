#include "io/thumbnail_disk_cache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

namespace ispview {

ThumbnailDiskCache::ThumbnailDiskCache(QString rootDirectory)
    : rootDirectory_(std::move(rootDirectory)) {
    if (rootDirectory_.isEmpty()) {
        rootDirectory_ = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
                         QStringLiteral("/thumbnails-v2");
    }
    QDir().mkpath(rootDirectory_);
}

QImage ThumbnailDiskCache::load(const QString& key) const {
    QImage image;
    const QString base = pathForKey(key);
    if (!image.load(base + QStringLiteral(".png"), "PNG")) {
        image.load(base + QStringLiteral(".jpg"), "JPEG");
    }
    return image;
}

bool ThumbnailDiskCache::store(const QString& key, const QImage& image,
                               const QSize& sourceSize, int validBits) const {
    if (image.isNull()) {
        return false;
    }
    QImage storedImage = image;
    if (sourceSize.isValid()) {
        storedImage.setText(QStringLiteral("ispview.sourceWidth"),
                            QString::number(sourceSize.width()));
        storedImage.setText(QStringLiteral("ispview.sourceHeight"),
                            QString::number(sourceSize.height()));
    }
    if (validBits > 0) {
        storedImage.setText(QStringLiteral("ispview.validBits"), QString::number(validBits));
    }
    bool hasTransparency = false;
    if (storedImage.hasAlphaChannel()) {
        const QImage alphaProbe =
            storedImage.format() == QImage::Format_ARGB32 ||
                    storedImage.format() == QImage::Format_RGBA8888
                ? storedImage
                : storedImage.convertToFormat(QImage::Format_RGBA8888);
        for (int y = 0; y < alphaProbe.height() && !hasTransparency; ++y) {
            const auto* pixels = reinterpret_cast<const QRgb*>(alphaProbe.constScanLine(y));
            for (int x = 0; x < alphaProbe.width(); ++x) {
                if (qAlpha(pixels[x]) != 255) {
                    hasTransparency = true;
                    break;
                }
            }
        }
    }
    const QString suffix = hasTransparency ? QStringLiteral(".png") : QStringLiteral(".jpg");
    QSaveFile file(pathForKey(key) + suffix);
    const char* format = hasTransparency ? "PNG" : "JPEG";
    const int quality = hasTransparency ? 20 : 85;
    if (!file.open(QIODevice::WriteOnly) || !storedImage.save(&file, format, quality)) {
        file.cancelWriting();
        return false;
    }
    const bool stored = file.commit();
    if (stored && (++storeCount_ % 64U) == 0U) trimIfNeeded();
    return stored;
}

QString ThumbnailDiskCache::pathForKey(const QString& key) const {
    const QByteArray digest =
        QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex();
    return rootDirectory_ + QLatin1Char('/') + QString::fromLatin1(digest);
}

void ThumbnailDiskCache::trimIfNeeded() const {
    constexpr qint64 maximumBytes = 512LL * 1024 * 1024;
    const QMutexLocker lock(&trimMutex_);
    QDir directory(rootDirectory_);
    QFileInfoList files = directory.entryInfoList(
        {QStringLiteral("*.png"), QStringLiteral("*.jpg")}, QDir::Files | QDir::Readable,
        QDir::Time | QDir::Reversed);
    qint64 totalBytes = 0;
    for (const QFileInfo& file : std::as_const(files)) totalBytes += file.size();
    if (totalBytes <= maximumBytes) return;
    const qint64 targetBytes = maximumBytes * 9 / 10;
    for (const QFileInfo& file : std::as_const(files)) {
        if (totalBytes <= targetBytes) break;
        const qint64 size = file.size();
        if (QFile::remove(file.absoluteFilePath())) totalBytes -= size;
    }
}

} // namespace ispview
