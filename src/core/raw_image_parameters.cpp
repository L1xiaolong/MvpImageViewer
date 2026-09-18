#include "core/raw_image_parameters.h"

#include <QtEndian>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace ispview {
namespace {

qsizetype checkedMultiply(qsizetype left, qsizetype right) {
    if (left < 0 || right < 0 ||
        (left != 0 && right > std::numeric_limits<qsizetype>::max() / left)) {
        return -1;
    }
    return left * right;
}

qsizetype effectiveStride(qsizetype configured, qsizetype minimum) {
    if (minimum < 0 || (configured > 0 && configured < minimum)) {
        return -1;
    }
    return configured > 0 ? configured : minimum;
}

int cfaDisplayChannel(BayerPattern pattern, BayerSampling sampling, int x, int y) {
    const int blockSize = bayerSampleBlockSize(sampling);
    const bool evenX = ((x / blockSize) & 1) == 0;
    const bool evenY = ((y / blockSize) & 1) == 0;
    switch (pattern) {
    case BayerPattern::RGGB:
        return evenY ? (evenX ? 0 : 1) : (evenX ? 1 : 2);
    case BayerPattern::GRBG:
        return evenY ? (evenX ? 1 : 0) : (evenX ? 2 : 1);
    case BayerPattern::GBRG:
        return evenY ? (evenX ? 1 : 2) : (evenX ? 0 : 1);
    case BayerPattern::BGGR:
        return evenY ? (evenX ? 2 : 1) : (evenX ? 1 : 0);
    }
    return 1;
}

} // namespace

BayerPattern shiftedBayerPattern(BayerPattern pattern, int dx, int dy) {
    // The four phases are RGGB with optional x/y inversions, and a shift toggles the inversion
    // of the axis it moves along.
    bool invertX = (((dx % 2) + 2) % 2) == 1;
    bool invertY = (((dy % 2) + 2) % 2) == 1;
    switch (pattern) {
    case BayerPattern::RGGB:
        break;
    case BayerPattern::GRBG:
        invertX = !invertX;
        break;
    case BayerPattern::GBRG:
        invertY = !invertY;
        break;
    case BayerPattern::BGGR:
        invertX = !invertX;
        invertY = !invertY;
        break;
    }
    if (invertX && invertY) {
        return BayerPattern::BGGR;
    }
    if (invertX) {
        return BayerPattern::GRBG;
    }
    if (invertY) {
        return BayerPattern::GBRG;
    }
    return BayerPattern::RGGB;
}

QByteArray packedMosaicPlane(const quint16* samples, qsizetype strideInSamples,
                             const QSize& sampleSize, const QRect& crop, bool littleEndian,
                             int validBits) {
    if (!samples || strideInSamples <= 0 || validBits <= 0 || validBits > 16 ||
        crop.x() < 0 || crop.y() < 0 || crop.isEmpty() ||
        crop.x() + crop.width() > sampleSize.width() ||
        crop.y() + crop.height() > sampleSize.height() ||
        strideInSamples < crop.x() + crop.width()) {
        return {};
    }
    const quint16 mask =
        static_cast<quint16>(validBits >= 16 ? 0xFFFFU : ((1U << validBits) - 1U));
    const qsizetype rowBytes = static_cast<qsizetype>(crop.width()) * 2;
    QByteArray plane(rowBytes * crop.height(), Qt::Uninitialized);
    auto* destination = reinterpret_cast<uchar*>(plane.data());
    for (int row = 0; row < crop.height(); ++row) {
        const quint16* sourceRow =
            samples + static_cast<qsizetype>(crop.y() + row) * strideInSamples + crop.x();
        auto* destinationRow = destination + static_cast<qsizetype>(row) * rowBytes;
        for (int column = 0; column < crop.width(); ++column) {
            const quint16 value = static_cast<quint16>(sourceRow[column] & mask);
            uchar* bytes = destinationRow + static_cast<qsizetype>(column) * 2;
            if (littleEndian) {
                qToLittleEndian<quint16>(value, bytes);
            } else {
                qToBigEndian<quint16>(value, bytes);
            }
        }
    }
    return plane;
}

QImage cfaMosaicImage(const QByteArray& plane, const RawImageParameters& parameters,
                      const QSize& outputSize) {
    const QSize sourceSize = parameters.size;
    const qsizetype rowBytes = static_cast<qsizetype>(sourceSize.width()) * 2;
    if (sourceSize.isEmpty() || plane.size() < rowBytes * sourceSize.height() ||
        parameters.maximumSampleValue() <= 0) {
        return {};
    }
    const QSize target = outputSize.isEmpty() ? sourceSize : outputSize;
    if (target.isEmpty()) {
        return {};
    }
    const int maximum = parameters.maximumSampleValue();
    const quint16 mask = static_cast<quint16>(parameters.validBits() >= 16
                                                  ? 0xFFFFU
                                                  : ((1U << parameters.validBits()) - 1U));
    const auto sampleAt = [&](int x, int y) {
        const auto* bytes = reinterpret_cast<const uchar*>(plane.constData()) +
                            static_cast<qsizetype>(y) * rowBytes + static_cast<qsizetype>(x) * 2;
        const quint16 stored = parameters.littleEndian ? qFromLittleEndian<quint16>(bytes)
                                                       : qFromBigEndian<quint16>(bytes);
        return static_cast<int>(stored & mask);
    };
    QImage image(target, QImage::Format_RGBA8888);
    if (image.isNull()) {
        return {};
    }
    const bool fullSize = target == sourceSize;
    for (int y = 0; y < target.height(); ++y) {
        auto* destination = image.scanLine(y);
        for (int x = 0; x < target.width(); ++x) {
            const int sourceX =
                fullSize ? x
                         : std::clamp(static_cast<int>((x + 0.5) * sourceSize.width() /
                                                       target.width()),
                                      0, sourceSize.width() - 1);
            const int sourceY =
                fullSize ? y
                         : std::clamp(static_cast<int>((y + 0.5) * sourceSize.height() /
                                                       target.height()),
                                      0, sourceSize.height() - 1);
            const double normalized = sampleAt(sourceX, sourceY) / static_cast<double>(maximum);
            const auto encoded = static_cast<uchar>(std::clamp(
                static_cast<int>(std::lround(normalized * 255.0)),
                0, 255));
            const int channel = cfaDisplayChannel(parameters.bayerPattern,
                                                  parameters.bayerSampling, sourceX, sourceY);
            destination[x * 4 + 0] = channel == 0 ? encoded : 0;
            destination[x * 4 + 1] = channel == 1 ? encoded : 0;
            destination[x * 4 + 2] = channel == 2 ? encoded : 0;
            destination[x * 4 + 3] = 255;
        }
    }
    return image;
}

bool RawImageParameters::isYuv() const {
    return format == RawPixelFormat::NV12 || format == RawPixelFormat::NV21 ||
           format == RawPixelFormat::I420 || format == RawPixelFormat::P010;
}

int RawImageParameters::validBits() const {
    switch (format) {
    case RawPixelFormat::P010:
    case RawPixelFormat::MipiRaw10:
        return 10;
    case RawPixelFormat::MipiRaw12:
        return 12;
    case RawPixelFormat::Raw16:
        return validBitsOverride > 0 ? validBitsOverride : 16;
    default:
        return 8;
    }
}

bool RawImageParameters::hasValidBitLayout() const {
    if (format != RawPixelFormat::Raw16) {
        return validBitsOverride == 0;
    }
    return validBitsOverride >= 0 && validBitsOverride <= 16;
}

int RawImageParameters::maximumSampleValue() const { return (1 << validBits()) - 1; }

bool RawImageParameters::hasValidDisplayTransform() const {
    const bool validGains =
        std::all_of(whiteBalanceGains.cbegin(), whiteBalanceGains.cend(),
                    [](double gain) { return std::isfinite(gain) && gain > 0.0 && gain <= 64.0; });
    const bool validMatrix = std::all_of(
        colorCorrectionMatrix.cbegin(), colorCorrectionMatrix.cend(), [](double coefficient) {
            return std::isfinite(coefficient) && std::abs(coefficient) <= 64.0;
        });
    return validGains && validMatrix && std::isfinite(displayGamma) && displayGamma >= 0.1 &&
           displayGamma <= 10.0;
}

bool RawImageParameters::hasValidOrientation() const {
    const int value = static_cast<int>(orientation);
    return value >= static_cast<int>(ImageOrientation::Normal) &&
           value <= static_cast<int>(ImageOrientation::Rotate270Clockwise);
}

bool RawImageParameters::hasValidBayerSampling() const {
    const int value = static_cast<int>(bayerSampling);
    return value >= static_cast<int>(BayerSampling::Standard2x2) &&
           value <= static_cast<int>(BayerSampling::QuadBayer4x4);
}

QString RawImageParameters::cacheKey() const {
    QString result = QStringLiteral("%1x%2|%3|%4|%5|%6|%7|%8|%9|%10|%11|%12|%13|%14|%15|%16")
                         .arg(size.width())
                         .arg(size.height())
                         .arg(static_cast<int>(format))
                         .arg(headerOffset)
                         .arg(rowStride)
                         .arg(chromaStride)
                         .arg(frameIndex)
                         .arg(littleEndian)
                         .arg(msbAligned)
                         .arg(validBitsOverride)
                         .arg(static_cast<int>(bayerPattern))
                         .arg(static_cast<int>(yuvMatrix))
                         .arg(static_cast<int>(range))
                         .arg(blackLevel)
                         .arg(whiteLevel)
                         .arg(static_cast<int>(orientation));
    if (isYuv()) {
        return result;
    }
    result += QLatin1Char('|') + QString::number(static_cast<int>(bayerSampling));
    result += QLatin1Char('|') + QString::number(demosaic);
    for (double gain : whiteBalanceGains) {
        result += QLatin1Char('|') + QString::number(gain, 'g', 17);
    }
    for (double coefficient : colorCorrectionMatrix) {
        result += QLatin1Char('|') + QString::number(coefficient, 'g', 17);
    }
    result += QLatin1Char('|') + QString::number(displayGamma, 'g', 17);
    return result;
}

QString rawPixelFormatName(RawPixelFormat format) {
    switch (format) {
    case RawPixelFormat::NV12:
        return QStringLiteral("NV12");
    case RawPixelFormat::NV21:
        return QStringLiteral("NV21");
    case RawPixelFormat::I420:
        return QStringLiteral("I420");
    case RawPixelFormat::P010:
        return QStringLiteral("P010");
    case RawPixelFormat::MipiRaw10:
        return QStringLiteral("MIPI RAW10");
    case RawPixelFormat::MipiRaw12:
        return QStringLiteral("MIPI RAW12");
    case RawPixelFormat::Raw16:
        return QStringLiteral("RAW16");
    }
    return {};
}

QString bayerPatternName(BayerPattern pattern) {
    switch (pattern) {
    case BayerPattern::RGGB:
        return QStringLiteral("RGGB");
    case BayerPattern::GRBG:
        return QStringLiteral("GRBG");
    case BayerPattern::GBRG:
        return QStringLiteral("GBRG");
    case BayerPattern::BGGR:
        return QStringLiteral("BGGR");
    }
    return {};
}

QString bayerSamplingName(BayerSampling sampling) {
    switch (sampling) {
    case BayerSampling::Standard2x2:
        return QStringLiteral("2x2 Bayer");
    case BayerSampling::QuadBayer4x4:
        return QStringLiteral("4x4 Quad Bayer");
    }
    return QStringLiteral("Unknown");
}

int bayerSampleBlockSize(BayerSampling sampling) {
    return sampling == BayerSampling::QuadBayer4x4 ? 2 : 1;
}

QString yuvMatrixName(YuvMatrix matrix) {
    switch (matrix) {
    case YuvMatrix::BT601:
        return QStringLiteral("BT.601");
    case YuvMatrix::BT709:
        return QStringLiteral("BT.709");
    case YuvMatrix::BT2020:
        return QStringLiteral("BT.2020");
    }
    return {};
}

qsizetype minimumRowStride(const RawImageParameters& parameters) {
    const qsizetype width = parameters.size.width();
    switch (parameters.format) {
    case RawPixelFormat::NV12:
    case RawPixelFormat::NV21:
    case RawPixelFormat::I420:
        return width;
    case RawPixelFormat::P010:
    case RawPixelFormat::Raw16:
        return checkedMultiply(width, 2);
    case RawPixelFormat::MipiRaw10:
        return checkedMultiply((width + 3) / 4, 5);
    case RawPixelFormat::MipiRaw12:
        return checkedMultiply((width + 1) / 2, 3);
    }
    return -1;
}

qsizetype minimumChromaRowStride(const RawImageParameters& parameters) {
    const qsizetype chromaWidth = (static_cast<qsizetype>(parameters.size.width()) + 1) / 2;
    switch (parameters.format) {
    case RawPixelFormat::NV12:
    case RawPixelFormat::NV21:
        return checkedMultiply(chromaWidth, 2);
    case RawPixelFormat::I420:
        return chromaWidth;
    case RawPixelFormat::P010:
        return checkedMultiply(chromaWidth, 4);
    default:
        return 0;
    }
}

qsizetype frameByteSize(const RawImageParameters& parameters) {
    if (parameters.size.isEmpty()) {
        return -1;
    }
    const qsizetype height = parameters.size.height();
    const qsizetype rowStride = effectiveStride(parameters.rowStride, minimumRowStride(parameters));
    if (rowStride < 0) {
        return -1;
    }
    const qsizetype primary = checkedMultiply(rowStride, height);
    if (primary < 0 || !parameters.isYuv()) {
        return primary;
    }

    const qsizetype chromaHeight = (height + 1) / 2;
    const qsizetype minimumChromaStride = minimumChromaRowStride(parameters);
    int planeCount = 1;
    if (parameters.format == RawPixelFormat::I420) {
        planeCount = 2;
    }
    const qsizetype chromaStride = effectiveStride(parameters.chromaStride, minimumChromaStride);
    if (chromaStride < 0) {
        return -1;
    }
    const qsizetype chroma = checkedMultiply(chromaStride, chromaHeight);
    if (chroma < 0 || chroma > std::numeric_limits<qsizetype>::max() / planeCount ||
        primary > std::numeric_limits<qsizetype>::max() - chroma * planeCount) {
        return -1;
    }
    return primary + chroma * planeCount;
}

qsizetype estimatedFullFrameBytes(const RawImageParameters& parameters) {
    const qsizetype sourceBytes = frameByteSize(parameters);
    const qsizetype pixels = checkedMultiply(parameters.size.width(), parameters.size.height());
    const qsizetype displayBytes = checkedMultiply(pixels, 4);
    if (sourceBytes <= 0 || displayBytes < 0 ||
        sourceBytes > std::numeric_limits<qsizetype>::max() - displayBytes) {
        return -1;
    }
    return sourceBytes + displayBytes;
}

int availableFrameCount(qint64 fileSize, const RawImageParameters& parameters) {
    const qsizetype frameSize = frameByteSize(parameters);
    if (frameSize <= 0 || parameters.headerOffset < 0 || fileSize < parameters.headerOffset) {
        return 0;
    }
    const qint64 count = (fileSize - parameters.headerOffset) / frameSize;
    return static_cast<int>(std::min<qint64>(count, std::numeric_limits<int>::max()));
}

QSize orientedImageSize(const QSize& sourceSize, ImageOrientation orientation) {
    if (orientation == ImageOrientation::Rotate90Clockwise ||
        orientation == ImageOrientation::Rotate270Clockwise) {
        return {sourceSize.height(), sourceSize.width()};
    }
    return sourceSize;
}

QPoint displayToSourcePixel(const QPoint& displayPixel, const QSize& sourceSize,
                            ImageOrientation orientation) {
    switch (orientation) {
    case ImageOrientation::Normal:
        return displayPixel;
    case ImageOrientation::Rotate90Clockwise:
        return {displayPixel.y(), sourceSize.height() - 1 - displayPixel.x()};
    case ImageOrientation::Rotate180:
        return {sourceSize.width() - 1 - displayPixel.x(),
                sourceSize.height() - 1 - displayPixel.y()};
    case ImageOrientation::Rotate270Clockwise:
        return {sourceSize.width() - 1 - displayPixel.y(), displayPixel.x()};
    }
    return displayPixel;
}

QImage orientedImage(QImage source, ImageOrientation orientation) {
    if (orientation == ImageOrientation::Normal || source.isNull()) {
        return source;
    }
    QImage oriented(orientedImageSize(source.size(), orientation), source.format());
    if (oriented.isNull()) {
        return source;
    }
    for (int y = 0; y < oriented.height(); ++y) {
        for (int x = 0; x < oriented.width(); ++x) {
            const QPoint sourcePixel = displayToSourcePixel({x, y}, source.size(), orientation);
            oriented.setPixel(x, y, source.pixel(sourcePixel));
        }
    }
    return oriented;
}

} // namespace ispview
