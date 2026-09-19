#include "io/raw_image_decoder.h"

#include "core/color_conversion.h"
#include "core/raw_plane_access.h"

#include <QFile>
#include <QFileInfo>
#include <QColorSpace>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace ispview {
namespace {

// Full frames keep source planes for GPU display and exact probes. The CPU image is an
// emergency fallback, so bounding it avoids repeating a full-resolution demosaic before
// the plane texture can be submitted.
constexpr QSize kFullFallbackMaximumSize{960, 720};

int toByte(double value) {
    return std::clamp(static_cast<int>(std::lround(value * 255.0)), 0, 255);
}

quint16 read16(const char* bytes, bool littleEndian) {
    const auto* source = reinterpret_cast<const uchar*>(bytes);
    return littleEndian ? qFromLittleEndian<quint16>(source) : qFromBigEndian<quint16>(source);
}

bool checkedFrameOffset(const RawImageParameters& parameters, qsizetype frameSize,
                        qsizetype fileSize, qsizetype& offset) {
    if (frameSize <= 0 || parameters.headerOffset < 0 || parameters.frameIndex < 0 ||
        parameters.frameIndex >
            (std::numeric_limits<qsizetype>::max() - parameters.headerOffset) / frameSize) {
        return false;
    }
    offset = parameters.headerOffset + static_cast<qsizetype>(parameters.frameIndex) * frameSize;
    return offset <= fileSize && frameSize <= fileSize - offset;
}

qsizetype rowStride(const RawImageParameters& parameters) {
    return parameters.rowStride > 0 ? parameters.rowStride : minimumRowStride(parameters);
}

qsizetype chromaStride(const RawImageParameters& parameters) {
    if (parameters.chromaStride > 0) {
        return parameters.chromaStride;
    }
    return minimumChromaRowStride(parameters);
}

QImage convertYuv(const QByteArray& bytes, const RawImageParameters& parameters,
                  const QSize& outputSize) {
    const int width = parameters.size.width();
    const int height = parameters.size.height();
    const qsizetype yStride = rowStride(parameters);
    const qsizetype uvStride = chromaStride(parameters);
    const qsizetype yBytes = yStride * height;
    const qsizetype chromaPlaneBytes = uvStride * ((height + 1) / 2);
    const auto matrix = yuvCoefficients(parameters.yuvMatrix);
    const int bits = parameters.format == RawPixelFormat::P010 ? 10 : 8;
    const double maximum = static_cast<double>((1 << bits) - 1);
    const double yOffset =
        parameters.range == QuantizationRange::Limited ? 16.0 * (1 << (bits - 8)) : 0.0;
    const double yScale =
        parameters.range == QuantizationRange::Limited ? 219.0 * (1 << (bits - 8)) : maximum;
    const double cCenter = static_cast<double>(1 << (bits - 1));
    const double cScale =
        parameters.range == QuantizationRange::Limited ? 224.0 * (1 << (bits - 8)) : maximum;

    auto sample = [&](qsizetype offset) -> int {
        if (bits == 8) {
            return static_cast<uchar>(bytes.at(offset));
        }
        quint16 value = read16(bytes.constData() + offset, parameters.littleEndian);
        return parameters.msbAligned ? value >> 6 : value & 0x03FF;
    };

    const qsizetype sampleBytes = bits == 8 ? 1 : 2;
    const int chromaWidth = (width + 1) / 2;
    const int chromaHeight = (height + 1) / 2;
    auto chromaAt = [&](int chromaX, int chromaY) {
        chromaX = std::clamp(chromaX, 0, chromaWidth - 1);
        chromaY = std::clamp(chromaY, 0, chromaHeight - 1);
        int uValue = 0;
        int vValue = 0;
        if (parameters.format == RawPixelFormat::I420) {
            uValue = sample(yBytes + chromaY * uvStride + chromaX);
            vValue = sample(yBytes + chromaPlaneBytes + chromaY * uvStride + chromaX);
        } else {
            const qsizetype pair =
                yBytes + chromaY * uvStride + chromaX * sampleBytes * 2;
            const int first = sample(pair);
            const int second = sample(pair + sampleBytes);
            const bool vu = parameters.format == RawPixelFormat::NV21;
            uValue = vu ? second : first;
            vValue = vu ? first : second;
        }
        return std::array<double, 2>{static_cast<double>(uValue),
                                     static_cast<double>(vValue)};
    };
    auto rgbAt = [&](int x, int y) {
        const qsizetype yOffsetBytes = y * yStride + x * sampleBytes;
        const int yValue = sample(yOffsetBytes);
        const double chromaX = std::clamp(
            parameters.chromaLocation == ChromaLocation::Left
                ? static_cast<double>(x) * chromaWidth / width
                : (x + 0.5) * chromaWidth / width - 0.5,
            0.0, static_cast<double>(chromaWidth - 1));
        const double chromaY = std::clamp(
            (y + 0.5) * chromaHeight / height - 0.5, 0.0,
            static_cast<double>(chromaHeight - 1));
        const int x0 = static_cast<int>(std::floor(chromaX));
        const int y0 = static_cast<int>(std::floor(chromaY));
        const int x1 = std::min(x0 + 1, chromaWidth - 1);
        const int y1 = std::min(y0 + 1, chromaHeight - 1);
        const double fx = chromaX - x0;
        const double fy = chromaY - y0;
        const auto topLeft = chromaAt(x0, y0);
        const auto topRight = chromaAt(x1, y0);
        const auto bottomLeft = chromaAt(x0, y1);
        const auto bottomRight = chromaAt(x1, y1);
        std::array<double, 2> chroma{};
        for (std::size_t channel = 0; channel < chroma.size(); ++channel) {
            const double top = topLeft[channel] * (1.0 - fx) + topRight[channel] * fx;
            const double bottom = bottomLeft[channel] * (1.0 - fx) + bottomRight[channel] * fx;
            chroma[channel] = top * (1.0 - fy) + bottom * fy;
        }
        const double luma = (yValue - yOffset) / yScale;
        const double u = (chroma[0] - cCenter) / cScale;
        const double v = (chroma[1] - cCenter) / cScale;
        return yuvRgbToDisplay(
            {luma + matrix.redV * v,
             luma - matrix.greenU * u - matrix.greenV * v,
             luma + matrix.blueU * u},
            parameters);
    };
    auto writePixel = [](uchar* destination, const std::array<double, 3>& rgb) {
        destination[0] = static_cast<uchar>(toByte(rgb[0]));
        destination[1] = static_cast<uchar>(toByte(rgb[1]));
        destination[2] = static_cast<uchar>(toByte(rgb[2]));
        destination[3] = 255;
    };

    QImage image(outputSize, QImage::Format_RGBA8888);
    const bool fullSize = outputSize == parameters.size;
    for (int y = 0; y < outputSize.height(); ++y) {
        auto* destination = image.scanLine(y);
        for (int x = 0; x < outputSize.width(); ++x) {
            if (fullSize) {
                writePixel(destination + x * 4, rgbAt(x, y));
                continue;
            }
            const double sourceX =
                std::clamp((x + 0.5) * width / outputSize.width() - 0.5, 0.0, width - 1.0);
            const double sourceY =
                std::clamp((y + 0.5) * height / outputSize.height() - 0.5, 0.0, height - 1.0);
            const int x0 = static_cast<int>(std::floor(sourceX));
            const int y0 = static_cast<int>(std::floor(sourceY));
            const int x1 = std::min(x0 + 1, width - 1);
            const int y1 = std::min(y0 + 1, height - 1);
            const double fx = sourceX - x0;
            const double fy = sourceY - y0;
            const auto topLeft = rgbAt(x0, y0);
            const auto topRight = rgbAt(x1, y0);
            const auto bottomLeft = rgbAt(x0, y1);
            const auto bottomRight = rgbAt(x1, y1);
            std::array<double, 3> interpolated{};
            for (std::size_t channel = 0; channel < interpolated.size(); ++channel) {
                const double top = topLeft[channel] * (1.0 - fx) + topRight[channel] * fx;
                const double bottom = bottomLeft[channel] * (1.0 - fx) + bottomRight[channel] * fx;
                interpolated[channel] = top * (1.0 - fy) + bottom * fy;
            }
            writePixel(destination + x * 4, interpolated);
        }
    }
    image.setColorSpace(displayQColorSpace(currentDisplayColorSpace()));
    return image;
}

} // namespace

QString RawImageDecoder::cacheIdentity() const {
    return QStringLiteral("headerless-raw-v5|display-%1")
        .arg(displayColorSpaceKey(currentDisplayColorSpace()));
}

bool RawImageDecoder::canDecode(const QString& path) const {
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QStringLiteral("yuv") || suffix == QStringLiteral("raw");
}

DecodeResult RawImageDecoder::decode(const DecodeRequest& request) const {
    if (!request.rawParameters) {
        return {{}, QStringLiteral("RAW/YUV parameters are required")};
    }
    const RawImageParameters& parameters = *request.rawParameters;
    if (!parameters.hasValidBitLayout()) {
        return {{},
                QStringLiteral("Valid-bit override is supported only for RAW16 and must be "
                               "between 1 and 16 (or 0 for the format default)")};
    }
    const int formatMaximum = parameters.maximumSampleValue();
    if (parameters.blackLevel < 0 || parameters.blackLevel >= formatMaximum ||
        (parameters.whiteLevel > 0 && (parameters.whiteLevel <= parameters.blackLevel ||
                                       parameters.whiteLevel > formatMaximum))) {
        return {{}, QStringLiteral("Black/white levels must fit the effective sample bit depth")};
    }
    if (!parameters.hasValidDisplayTransform() || !parameters.hasValidBayerSampling() ||
        !parameters.hasValidYuvColorDescription()) {
        return {{}, QStringLiteral("White balance, CCM, or display gamma is invalid")};
    }
    if (!parameters.hasValidOrientation()) {
        return {{}, QStringLiteral("RAW/YUV orientation is invalid")};
    }
    const qsizetype frameSize = frameByteSize(parameters);
    QFile file(request.path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {{}, file.errorString()};
    }
    qsizetype offset = 0;
    if (!checkedFrameOffset(parameters, frameSize, file.size(), offset) || !file.seek(offset)) {
        return {{}, QStringLiteral("RAW/YUV parameters exceed the file bounds")};
    }
    QByteArray bytes = file.read(frameSize);
    if (bytes.size() != frameSize) {
        return {{}, QStringLiteral("Could not read a complete RAW/YUV frame")};
    }

    const QSize logicalDisplaySize = orientedImageSize(parameters.size, parameters.orientation);
    QSize outputSize = logicalDisplaySize;
    const QSize maximumSize =
        request.purpose == DecodePurpose::Full ? kFullFallbackMaximumSize : request.maximumSize;
    if (!maximumSize.isEmpty() &&
        (outputSize.width() > maximumSize.width() || outputSize.height() > maximumSize.height())) {
        outputSize.scale(maximumSize, Qt::KeepAspectRatio);
    }
    const QSize sourceOutputSize = orientedImageSize(outputSize, parameters.orientation);
    QImage display = parameters.isYuv() ? convertYuv(bytes, parameters, sourceOutputSize)
                                        : renderBayerImage(bytes, parameters, sourceOutputSize);
    display = orientedImage(std::move(display), parameters.orientation);
    if (display.isNull()) {
        return {{}, QStringLiteral("RAW/YUV conversion failed")};
    }
    const QFileInfo info(request.path);
    auto frame = std::make_shared<ImageFrame>();
    frame->descriptor.size =
        request.purpose == DecodePurpose::Full ? logicalDisplaySize : display.size();
    frame->descriptor.layout = parameters.isYuv() ? PixelLayout::SemiPlanar : PixelLayout::Bayer;
    if (parameters.format == RawPixelFormat::I420) {
        frame->descriptor.layout = PixelLayout::Planar;
    }
    frame->descriptor.channelOrder = parameters.isYuv() ? ChannelOrder::YUV : ChannelOrder::Bayer;
    frame->descriptor.storageBits =
        parameters.format == RawPixelFormat::P010 || parameters.format == RawPixelFormat::Raw16
            ? 16
            : parameters.validBits();
    frame->descriptor.validBits = parameters.validBits();
    if (parameters.isYuv()) {
        frame->descriptor.sourceColor.colorSpace = QStringLiteral("YUV");
        frame->descriptor.sourceColor.primaries = yuvPrimariesName(parameters.yuvPrimaries);
        frame->descriptor.sourceColor.transferFunction = yuvTransferName(parameters.yuvTransfer);
        frame->descriptor.sourceColor.matrixCoefficients = yuvMatrixName(parameters.yuvMatrix);
        frame->descriptor.sourceColor.chromaLocation = chromaLocationName(parameters.chromaLocation);
        frame->descriptor.sourceColor.fullRange = parameters.range == QuantizationRange::Full;
        applyDisplayColor(frame->descriptor.displayColor);
        frame->descriptor.displayColor.matrixCoefficients = QStringLiteral("RGB");
    } else if (parameters.demosaic) {
        frame->descriptor.sourceColor.colorSpace = QStringLiteral("CFA sensor samples");
        frame->descriptor.sourceColor.primaries = QStringLiteral("Sensor CFA");
        frame->descriptor.sourceColor.transferFunction = QStringLiteral("Linear sensor values");
        frame->descriptor.sourceColor.matrixCoefficients = QStringLiteral("None");
        // The developed mosaic is a user-defined transform (CCM contract plus the user's display
        // gamma), so it deliberately does not claim to be the application display space.
        frame->descriptor.displayColor.colorSpace = QStringLiteral("User-defined RGB");
        frame->descriptor.displayColor.primaries = QStringLiteral("sRGB / BT.709 (CCM contract)");
        frame->descriptor.displayColor.transferFunction =
            QStringLiteral("Power %1").arg(parameters.displayGamma, 0, 'g', 6);
        frame->descriptor.displayColor.matrixCoefficients = QStringLiteral("RGB");
    } else {
        frame->descriptor.sourceColor.colorSpace = QStringLiteral("CFA sensor samples");
        frame->descriptor.sourceColor.primaries = QStringLiteral("Sensor CFA");
        frame->descriptor.sourceColor.transferFunction = QStringLiteral("Linear sensor values");
        frame->descriptor.sourceColor.matrixCoefficients = QStringLiteral("None");
        applyDisplayColor(frame->descriptor.displayColor, true);
        frame->descriptor.displayColor.matrixCoefficients =
            QStringLiteral("CFA false-colour channel mapping");
    }
    frame->metadata.path = info.absoluteFilePath();
    frame->metadata.fileName = info.fileName();
    frame->metadata.format = rawPixelFormatName(parameters.format);
    frame->metadata.fileSize = info.size();
    frame->metadata.modifiedAt = info.lastModified();
    frame->metadata.decoderName = QStringLiteral("ISPView RAW/YUV");
    frame->metadata.sourceSize = logicalDisplaySize;
    frame->rawParameters = parameters;
    // Only a full decode keeps the source planes the exact probes read.
    frame->sourceSamplesPending = request.purpose != DecodePurpose::Full;
    if (request.purpose != DecodePurpose::Full) {
        frame->storage = std::move(display);
    } else {
        auto planes = std::make_shared<PlaneBufferSet>();
        planes->storage = std::move(bytes);
        const qsizetype primaryStride = rowStride(parameters);
        const qsizetype primaryBytes = primaryStride * parameters.size.height();
        planes->planes.push_back({0, primaryStride, primaryBytes});
        if (parameters.isYuv()) {
            const qsizetype secondaryStride = chromaStride(parameters);
            const qsizetype secondaryBytes = secondaryStride * ((parameters.size.height() + 1) / 2);
            planes->planes.push_back({primaryBytes, secondaryStride, secondaryBytes});
            if (parameters.format == RawPixelFormat::I420) {
                planes->planes.push_back(
                    {primaryBytes + secondaryBytes, secondaryStride, secondaryBytes});
            }
        }
        planes->displayImage = std::move(display);
        frame->storage = std::shared_ptr<const PlaneBufferSet>(std::move(planes));
    }
    return {std::move(frame), {}};
}

std::optional<quint16> RawImageDecoder::bayerValueAt(const ImageFrame& frame, int x, int y) {
    const auto sample = RawPlaneAccessor(frame).bayerAtSourcePixel({x, y});
    return sample ? std::optional<quint16>(sample->value) : std::nullopt;
}

QString RawImageDecoder::pixelDescription(const ImageFrame& frame, int x, int y) {
    return RawPlaneAccessor(frame).pixelDescriptionAtDisplayPixel({x, y});
}

} // namespace ispview
