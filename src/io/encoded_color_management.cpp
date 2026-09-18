#include "io/encoded_color_management.h"

#include <QByteArray>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QHash>
#include <QList>
#include <QMutex>
#include <QMutexLocker>

#include <algorithm>
#include <atomic>
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
std::atomic_bool colorManagementEnabled{true};

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

enum class TransformSampleFormat : char { Rgba8 = '8', Rgba16 = '6', RgbaFloat = 'f' };

cmsUInt32Number lcmsPixelType(TransformSampleFormat format) {
    switch (format) {
    case TransformSampleFormat::Rgba16:
        return TYPE_RGBA_16;
    case TransformSampleFormat::RgbaFloat:
        return TYPE_RGBA_FLT;
    case TransformSampleFormat::Rgba8:
    default:
        return TYPE_RGBA_8;
    }
}

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

// Opens the destination profile of a display space. sRGB keeps LittleCMS' own profile, which is
// what the viewer used before the display space became configurable; the wider spaces come from
// Qt so the ICC data matches the QColorSpace the decoders tag their images with.
cmsHPROFILE createDestinationProfile(cmsContext context, DisplayColorSpace target,
                                     LcmsErrorState& errorState) {
    if (target == DisplayColorSpace::Srgb) {
        return cmsCreate_sRGBProfileTHR(context);
    }
    const QByteArray profile = displayQColorSpace(target).iccProfile();
    if (profile.isEmpty()) {
        errorState.message =
            QStringLiteral("No ICC profile is available for %1").arg(displayColorSpaceName(target));
        return nullptr;
    }
    cmsHPROFILE opened = cmsOpenProfileFromMemTHR(
        context, profile.constData(), static_cast<cmsUInt32Number>(profile.size()));
    if (!opened) {
        errorState.message = QStringLiteral("LittleCMS rejected the %1 display profile")
                                 .arg(displayColorSpaceName(target));
    }
    return opened;
}

std::shared_ptr<LcmsTransform> createTransform(const QByteArray& profile,
                                              const QString& fallbackDescription,
                                              TransformSampleFormat sampleFormat,
                                              DisplayColorSpace target) {
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
    result->destinationProfile = createDestinationProfile(result->context, target, result->errorState);
    if (!result->destinationProfile) {
        result->creationError =
            result->errorState.message.isEmpty()
                ? QStringLiteral("LittleCMS could not create the display profile")
                : result->errorState.message;
        return result;
    }
    const cmsUInt32Number pixelType = lcmsPixelType(sampleFormat);
    result->transform = cmsCreateTransformTHR(
        result->context, result->sourceProfile, pixelType, result->destinationProfile,
        pixelType, INTENT_RELATIVE_COLORIMETRIC, cmsFLAGS_COPY_ALPHA);
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
                                              const QString& fallbackDescription,
                                              TransformSampleFormat sampleFormat,
                                              DisplayColorSpace target) {
    constexpr qsizetype kMaximumCachedTransforms = 12;
    QByteArray key = QCryptographicHash::hash(profile, QCryptographicHash::Sha256);
    key.append(static_cast<char>(sampleFormat));
    // The same source profile produces different pixels per target, so the display space is part
    // of the key.
    key.append(displayColorSpaceKey(target).toLatin1());
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
        createTransform(profile, fallbackDescription, sampleFormat, target);
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

bool EncodedColorManagement::isEnabled() {
    return colorManagementEnabled.load(std::memory_order_relaxed);
}

void EncodedColorManagement::setEnabled(bool enabled) {
    colorManagementEnabled.store(enabled, std::memory_order_relaxed);
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

void EncodedColorManagement::normalizeToDisplay(QImage& image, ImageMetadata& metadata) {
    normalizeToDisplay(image, metadata, currentDisplayColorSpace());
}

void EncodedColorManagement::normalizeToDisplay(QImage& image, ImageMetadata& metadata,
                                                DisplayColorSpace target) {
    const QColorSpace targetColorSpace = displayQColorSpace(target);
    // Untagged encoded images follow the viewer's documented sRGB assumption, so an untagged
    // image still needs conversion when the display space is wider than sRGB.
    const bool tagged = image.colorSpace().isValid();
    const QColorSpace sourceColorSpace =
        tagged ? image.colorSpace() : QColorSpace(QColorSpace::SRgb);
    if (sourceColorSpace == targetColorSpace) {
        return;
    }
    const QByteArray embeddedProfile = sourceColorSpace.iccProfile();
    if (embeddedProfile.isEmpty()) {
        return;
    }

    ImageMetadata::ColorProfile colorProfile;
    colorProfile.sourceDescription = tagged ? boundedProfileText(sourceColorSpace.description())
                                            : QStringLiteral("Assumed sRGB (no embedded profile)");
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
    if (!isEnabled()) {
        colorProfile.transformEngine = QStringLiteral("Disabled in settings");
        metadata.colorProfile = std::move(colorProfile);
        return;
    }

#if ISPVIEW_HAS_LCMS2
    TransformSampleFormat sampleFormat = TransformSampleFormat::Rgba8;
    QImage::Format targetFormat = QImage::Format_RGBA8888;
    qsizetype bytesPerPixel = 4;
    if (image.depth() > 32) {
        if (image.format() == QImage::Format_RGBA16FPx4 ||
            image.format() == QImage::Format_RGBA32FPx4) {
            // LittleCMS consumes native float32 values. Promote half-float images so the
            // conversion itself does not introduce an intermediate 8-bit quantization.
            sampleFormat = TransformSampleFormat::RgbaFloat;
            targetFormat = QImage::Format_RGBA32FPx4;
            bytesPerPixel = 4 * static_cast<qsizetype>(sizeof(float));
        } else {
            sampleFormat = TransformSampleFormat::Rgba16;
            targetFormat = QImage::Format_RGBA64;
            bytesPerPixel = 4 * static_cast<qsizetype>(sizeof(quint16));
        }
    }
    const std::shared_ptr<LcmsTransform> transform =
        cachedTransform(embeddedProfile, colorProfile.sourceDescription, sampleFormat, target);
    if (!transform->isValid()) {
        metadata.colorWarning = transform->creationError;
        metadata.colorProfile = std::move(colorProfile);
        return;
    }
    colorProfile.sourceDescription = transform->sourceDescription;
    if (!tagged) {
        // Say that the source space is an assumption rather than the profile's own description.
        colorProfile.sourceDescription = QStringLiteral("Assumed sRGB (no embedded profile)");
    }
    colorProfile.renderingIntent = QStringLiteral("Relative colorimetric");

    if (image.format() != targetFormat) {
        image = image.convertToFormat(targetFormat);
    }
    image.detach();
    const qsizetype rowBytes = static_cast<qsizetype>(image.width()) * bytesPerPixel;
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
    image.setColorSpace(targetColorSpace);
    colorProfile.destinationColorSpace = displayColorSpaceName(target);
    colorProfile.transformEngine = QStringLiteral("LittleCMS %1").arg(version());
    colorProfile.converted = true;
#else
    colorProfile.transformEngine = QStringLiteral("Not applied — LittleCMS unavailable");
#endif
    metadata.colorProfile = std::move(colorProfile);
}

} // namespace ispview
