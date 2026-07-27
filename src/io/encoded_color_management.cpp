#include "io/encoded_color_management.h"

#include <QByteArray>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QHash>
#include <QList>
#include <QMutex>
#include <QMutexLocker>

#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>

#if ISPVIEW_HAS_LCMS2
#include <lcms2.h>
#endif

namespace ispview {
namespace {

constexpr qsizetype kMaximumIccProfileBytes = 64LL * 1024 * 1024;
constexpr qsizetype kMaximumProfileTextLength = 512;

QString boundedProfileText(QString text) {
    text.replace(QChar::Null, QChar::ReplacementCharacter);
    for (QChar& character : text) {
        if (character.category() == QChar::Other_Control && character != QLatin1Char('\n') &&
            character != QLatin1Char('\t')) {
            character = QChar::ReplacementCharacter;
        }
    }
    return text.trimmed().left(kMaximumProfileTextLength);
}

QString profileFingerprint(const QByteArray& profile) {
    return QString::fromLatin1(
        QCryptographicHash::hash(profile, QCryptographicHash::Sha256).toHex().left(16));
}

#if ISPVIEW_HAS_LCMS2

constexpr qsizetype kMaximumColorWarningLength = 512;
constexpr int kTransformRowsPerChunk = 64;

QString boundedWarning(QString warning) {
    warning.replace(QChar::Null, QChar::ReplacementCharacter);
    return warning.trimmed().left(kMaximumColorWarningLength);
}

struct LcmsErrorState {
    QString message;
};

void lcmsErrorHandler(cmsContext context, cmsUInt32Number, const char* text) {
    if (auto* state = static_cast<LcmsErrorState*>(cmsGetContextUserData(context))) {
        state->message = boundedWarning(QString::fromUtf8(text));
    }
}

struct LcmsTransform {
    LcmsErrorState errorState;
    cmsContext context = nullptr;
    cmsHPROFILE sourceProfile = nullptr;
    cmsHPROFILE destinationProfile = nullptr;
    cmsHTRANSFORM transform = nullptr;
    QString sourceDescription;
    QString creationError;
    QMutex useMutex;

    ~LcmsTransform() {
        if (transform) {
            cmsDeleteTransform(transform);
        }
        if (destinationProfile) {
            cmsCloseProfile(destinationProfile);
        }
        if (sourceProfile) {
            cmsCloseProfile(sourceProfile);
        }
        if (context) {
            cmsDeleteContext(context);
        }
    }

    [[nodiscard]] bool isValid() const { return transform != nullptr; }
};

QString profileDescription(cmsHPROFILE profile, const QString& fallback) {
    const cmsUInt32Number required =
        cmsGetProfileInfoUTF8(profile, cmsInfoDescription, "en", "US", nullptr, 0);
    if (required <= 1 || required > 4096) {
        return fallback;
    }
    QByteArray buffer(static_cast<qsizetype>(required), Qt::Uninitialized);
    if (cmsGetProfileInfoUTF8(profile, cmsInfoDescription, "en", "US", buffer.data(), required) ==
        0) {
        return fallback;
    }
    return boundedProfileText(QString::fromUtf8(buffer.constData()));
}

std::shared_ptr<LcmsTransform> createTransform(const QByteArray& profile,
                                              const QString& fallbackDescription) {
    auto result = std::make_shared<LcmsTransform>();
    result->context = cmsCreateContext(nullptr, &result->errorState);
    if (!result->context) {
        result->creationError = QStringLiteral("LittleCMS could not create a conversion context");
        return result;
    }
    cmsSetLogErrorHandlerTHR(result->context, lcmsErrorHandler);
    result->sourceProfile = cmsOpenProfileFromMemTHR(
        result->context, profile.constData(), static_cast<cmsUInt32Number>(profile.size()));
    if (!result->sourceProfile) {
        result->creationError =
            result->errorState.message.isEmpty()
                ? QStringLiteral("LittleCMS rejected the embedded ICC profile")
                : result->errorState.message;
        return result;
    }
    result->sourceDescription =
        profileDescription(result->sourceProfile, fallbackDescription);
    if (cmsGetColorSpace(result->sourceProfile) != cmsSigRgbData) {
        result->creationError = QStringLiteral("Only embedded RGB ICC profiles are supported");
        return result;
    }
    result->destinationProfile = cmsCreate_sRGBProfileTHR(result->context);
    if (!result->destinationProfile) {
        result->creationError = QStringLiteral("LittleCMS could not create the sRGB profile");
        return result;
    }
    result->transform = cmsCreateTransformTHR(
        result->context, result->sourceProfile, TYPE_RGBA_8, result->destinationProfile,
        TYPE_RGBA_8, INTENT_RELATIVE_COLORIMETRIC, cmsFLAGS_COPY_ALPHA);
    if (!result->transform) {
        result->creationError =
            result->errorState.message.isEmpty()
                ? QStringLiteral("LittleCMS could not create the ICC transform")
                : result->errorState.message;
    }
    return result;
}

struct TransformCache {
    QMutex mutex;
    QHash<QByteArray, std::shared_ptr<LcmsTransform>> entries;
    QList<QByteArray> leastRecentlyUsed;
};

TransformCache& transformCache() {
    static TransformCache cache;
    return cache;
}

std::shared_ptr<LcmsTransform> cachedTransform(const QByteArray& profile,
                                              const QString& fallbackDescription) {
    constexpr qsizetype kMaximumCachedTransforms = 8;
    const QByteArray key = QCryptographicHash::hash(profile, QCryptographicHash::Sha256);
    TransformCache& cache = transformCache();
    {
        const QMutexLocker lock(&cache.mutex);
        if (const auto found = cache.entries.constFind(key); found != cache.entries.cend()) {
            cache.leastRecentlyUsed.removeAll(key);
            cache.leastRecentlyUsed.append(key);
            return *found;
        }
    }

    const std::shared_ptr<LcmsTransform> created =
        createTransform(profile, fallbackDescription);
    if (!created->isValid()) {
        return created;
    }

    const QMutexLocker lock(&cache.mutex);
    if (const auto found = cache.entries.constFind(key); found != cache.entries.cend()) {
        cache.leastRecentlyUsed.removeAll(key);
        cache.leastRecentlyUsed.append(key);
        return *found;
    }
    cache.entries.insert(key, created);
    cache.leastRecentlyUsed.append(key);
    while (cache.leastRecentlyUsed.size() > kMaximumCachedTransforms) {
        cache.entries.remove(cache.leastRecentlyUsed.takeFirst());
    }
    return created;
}

#endif

} // namespace

bool EncodedColorManagement::isAvailable() {
#if ISPVIEW_HAS_LCMS2
    return true;
#else
    return false;
#endif
}

QString EncodedColorManagement::version() {
#if ISPVIEW_HAS_LCMS2
    return QStringLiteral("%1.%2")
        .arg(LCMS_VERSION / 1000)
        .arg((LCMS_VERSION % 1000) / 10);
#else
    return {};
#endif
}

void EncodedColorManagement::normalizeToSrgb(QImage& image, ImageMetadata& metadata) {
    const QColorSpace sourceColorSpace = image.colorSpace();
    if (!sourceColorSpace.isValid()) {
        return;
    }
    const QByteArray embeddedProfile = sourceColorSpace.iccProfile();
    if (embeddedProfile.isEmpty()) {
        return;
    }

    ImageMetadata::ColorProfile colorProfile;
    colorProfile.sourceDescription = boundedProfileText(sourceColorSpace.description());
    if (colorProfile.sourceDescription.isEmpty()) {
        colorProfile.sourceDescription = QStringLiteral("Embedded ICC profile");
    }
    colorProfile.destinationColorSpace = QStringLiteral("Unchanged");
    if (embeddedProfile.size() > kMaximumIccProfileBytes) {
        metadata.colorWarning = QStringLiteral("Embedded ICC profile exceeds the 64 MiB limit");
        metadata.colorProfile = std::move(colorProfile);
        return;
    }
    colorProfile.sourceFingerprint = profileFingerprint(embeddedProfile);

#if ISPVIEW_HAS_LCMS2
    const std::shared_ptr<LcmsTransform> transform =
        cachedTransform(embeddedProfile, colorProfile.sourceDescription);
    if (!transform->isValid()) {
        metadata.colorWarning = transform->creationError;
        metadata.colorProfile = std::move(colorProfile);
        return;
    }
    colorProfile.sourceDescription = transform->sourceDescription;
    colorProfile.renderingIntent = QStringLiteral("Relative colorimetric");

    if (image.format() != QImage::Format_RGBA8888) {
        image = image.convertToFormat(QImage::Format_RGBA8888);
    }
    image.detach();
    const qsizetype rowBytes = static_cast<qsizetype>(image.width()) * 4;
    const int maximumChunkRows = std::min(kTransformRowsPerChunk, image.height());
    QByteArray sourceChunk(rowBytes * maximumChunkRows, Qt::Uninitialized);
    if (image.bytesPerLine() != rowBytes ||
        sourceChunk.size() != rowBytes * maximumChunkRows) {
        metadata.colorWarning = QStringLiteral("Could not allocate an ICC chunk buffer");
        metadata.colorProfile = std::move(colorProfile);
        return;
    }
    const quint64 maximumChunkPixels = quint64(image.width()) * quint64(maximumChunkRows);
    if (maximumChunkPixels > std::numeric_limits<cmsUInt32Number>::max()) {
        metadata.colorWarning = QStringLiteral("ICC transform chunk exceeds the pixel limit");
        metadata.colorProfile = std::move(colorProfile);
        return;
    }
    {
        // LittleCMS transforms are reusable. Serialize use of each cached transform so this
        // remains safe across thumbnail and interactive decoder pools on every supported build.
        const QMutexLocker transformLock(&transform->useMutex);
        for (int firstRow = 0; firstRow < image.height(); firstRow += kTransformRowsPerChunk) {
            const int chunkRows = std::min(kTransformRowsPerChunk, image.height() - firstRow);
            const qsizetype chunkBytes = rowBytes * chunkRows;
            const quint64 chunkPixels = quint64(image.width()) * quint64(chunkRows);
            std::memcpy(sourceChunk.data(), image.constScanLine(firstRow),
                        static_cast<std::size_t>(chunkBytes));
            cmsDoTransform(transform->transform, sourceChunk.constData(), image.scanLine(firstRow),
                           static_cast<cmsUInt32Number>(chunkPixels));
        }
    }
    image.setColorSpace(QColorSpace(QColorSpace::SRgb));
    colorProfile.destinationColorSpace = QStringLiteral("sRGB");
    colorProfile.transformEngine = QStringLiteral("LittleCMS %1").arg(version());
    colorProfile.converted = true;
#else
    colorProfile.transformEngine = QStringLiteral("Not applied — LittleCMS unavailable");
#endif
    metadata.colorProfile = std::move(colorProfile);
}

} // namespace ispview
