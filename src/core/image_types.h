#pragma once

#include "core/raw_image_parameters.h"

#include <QByteArray>
#include <QDateTime>
#include <QImage>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>
#include <optional>
#include <variant>

namespace ispview {

enum class PixelLayout { Interleaved, Planar, SemiPlanar, Bayer, Packed };
enum class SampleType { UInt, SInt, Float };
enum class ChannelOrder { Gray, RGB, BGR, RGBA, BGRA, YUV, YVU, Bayer };

struct ColorDescriptor {
    QString colorSpace = QStringLiteral("sRGB");
    QString primaries = QStringLiteral("sRGB / BT.709");
    QString transferFunction = QStringLiteral("sRGB");
    QString matrixCoefficients = QStringLiteral("RGB");
    QString chromaLocation;
    bool fullRange = true;
};

struct ImageDescriptor {
    QSize size;
    PixelLayout layout = PixelLayout::Interleaved;
    SampleType sampleType = SampleType::UInt;
    ChannelOrder channelOrder = ChannelOrder::RGBA;
    int storageBits = 8;
    int validBits = 8;
    ColorDescriptor sourceColor;
    ColorDescriptor displayColor;
};

struct PlaneBuffer {
    qsizetype offset = 0;
    qsizetype stride = 0;
    qsizetype byteSize = 0;
};

struct PlaneBufferSet {
    QByteArray storage;
    QVector<PlaneBuffer> planes;
    // Bounded CPU reference conversion for fallback, probes, and tests. Its size may be
    // smaller than ImageDescriptor::size; supported RAW/YUV formats render full source
    // planes on the GPU.
    QImage displayImage;
    // Camera RAW keeps LibRaw's processed bitmap as the rendered image while the sensor
    // mosaic is exposed to exact pixel probes only.
    bool renderFromDisplayImage = false;
};

struct ImageMetadata {
    enum class Orientation {
        Unspecified = 0,
        Normal = 1,
        MirrorHorizontal = 2,
        Rotate180 = 3,
        MirrorVertical = 4,
        MirrorHorizontalRotate270 = 5,
        Rotate90 = 6,
        MirrorHorizontalRotate90 = 7,
        Rotate270 = 8
    };

    QString path;
    QString fileName;
    QString format;
    qint64 fileSize = 0;
    QSize sourceSize;
    QDateTime modifiedAt;
    QString decoderName;
    QString decoderVersion;
    QString metadataReaderName;
    QString metadataReaderVersion;
    QString metadataWarning;
    Orientation sourceOrientation = Orientation::Unspecified;
    bool gpsMetadataPresent = false;

    struct Camera {
        QString make;
        QString model;
        QString software;
        QString lens;
        QDateTime capturedAt;
        double exposureSeconds = 0.0;
        double aperture = 0.0;
        double focalLengthMm = 0.0;
        int iso = 0;
        QString exposureProgram;
        QString meteringMode;
        QString exposureCompensation;
        QString flash;
        QString gps;
        QSize sensorSize;
    };
    std::optional<Camera> camera;

    struct Descriptive {
        QString title;
        QString description;
        QString creator;
        QString copyright;
    };
    std::optional<Descriptive> descriptive;

    struct ColorProfile {
        QString sourceDescription;
        QString sourceFingerprint;
        QString destinationColorSpace;
        QString transformEngine;
        QString renderingIntent;
        bool converted = false;
    };
    std::optional<ColorProfile> colorProfile;
    QString colorWarning;
};

using ImageStorage = std::variant<QImage, std::shared_ptr<const PlaneBufferSet>>;

struct ImageFrame {
    ImageDescriptor descriptor;
    ImageMetadata metadata;
    ImageStorage storage;
    std::optional<RawImageParameters> rawParameters;
    // Set by decoders when the frame is a bounded proxy whose exact source samples (RAW/YUV
    // planes, camera RAW mosaic) only arrive with a full decode.
    bool sourceSamplesPending = false;

    [[nodiscard]] const QImage* qImage() const {
        if (const auto* image = std::get_if<QImage>(&storage)) {
            return image;
        }
        const auto* planes = std::get_if<std::shared_ptr<const PlaneBufferSet>>(&storage);
        return planes && *planes && !(*planes)->displayImage.isNull() ? &(*planes)->displayImage
                                                                      : nullptr;
    }

    [[nodiscard]] qsizetype byteSize() const {
        if (const auto* image = std::get_if<QImage>(&storage)) {
            return image->sizeInBytes();
        }
        const auto* planes = std::get_if<std::shared_ptr<const PlaneBufferSet>>(&storage);
        if (!planes || !*planes) {
            return 0;
        }
        return (*planes)->storage.size() + (*planes)->displayImage.sizeInBytes();
    }
};

using ImageFramePtr = std::shared_ptr<const ImageFrame>;

} // namespace ispview
