#include "core/display_histogram.h"

#include "core/raw_plane_access.h"
#include <QColorSpace>

#include <algorithm>
#include <array>
#include <cmath>

namespace ispview {
namespace {

struct ChannelAccumulator {
    HistogramChannel channel;
    quint64 sum = 0;
    quint64 squaredSum = 0;
};

ChannelAccumulator makeAccumulator(int maximumValue) {
    ChannelAccumulator result;
    result.channel.bins.resize(maximumValue + 1);
    return result;
}

QPair<int, int> gridSampleCounts(int width, int height, qint64 maximumSamples) {
    if (width <= 0 || height <= 0 || maximumSamples <= 0) {
        return {0, 0};
    }
    const qint64 available = static_cast<qint64>(width) * height;
    if (available <= maximumSamples) {
        return {width, height};
    }

    const int maximumRows = static_cast<int>(std::min<qint64>(height, maximumSamples));
    const double idealRows =
        std::sqrt(static_cast<double>(maximumSamples) * height / width);
    const int rows = std::clamp(static_cast<int>(std::floor(idealRows)), 1, maximumRows);
    const int columns = static_cast<int>(
        std::clamp(maximumSamples / rows, qint64{1}, static_cast<qint64>(width)));
    return {columns, rows};
}

void addSample(ChannelAccumulator& accumulator, int value) {
    ++accumulator.channel.bins[value];
    accumulator.sum += static_cast<quint64>(value);
    accumulator.squaredSum += static_cast<quint64>(value) * static_cast<quint64>(value);
}

void finishChannel(ChannelAccumulator& accumulator, qint64 sampleCount) {
    const auto first = std::find_if(accumulator.channel.bins.cbegin(),
                                    accumulator.channel.bins.cend(),
                                    [](quint64 count) { return count != 0; });
    const auto last = std::find_if(accumulator.channel.bins.crbegin(),
                                   accumulator.channel.bins.crend(),
                                   [](quint64 count) { return count != 0; });
    if (first == accumulator.channel.bins.cend() || last == accumulator.channel.bins.crend()) {
        return;
    }
    accumulator.channel.minimum =
        static_cast<int>(std::distance(accumulator.channel.bins.cbegin(), first));
    accumulator.channel.maximum =
        static_cast<int>(accumulator.channel.bins.size()) - 1 -
        static_cast<int>(std::distance(accumulator.channel.bins.crbegin(), last));
    accumulator.channel.mean = static_cast<double>(accumulator.sum) /
                               static_cast<double>(sampleCount);
    const double meanSquare = static_cast<double>(accumulator.squaredSum) /
                              static_cast<double>(sampleCount);
    const double variance =
        std::max(0.0, meanSquare - accumulator.channel.mean * accumulator.channel.mean);
    accumulator.channel.standardDeviation = std::sqrt(variance);
}

struct YuvCoefficients {
    double redV;
    double greenU;
    double greenV;
    double blueU;
};

YuvCoefficients yuvCoefficients(YuvMatrix matrix) {
    switch (matrix) {
    case YuvMatrix::BT601:
        return {1.402, 0.344136, 0.714136, 1.772};
    case YuvMatrix::BT2020:
        return {1.4746, 0.164553, 0.571353, 1.8814};
    case YuvMatrix::BT709:
    default:
        return {1.5748, 0.187324, 0.468124, 1.8556};
    }
}

int toByte(double value) {
    return std::clamp(static_cast<int>(std::lround(value * 255.0)), 0, 255);
}

std::array<double, 2> interpolatedChromaAt(const RawPlaneAccessor& accessor,
                                           const QPoint& sourcePixel) {
    const QSize sourceSize = accessor.sourceSize();
    const QSize chromaSize((sourceSize.width() + 1) / 2, (sourceSize.height() + 1) / 2);
    const double chromaX = std::clamp(
        (sourcePixel.x() + 0.5) * chromaSize.width() / sourceSize.width() - 0.5,
        0.0, static_cast<double>(chromaSize.width() - 1));
    const double chromaY = std::clamp(
        (sourcePixel.y() + 0.5) * chromaSize.height() / sourceSize.height() - 0.5,
        0.0, static_cast<double>(chromaSize.height() - 1));
    const int x0 = static_cast<int>(std::floor(chromaX));
    const int y0 = static_cast<int>(std::floor(chromaY));
    const int x1 = std::min(x0 + 1, chromaSize.width() - 1);
    const int y1 = std::min(y0 + 1, chromaSize.height() - 1);
    const double fx = chromaX - x0;
    const double fy = chromaY - y0;
    const auto sample = [&](int x, int y) {
        const auto value = accessor.yuvAtSourcePixel({x * 2, y * 2});
        return value ? std::array<double, 2>{static_cast<double>(value->u),
                                             static_cast<double>(value->v)}
                     : std::array<double, 2>{};
    };
    const auto topLeft = sample(x0, y0);
    const auto topRight = sample(x1, y0);
    const auto bottomLeft = sample(x0, y1);
    const auto bottomRight = sample(x1, y1);
    std::array<double, 2> result{};
    for (std::size_t channel = 0; channel < result.size(); ++channel) {
        const double top = topLeft[channel] * (1.0 - fx) + topRight[channel] * fx;
        const double bottom = bottomLeft[channel] * (1.0 - fx) + bottomRight[channel] * fx;
        result[channel] = top * (1.0 - fy) + bottom * fy;
    }
    return result;
}

std::array<int, 3> displayRgbAt(const RawPlaneAccessor& accessor,
                                const RawImageParameters& parameters,
                                const QPoint& displayPixel) {
    if (parameters.isYuv()) {
        const auto sample = accessor.yuvAtDisplayPixel(displayPixel);
        if (!sample) return {};
        const int bits = parameters.validBits();
        const double maximum = static_cast<double>((1 << bits) - 1);
        const double scale = static_cast<double>(1 << (bits - 8));
        const double yOffset =
            parameters.range == QuantizationRange::Limited ? 16.0 * scale : 0.0;
        const double yScale =
            parameters.range == QuantizationRange::Limited ? 219.0 * scale : maximum;
        const double chromaCenter = static_cast<double>(1 << (bits - 1));
        const double chromaScale =
            parameters.range == QuantizationRange::Limited ? 224.0 * scale : maximum;
        const auto coefficients = yuvCoefficients(parameters.yuvMatrix);
        const auto chroma = interpolatedChromaAt(accessor, sample->sourcePixel);
        const double y = (sample->y - yOffset) / yScale;
        const double u = (chroma[0] - chromaCenter) / chromaScale;
        const double v = (chroma[1] - chromaCenter) / chromaScale;
        return {toByte(y + coefficients.redV * v),
                toByte(y - coefficients.greenU * u - coefficients.greenV * v),
                toByte(y + coefficients.blueU * u)};
    }

    const auto center = accessor.bayerAtDisplayPixel(displayPixel);
    if (!center) return {};
    const int white = parameters.whiteLevel > parameters.blackLevel
                          ? parameters.whiteLevel
                          : parameters.maximumSampleValue();
    auto normalized = [&](const QPoint& sourcePixel) {
        const auto sample = accessor.bayerAtSourcePixel(sourcePixel);
        if (!sample || white <= parameters.blackLevel) return 0.0;
        return std::clamp((sample->value - parameters.blackLevel) /
                              static_cast<double>(white - parameters.blackLevel),
                          0.0, 1.0);
    };
    // The unified histogram always reconstructs RGB, even when the canvas shows mosaic gray.

    std::array<double, 3> sums{};
    std::array<int, 3> counts{};
    const QRect sourceBounds(QPoint{}, parameters.size);
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const QPoint point(std::clamp(center->sourcePixel.x() + dx, 0, sourceBounds.right()),
                               std::clamp(center->sourcePixel.y() + dy, 0, sourceBounds.bottom()));
            const BayerSampleChannel channel =
                RawPlaneAccessor::channelAtSourcePixel(parameters.bayerPattern, point);
            const int index = channel == BayerSampleChannel::Red
                                  ? 0
                                  : channel == BayerSampleChannel::Blue ? 2 : 1;
            sums[static_cast<std::size_t>(index)] += normalized(point);
            ++counts[static_cast<std::size_t>(index)];
        }
    }
    std::array<double, 3> balanced{};
    for (std::size_t channel = 0; channel < balanced.size(); ++channel) {
        balanced[channel] =
            counts[channel] > 0
                ? sums[channel] / counts[channel] * parameters.whiteBalanceGains[channel] : 0.0;
    }
    std::array<int, 3> result{};
    for (std::size_t output = 0; output < result.size(); ++output) {
        double corrected = 0.0;
        for (std::size_t input = 0; input < balanced.size(); ++input) {
            corrected += parameters.colorCorrectionMatrix[output * 3 + input] * balanced[input];
        }
        result[output] = toByte(std::pow(std::clamp(corrected, 0.0, 1.0),
                                         1.0 / parameters.displayGamma));
    }
    return result;
}

QRect pixelRegion(const QSize& size, const QRectF& normalizedRegion) {
    const QRectF clipped =
        normalizedRegion.normalized().intersected(QRectF(0.0, 0.0, 1.0, 1.0));
    if (size.isEmpty() || clipped.isEmpty()) {
        return {};
    }
    const int left = std::clamp(static_cast<int>(std::floor(clipped.left() * size.width())), 0,
                                size.width());
    const int top = std::clamp(static_cast<int>(std::floor(clipped.top() * size.height())), 0,
                               size.height());
    const int right = std::clamp(static_cast<int>(std::ceil(clipped.right() * size.width())), left,
                                 size.width());
    const int bottom =
        std::clamp(static_cast<int>(std::ceil(clipped.bottom() * size.height())), top,
                   size.height());
    return {left, top, right - left, bottom - top};
}

DisplayHistogram analyzeRegionImpl(const ImageFrame& frame, const QRectF& normalizedRegion,
                                   qint64 maximumSamples, bool nativeRgb = false) {
    DisplayHistogram result;
    const RawPlaneAccessor rawAccessor(frame);
    if (rawAccessor.isValid() && frame.rawParameters) {
        if (nativeRgb || !frame.rawParameters->hasValidDisplayTransform() ||
            (!rawAccessor.isYuv() && (rawAccessor.sourceSize().width() < 2 ||
                                      rawAccessor.sourceSize().height() < 2))) return {};
        result.analyzedSize = rawAccessor.displaySize();
        result.logicalSize = result.analyzedSize;
        result.analyzedRegion = pixelRegion(result.analyzedSize, normalizedRegion);
        result.logicalRegion = result.analyzedRegion;
        if (result.analyzedRegion.isEmpty() || maximumSamples <= 0) return {};
        result.availablePixelCount = static_cast<qint64>(result.analyzedRegion.width()) *
                                     result.analyzedRegion.height();
        ChannelAccumulator red = makeAccumulator(255);
        ChannelAccumulator green = makeAccumulator(255);
        ChannelAccumulator blue = makeAccumulator(255);
        ChannelAccumulator luma = makeAccumulator(255);
        const auto [sampleColumns, sampleRows] =
            gridSampleCounts(result.analyzedRegion.width(), result.analyzedRegion.height(),
                             maximumSamples);
        for (int rowIndex = 0; rowIndex < sampleRows; ++rowIndex) {
            const int y = result.analyzedRegion.top() +
                          static_cast<int>(static_cast<qint64>(rowIndex) *
                                           result.analyzedRegion.height() / sampleRows);
            for (int columnIndex = 0; columnIndex < sampleColumns; ++columnIndex) {
                const int x = result.analyzedRegion.left() +
                              static_cast<int>(static_cast<qint64>(columnIndex) *
                                               result.analyzedRegion.width() / sampleColumns);
                const auto rgb = displayRgbAt(rawAccessor, *frame.rawParameters, {x, y});
                const int yPrime = (2126 * rgb[0] + 7152 * rgb[1] + 722 * rgb[2] + 5000) / 10000;
                addSample(red, rgb[0]);
                addSample(green, rgb[1]);
                addSample(blue, rgb[2]);
                addSample(luma, yPrime);
                ++result.sampledPixelCount;
            }
        }
        finishChannel(red, result.sampledPixelCount);
        finishChannel(green, result.sampledPixelCount);
        finishChannel(blue, result.sampledPixelCount);
        finishChannel(luma, result.sampledPixelCount);
        result.red = std::move(red.channel);
        result.green = std::move(green.channel);
        result.blue = std::move(blue.channel);
        result.luma = std::move(luma.channel);
        return result;
    }

    const QImage* source = frame.qImage();
    if (!source || source->isNull() || maximumSamples <= 0) {
        return result;
    }

    QImage colorConverted;
    if (!nativeRgb && source->colorSpace().isValid() &&
        source->colorSpace() != QColorSpace(QColorSpace::SRgb)) {
        colorConverted = source->convertedToColorSpace(QColorSpace::SRgb);
        if (colorConverted.isNull()) return {};
        source = &colorConverted;
    }

    const bool highBitDepth = frame.descriptor.validBits > 8 || source->depth() > 32;
    const QImage::Format targetFormat = highBitDepth ? QImage::Format_RGBA64
                                                      : QImage::Format_RGBA8888;
    QImage converted;
    if (source->format() != targetFormat &&
        !(targetFormat == QImage::Format_RGBA8888 &&
          source->format() == QImage::Format_RGBX8888)) {
        converted = source->convertToFormat(targetFormat);
        source = &converted;
    }
    if (source->isNull()) {
        return result;
    }

    result.analyzedSize = source->size();
    result.logicalSize = frame.descriptor.size.isEmpty() ? source->size() : frame.descriptor.size;
    result.analyzedRegion = pixelRegion(result.analyzedSize, normalizedRegion);
    result.logicalRegion = pixelRegion(result.logicalSize, normalizedRegion);
    if (result.analyzedRegion.isEmpty() || result.logicalRegion.isEmpty()) {
        return {};
    }
    result.availablePixelCount = static_cast<qint64>(result.analyzedRegion.width()) *
                                 result.analyzedRegion.height();
    if (result.availablePixelCount <= 0) {
        return {};
    }

    result.maximumValue = nativeRgb && highBitDepth ? 65535 : 255;
    ChannelAccumulator red = makeAccumulator(result.maximumValue);
    ChannelAccumulator green = makeAccumulator(result.maximumValue);
    ChannelAccumulator blue = makeAccumulator(result.maximumValue);
    ChannelAccumulator luma = makeAccumulator(result.maximumValue);
    const auto [sampleColumns, sampleRows] =
        gridSampleCounts(result.analyzedRegion.width(), result.analyzedRegion.height(),
                         maximumSamples);
    for (int rowIndex = 0; rowIndex < sampleRows; ++rowIndex) {
        const int y = result.analyzedRegion.top() +
                      static_cast<int>(static_cast<qint64>(rowIndex) *
                                       result.analyzedRegion.height() / sampleRows);
        const uchar* row = source->constScanLine(y);
        for (int columnIndex = 0; columnIndex < sampleColumns; ++columnIndex) {
            const int x = result.analyzedRegion.left() +
                          static_cast<int>(static_cast<qint64>(columnIndex) *
                                           result.analyzedRegion.width() / sampleColumns);
            int r = 0;
            int g = 0;
            int b = 0;
            if (highBitDepth) {
                const QRgba64 pixel = reinterpret_cast<const QRgba64*>(row)[x];
                r = pixel.red();
                g = pixel.green();
                b = pixel.blue();
                if (!nativeRgb) {
                    r = (r + 128) / 257;
                    g = (g + 128) / 257;
                    b = (b + 128) / 257;
                }
            } else {
                const uchar* pixel = row + x * 4;
                r = pixel[0];
                g = pixel[1];
                b = pixel[2];
            }
            const int yPrime = (2126 * r + 7152 * g + 722 * b + 5000) / 10000;
            addSample(red, r);
            addSample(green, g);
            addSample(blue, b);
            addSample(luma, yPrime);
            ++result.sampledPixelCount;
        }
    }

    finishChannel(red, result.sampledPixelCount);
    finishChannel(green, result.sampledPixelCount);
    finishChannel(blue, result.sampledPixelCount);
    finishChannel(luma, result.sampledPixelCount);
    result.red = std::move(red.channel);
    result.green = std::move(green.channel);
    result.blue = std::move(blue.channel);
    result.luma = std::move(luma.channel);
    return result;
}

} // namespace

DisplayHistogram DisplayHistogramAnalyzer::analyzeNativeRgb(const ImageFrame& frame) {
    return analyzeRegionImpl(frame, QRectF(0.0, 0.0, 1.0, 1.0), kDefaultMaximumSamples, true);
}

DisplayHistogram DisplayHistogramAnalyzer::analyze(const ImageFrame& frame,
                                                    qint64 maximumSamples) {
    return analyzeRegionImpl(frame, QRectF(0.0, 0.0, 1.0, 1.0), maximumSamples);
}

DisplayHistogram DisplayHistogramAnalyzer::analyzeRegion(const ImageFrame& frame,
                                                          const QRectF& normalizedRegion,
                                                          qint64 maximumSamples) {
    return analyzeRegionImpl(frame, normalizedRegion, maximumSamples);
}

} // namespace ispview
