#pragma once

#include <QImage>
#include <QPoint>
#include <QSize>
#include <QString>

#include <array>

namespace ispview {

enum class RawPixelFormat { NV12, NV21, I420, P010, MipiRaw10, MipiRaw12, Raw16 };
enum class BayerPattern { RGGB, GRBG, GBRG, BGGR };
enum class YuvMatrix { BT601, BT709, BT2020 };
enum class QuantizationRange { Full, Limited };
enum class ImageOrientation { Normal, Rotate90Clockwise, Rotate180, Rotate270Clockwise };

struct RawImageParameters {
    QSize size;
    RawPixelFormat format = RawPixelFormat::NV12;
    qsizetype headerOffset = 0;
    qsizetype rowStride = 0;
    qsizetype chromaStride = 0;
    int frameIndex = 0;
    bool littleEndian = true;
    bool msbAligned = false;
    // Zero selects the pixel-format default. A non-zero override is valid only for
    // Raw16 containers, where the sensor samples may use fewer than 16 bits.
    int validBitsOverride = 0;
    BayerPattern bayerPattern = BayerPattern::RGGB;
    YuvMatrix yuvMatrix = YuvMatrix::BT709;
    QuantizationRange range = QuantizationRange::Limited;
    ImageOrientation orientation = ImageOrientation::Normal;
    int blackLevel = 0;
    int whiteLevel = 0;
    bool demosaic = false;
    std::array<double, 3> whiteBalanceGains{1.0, 1.0, 1.0};
    std::array<double, 9> colorCorrectionMatrix{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
    double displayGamma = 2.2;

    [[nodiscard]] bool isYuv() const;
    [[nodiscard]] int validBits() const;
    [[nodiscard]] bool hasValidBitLayout() const;
    [[nodiscard]] int maximumSampleValue() const;
    [[nodiscard]] bool hasValidDisplayTransform() const;
    [[nodiscard]] bool hasValidOrientation() const;
    [[nodiscard]] QString cacheKey() const;
};

[[nodiscard]] QString rawPixelFormatName(RawPixelFormat format);
[[nodiscard]] QString bayerPatternName(BayerPattern pattern);
[[nodiscard]] QString yuvMatrixName(YuvMatrix matrix);
// Shifts a Bayer pattern by (dx, dy) positions, which is what cropping a mosaic off its original
// origin does to the colour phase.
[[nodiscard]] BayerPattern shiftedBayerPattern(BayerPattern pattern, int dx, int dy);
// Copies a 16-bit mosaic out of a strided sensor array into a tightly packed RAW16 plane,
// clamping to the source bounds and masking every sample to the valid-bit range. Returns an
// empty array when the geometry does not describe a readable region.
[[nodiscard]] QByteArray packedMosaicPlane(const quint16* samples, qsizetype strideInSamples,
                                           const QSize& sampleSize, const QRect& crop,
                                           bool littleEndian, int validBits);
// Renders a packed RAW16 Bayer plane as the un-demosaiced grey mosaic the viewer shows when
// demosaicing is off: black level removal, white-level normalization, and display gamma, using
// the same transform as the headerless RAW path. Samples are masked to the valid-bit range, and
// an output size other than parameters.size samples the mosaic with nearest neighbours.
[[nodiscard]] QImage grayMosaicImage(const QByteArray& plane,
                                     const RawImageParameters& parameters,
                                     const QSize& outputSize = {});
[[nodiscard]] qsizetype minimumRowStride(const RawImageParameters& parameters);
[[nodiscard]] qsizetype minimumChromaRowStride(const RawImageParameters& parameters);
[[nodiscard]] qsizetype frameByteSize(const RawImageParameters& parameters);
[[nodiscard]] qsizetype estimatedFullFrameBytes(const RawImageParameters& parameters);
[[nodiscard]] int availableFrameCount(qint64 fileSize, const RawImageParameters& parameters);
[[nodiscard]] QSize orientedImageSize(const QSize& sourceSize, ImageOrientation orientation);
// Rotates a decoded image into the orientation its parameters describe. The mapping matches
// displayToSourcePixel so plane reads and rendered pixels stay aligned.
[[nodiscard]] QImage orientedImage(QImage source, ImageOrientation orientation);
[[nodiscard]] QPoint displayToSourcePixel(const QPoint& displayPixel, const QSize& sourceSize,
                                          ImageOrientation orientation);

} // namespace ispview
