#include "io/thumbnail_disk_cache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QBuffer>
#include <QDateTime>
#include <QScopeGuard>
#include <QFileInfo>
#include <QMutexLocker>
#include <QStandardPaths>

#include <algorithm>

namespace mvpview {
namespace {
QImage readCacheImage(const QString& path, const char* format) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QByteArray bytes = file.readAll();
    static const QByteArray pngTrailer = QByteArray::fromHex("0000000049454e44ae426082");
    static const QByteArray jpegTrailer = QByteArray::fromHex("ffd9");
    if (!bytes.endsWith(format[0] == 'P' ? pngTrailer : jpegTrailer)) return {};
    QImage image;
    return image.loadFromData(bytes, format) ? image : QImage{};
}
} // namespace

ThumbnailDiskCache::ThumbnailDiskCache(QString rootDirectory)
    : rootDirectory_(std::move(rootDirectory)) {
    if (rootDirectory_.isEmpty()) {
        rootDirectory_ = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
                         QStringLiteral("/thumbnails-v2");
    }
    maintenancePool_.setMaxThreadCount(1);
    QDir().mkpath(rootDirectory_);
}

QImage ThumbnailDiskCache::load(const QString& key) const {
    // A writer publishes a new immutable entry directly. Do not decode its
    // unfinished bytes, including when another process is filling this cache.
    const QMutexLocker lock(&writeKeysMutex_);
    if (writingKeys_.contains(key)) return {};
    const QString base = pathForKey(key);
    for (const auto* format : {"PNG", "JPEG"}) {
        const QImage image = readCacheImage(
            base + (format[0] == 'P' ? QStringLiteral(".png") : QStringLiteral(".jpg")), format);
        if (!image.isNull()) return image;
    }
    return {};
}

bool ThumbnailDiskCache::store(const QString& key, const QImage& image,
                               const QSize& sourceSize, int validBits) const {
    if (image.isNull()) {
        return false;
    }
    {
        const QMutexLocker lock(&writeKeysMutex_);
        if (writingKeys_.contains(key)) return false;
        writingKeys_.insert(key);
    }
    const auto releaseKey = qScopeGuard([this, &key] {
        const QMutexLocker lock(&writeKeysMutex_);
        writingKeys_.remove(key);
    });
    QImage storedImage = image;
    if (sourceSize.isValid()) {
        storedImage.setText(QStringLiteral("mvpview.sourceWidth"),
                            QString::number(sourceSize.width()));
        storedImage.setText(QStringLiteral("mvpview.sourceHeight"),
                            QString::number(sourceSize.height()));
    }
    if (validBits > 0) {
        storedImage.setText(QStringLiteral("mvpview.validBits"), QString::number(validBits));
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
    const QString path = pathForKey(key) + suffix;
    const char* format = hasTransparency ? "PNG" : "JPEG";
    const int quality = hasTransparency ? 20 : 85;
    QByteArray encoded;
    QBuffer buffer(&encoded);
    if (!buffer.open(QIODevice::WriteOnly) || !storedImage.save(&buffer, format, quality)) return false;
    // These files are disposable, content-addressed cache entries. Replacing
    // hundreds of them through QSaveFile triggers process termination on some
    // Windows installations. Create each key once without temporary-file renames;
    // a partial entry is rejected by load() and the source is decoded normally.
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return !readCacheImage(path, format).isNull();
    if (file.write(encoded) != encoded.size() || !file.flush()) {
        file.close();
        (void)file.remove();
        return false;
    }
    file.close();
    if ((++storeCount_ % 64U) == 0U) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        qint64 previous = lastTrimMs_.load();
        bool idle = false;
        if (now - previous >= 60'000 && lastTrimMs_.compare_exchange_strong(previous, now) &&
            maintenanceScheduled_.compare_exchange_strong(idle, true)) {
            maintenancePool_.start([this] {
                const auto finished = qScopeGuard([this] { maintenanceScheduled_.store(false); });
                trimIfNeeded();
            });
        }
    }
    return true;
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

} // namespace mvpview
