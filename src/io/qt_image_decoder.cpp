#include "io/qt_image_decoder.h"

#include "io/encoded_color_management.h"
#include "io/metadata_reader.h"

#include <QColorSpace>
#include <QFileInfo>
#include <QImageReader>

#include <algorithm>
#include <atomic>

namespace ispview {
namespace {
std::atomic_bool autoOrientation{true};
std::atomic_bool keepHighBitDepth{true};

const QList<QByteArray>& availableFormats() {
    static const QList<QByteArray> formats = [] {
        QList<QByteArray> result = QImageReader::supportedImageFormats();
        for (QByteArray& format : result) {
            format = format.toLower();
        }
        return result;
    }();
    return formats;
}

bool containsAny(const QList<QByteArray>& formats,
                 std::initializer_list<QByteArray> candidates) {
    return std::any_of(candidates.begin(), candidates.end(), [&formats](const QByteArray& value) {
        return formats.contains(value);
    });
}

QByteArray readerFormatForSuffix(const QString& suffix) {
    if (suffix == QStringLiteral("dib")) {
        return QByteArrayLiteral("bmp");
    }
    if (suffix == QStringLiteral("heic") || suffix == QStringLiteral("heif")) {
        const QList<QByteArray>& formats = availableFormats();
        const QByteArray requested = suffix.toLatin1();
        if (formats.contains(requested)) {
            return requested;
        }
        return formats.contains(QByteArrayLiteral("heic")) ? QByteArrayLiteral("heic")
                                                           : QByteArrayLiteral("heif");
    }
    return suffix.toLatin1();
}
}

bool QtImageDecoder::autoOrientationEnabled() {
    return autoOrientation.load(std::memory_order_relaxed);
}

bool QtImageDecoder::preserveHighBitDepth() {
    return keepHighBitDepth.load(std::memory_order_relaxed);
}

QStringList QtImageDecoder::supportedSuffixes() {
    static const QStringList suffixes = [] {
        const QList<QByteArray>& formats = availableFormats();
        QStringList result;
        if (containsAny(formats, {QByteArrayLiteral("jpg"), QByteArrayLiteral("jpeg")})) {
            result.append({QStringLiteral("jpg"), QStringLiteral("jpeg")});
        }
        if (formats.contains(QByteArrayLiteral("png"))) {
            result.append(QStringLiteral("png"));
        }
        if (formats.contains(QByteArrayLiteral("bmp"))) {
            result.append({QStringLiteral("bmp"), QStringLiteral("dib")});
        }
        if (containsAny(formats, {QByteArrayLiteral("heic"), QByteArrayLiteral("heif")})) {
            result.append({QStringLiteral("heic"), QStringLiteral("heif")});
        }
        return result;
    }();
    return suffixes;
}

void QtImageDecoder::setAutoOrientationEnabled(bool enabled) {
    autoOrientation.store(enabled, std::memory_order_relaxed);
}

void QtImageDecoder::setPreserveHighBitDepth(bool enabled) {
    keepHighBitDepth.store(enabled, std::memory_order_relaxed);
}

QString QtImageDecoder::cacheIdentity() const {
    const QString colorIdentity =
        EncodedColorManagement::isAvailable()
            ? QStringLiteral("lcms-%1").arg(EncodedColorManagement::version())
            : QStringLiteral("lcms-disabled");
    return QStringLiteral("qt-image-v5|qt-%1|formats-%2|%3|icc-%4|orientation-%5|depth-%6")
        .arg(QString::fromLatin1(qVersion()), supportedSuffixes().join(QLatin1Char(',')),
             colorIdentity,
             EncodedColorManagement::isEnabled() ? QStringLiteral("on") : QStringLiteral("off"),
             autoOrientationEnabled() ? QStringLiteral("on") : QStringLiteral("off"),
             preserveHighBitDepth() ? QStringLiteral("native") : QStringLiteral("8"));
}

bool QtImageDecoder::canDecode(const QString& path) const {
    const QString suffix = QFileInfo(path).suffix().toLower();
    return supportedSuffixes().contains(suffix);
}

DecodeResult QtImageDecoder::decode(const DecodeRequest& request) const {
    if (!canDecode(request.path)) {
        return {{}, QStringLiteral("Unsupported image format")};
    }

    const QString suffix = QFileInfo(request.path).suffix().toLower();
    const QByteArray readerFormat = readerFormatForSuffix(suffix);
    QImageReader reader(request.path, readerFormat);
    reader.setAutoTransform(autoOrientationEnabled());
    // The registry has already validated the suffix. Selecting the corresponding Qt
    // decoder explicitly avoids a second content probe and keeps thumbnail loading cheap.
    reader.setDecideFormatFromContent(false);
    const QString fileFormat = QFileInfo(request.path).suffix().toUpper();
    const QSize sourceSize = reader.size();

    if (!request.maximumSize.isEmpty()) {
        if (sourceSize.isValid() && (sourceSize.width() > request.maximumSize.width() ||
                                     sourceSize.height() > request.maximumSize.height())) {
            reader.setScaledSize(sourceSize.scaled(request.maximumSize, Qt::KeepAspectRatio));
        }
    }

    QImage image = reader.read();
    if (image.isNull()) {
        return {{}, reader.errorString()};
    }

    const bool sourceHighBitDepth = image.depth() > 32;
    const bool highBitDepth = sourceHighBitDepth && preserveHighBitDepth();
    if (highBitDepth) {
        if (image.format() != QImage::Format_RGBA64 &&
            image.format() != QImage::Format_RGBA16FPx4 &&
            image.format() != QImage::Format_RGBA32FPx4) {
            image = image.convertToFormat(QImage::Format_RGBA64);
        }
    } else if (image.format() != QImage::Format_RGBA8888) {
        image = image.convertToFormat(QImage::Format_RGBA8888);
    }

    const QFileInfo fileInfo(request.path);
    auto frame = std::make_shared<ImageFrame>();
    frame->descriptor.size = image.size();
    frame->descriptor.layout = PixelLayout::Interleaved;
    frame->descriptor.sampleType = SampleType::UInt;
    frame->descriptor.channelOrder = ChannelOrder::RGBA;
    frame->descriptor.storageBits = highBitDepth ? 16 : 8;
    frame->descriptor.validBits = highBitDepth ? 16 : 8;
    frame->metadata.path = fileInfo.absoluteFilePath();
    frame->metadata.fileName = fileInfo.fileName();
    frame->metadata.format = fileFormat;
    frame->metadata.fileSize = fileInfo.size();
    frame->metadata.sourceSize = sourceSize.isValid() ? sourceSize : image.size();
    frame->metadata.modifiedAt = fileInfo.lastModified();
    frame->metadata.decoderName = QStringLiteral("Qt Image Formats");
    frame->metadata.decoderVersion = QString::fromLatin1(qVersion());
    // The current LittleCMS adapter is deliberately RGBA8-only. Preserve high-bit source
    // samples until a true 16-bit transform is available instead of silently quantizing them.
    if (highBitDepth && image.colorSpace().isValid()) {
        ImageMetadata::ColorProfile profile;
        profile.sourceDescription = image.colorSpace().description();
        profile.destinationColorSpace = QStringLiteral("Unchanged");
        profile.transformEngine = QStringLiteral("Not applied — high-bit ICC path pending");
        frame->metadata.colorProfile = std::move(profile);
        frame->metadata.colorWarning =
            QStringLiteral("Embedded profile retained; 16-bit ICC conversion is not implemented");
    } else {
        EncodedColorManagement::normalizeToSrgb(image, frame->metadata);
    }
    if (frame->metadata.colorProfile && !frame->metadata.colorProfile->converted) {
        frame->descriptor.color.colorSpace = frame->metadata.colorProfile->sourceDescription;
        frame->descriptor.color.transferFunction = QStringLiteral("ICC");
    } else {
        frame->descriptor.color.colorSpace = QStringLiteral("sRGB");
        frame->descriptor.color.transferFunction = QStringLiteral("sRGB");
    }
    // Metadata is not needed for browser tiles. Skipping it preserves parallel thumbnail
    // throughput; Preview/Full share the bounded metadata cache in MetadataReader.
    if (request.purpose != DecodePurpose::Thumbnail) {
        MetadataReader::enrich(request.path, frame->metadata);
    }
    frame->storage = std::move(image);
    return {std::move(frame), {}};
}

} // namespace ispview
