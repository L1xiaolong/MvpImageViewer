#include "core/display_histogram.h"

#include "core/color_conversion.h"
#include "core/raw_plane_access.h"
#include "core/yuv_plane_view.h"
#include <QColorSpace>
#include <QScopeGuard>
#include <QSemaphore>
#include <QThread>
#include <QThreadPool>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <optional>

namespace mvpview {
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

struct DisplayAccumulator {
    ChannelAccumulator red = makeAccumulator(255);
    ChannelAccumulator green = makeAccumulator(255);
    ChannelAccumulator blue = makeAccumulator(255);
    ChannelAccumulator luma = makeAccumulator(255);
    qint64 count = 0;

    void add(const std::array<int, 3>& rgb, bool lumaOnly) {
        if (!lumaOnly) {
            addSample(red, rgb[0]);
            addSample(green, rgb[1]);
            addSample(blue, rgb[2]);
        }
        addSample(luma, (2126 * rgb[0] + 7152 * rgb[1] + 722 * rgb[2] + 5000) / 10000);
        ++count;
    }

    void merge(const DisplayAccumulator& other, bool lumaOnly) {
        const auto mergeChannel = [](ChannelAccumulator& to, const ChannelAccumulator& from) {
            for (qsizetype i = 0; i < to.channel.bins.size(); ++i)
                to.channel.bins[i] += from.channel.bins[i];
            to.sum += from.sum;
            to.squaredSum += from.squaredSum;
        };
        if (!lumaOnly) {
            mergeChannel(red, other.red);
            mergeChannel(green, other.green);
            mergeChannel(blue, other.blue);
        }
        mergeChannel(luma, other.luma);
        count += other.count;
    }
};

// Shared across analyses: comparing several images cannot create a pool per image.
// tryStart never queues work, so analysis cannot block behind another analysis's helpers.
class HistogramPool : public QThreadPool {
  public:
    HistogramPool() {
        setMaxThreadCount(std::clamp(QThread::idealThreadCount() / 2 - 1, 1, 3));
        setExpiryTimeout(10'000);
        setThreadPriority(QThread::LowPriority);
    }
};

template <typename PixelReader>
bool accumulateRawHistogram(DisplayAccumulator& total, const QRect& region,
                            int columns, int rows, const PixelReader& readPixel,
                            const std::function<bool()>& cancelled, bool lumaOnly) {
    if (cancelled && cancelled()) return false;
    std::array<DisplayAccumulator, 3> helpers;
    std::atomic<int> nextRow{0};
    std::atomic<bool> stopped{false};
    QSemaphore finished;
    int running = 0;
    const auto process = [&](DisplayAccumulator& accumulator, bool caller) {
        for (;;) {
            const int first = nextRow.fetch_add(8, std::memory_order_relaxed);
            if (first >= rows) return;
            for (int row = first; row < std::min(first + 8, rows); ++row) {
                // Invoke user callbacks only on the calling thread, as before.
                if (caller && cancelled && cancelled())
                    stopped.store(true, std::memory_order_relaxed);
                if (stopped.load(std::memory_order_relaxed)) return;
                const int y = region.top() + static_cast<int>(qint64(row) * region.height() / rows);
                for (int column = 0; column < columns; ++column) {
                    const int x = region.left() +
                                  static_cast<int>(qint64(column) * region.width() / columns);
                    const QPoint point = columns == region.width() && rows == region.height()
                        ? QPoint(x, y)
                        : detail::histogramSamplePoint(region, column, row, columns, rows);
                    accumulator.add(readPixel(point), lumaOnly);
                }
            }
        }
    };
    // Keep borrowed frame/converter/accumulator storage alive even if a caller callback throws.
    const auto join = qScopeGuard([&] {
        stopped.store(true, std::memory_order_relaxed);
        finished.acquire(running);
    });
    if (qint64(columns) * rows >= 1'000'000 && QThread::idealThreadCount() >= 4) {
        static HistogramPool pool;
        for (auto& helper : helpers) {
            if (!pool.tryStart([&, target = &helper] {
                    process(*target, false);
                    finished.release();
                })) break;
            ++running;
        }
    }
    process(total, true);
    while (running > 0) {
        if (finished.tryAcquire(1, 10)) --running;
        if (cancelled && cancelled()) stopped.store(true, std::memory_order_relaxed);
    }
    if (stopped.load(std::memory_order_relaxed)) return false;
    for (const auto& helper : helpers) total.merge(helper, lumaOnly);
    return true;
}

int toByte(double value) {
    return std::clamp(static_cast<int>(std::lround(value * 255.0)), 0, 255);
}

std::array<double, 2> interpolatedChromaAt(const RawImageParameters& parameters,
                                           const detail::YuvPlaneView& planes,
                                           const QPoint& sourcePixel) {
    const QSize sourceSize = parameters.size;
    const QSize chromaSize((sourceSize.width() + 1) / 2, (sourceSize.height() + 1) / 2);
    const double chromaX = std::clamp(
        parameters.chromaLocation == ChromaLocation::Left
            ? static_cast<double>(sourcePixel.x()) * chromaSize.width() / sourceSize.width()
            : (sourcePixel.x() + 0.5) * chromaSize.width() / sourceSize.width() - 0.5,
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
        const auto value = planes.chroma(x, y);
        return std::array<double, 2>{static_cast<double>(value[0]), static_cast<double>(value[1])};
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

class YuvHistogramConverter {
  public:
    YuvHistogramConverter(const ImageFrame& frame, DisplayColorSpace target, bool cacheTransfer)
        : parameters_(*frame.rawParameters), planes_(frame),
          transform_(parameters_, target),
          coefficients_(yuvCoefficients(parameters_.yuvMatrix)) {
        const auto& parameters = parameters_;
        const int bits = parameters.validBits();
        const double maximum = static_cast<double>((1 << bits) - 1);
        const double scale = static_cast<double>(1 << (bits - 8));
        yOffset_ =
            parameters.range == QuantizationRange::Limited ? 16.0 * scale : 0.0;
        yScale_ =
            parameters.range == QuantizationRange::Limited ? 219.0 * scale : maximum;
        chromaCenter_ = static_cast<double>(1 << (bits - 1));
        chromaScale_ =
            parameters.range == QuantizationRange::Limited ? 224.0 * scale : maximum;
        if (cacheTransfer && parameters.yuvPrimaries == YuvPrimaries::BT709 &&
            target == DisplayColorSpace::Srgb) {
            // Identity primaries: each output depends only on its own input channel.
            // Cache only intervals whose endpoints round to the SAME byte. Ambiguous
            // intervals still use the original double-precision transform, not interpolation.
            transferBins_.resize(kTransferIntervals);
            int left = toByte(transform_.convert({0.0, 0.0, 0.0})[0]);
            for (int i = 0; i < kTransferIntervals; ++i) {
                const double value = double(i + 1) / kTransferIntervals;
                const int right = toByte(transform_.convert({value, value, value})[0]);
                transferBins_[i] = left == right ? static_cast<qint16>(left) : qint16{-1};
                left = right;
            }
            // Rounded transfer constants can create tiny jumps at these branch points.
            // Do not assume monotonicity across a branch, even if its endpoint bytes agree.
            for (const double branch : {0.081, 0.04045, 0.0031308,
                                        4.5 * 0.0031308, 12.92 * 0.0031308}) {
                const int index = int(branch * kTransferIntervals);
                transferBins_[index - 1] = transferBins_[index] = -1;
            }
        }
    }

    std::array<int, 3> rgbAt(const QPoint& displayPixel) const {
        const auto source = displayToSourcePixel(displayPixel, parameters_.size,
                                                 parameters_.orientation);
        const auto chroma = interpolatedChromaAt(parameters_, planes_, source);
        const double y = (planes_.luma(source.x(), source.y()) - yOffset_) / yScale_;
        const double u = (chroma[0] - chromaCenter_) / chromaScale_;
        const double v = (chroma[1] - chromaCenter_) / chromaScale_;
        const std::array<double, 3> sourceRgb{
            y + coefficients_.redV * v,
            y - coefficients_.greenU * u - coefficients_.greenV * v,
            y + coefficients_.blueU * u};
        if (!transferBins_.isEmpty()) {
            std::array<int, 3> result{};
            bool exact = true;
            for (std::size_t channel = 0; channel < result.size(); ++channel) {
                // Power-of-two interval widths are exactly representable in double.
                const int index = std::min(kTransferIntervals - 1,
                    int(std::clamp(sourceRgb[channel], 0.0, 1.0) * kTransferIntervals));
                result[channel] = transferBins_.at(index);
                exact = exact && result[channel] >= 0;
            }
            if (exact) return result;
        }
        const auto rgb = transform_.convert(sourceRgb);
        return {toByte(rgb[0]), toByte(rgb[1]), toByte(rgb[2])};
    }

  private:
    const RawImageParameters& parameters_;
    detail::YuvPlaneView planes_;
    YuvDisplayTransform transform_;
    YuvCoefficients coefficients_;
    double yOffset_, yScale_, chromaCenter_, chromaScale_;
    static constexpr int kTransferIntervals = 16384;
    QVector<qint16> transferBins_;
};

std::array<int, 3> displayBayerRgbAt(const RawPlaneAccessor& accessor,
                                     const RawImageParameters& parameters,
                                     const QPoint& displayPixel) {
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
    // The unified histogram always reconstructs RGB, even when the canvas shows CFA false colour.

    std::array<double, 3> sums{};
    std::array<int, 3> counts{};
    const QRect sourceBounds(QPoint{}, parameters.size);
    const int radius = bayerSampleBlockSize(parameters.bayerSampling);
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const QPoint point(std::clamp(center->sourcePixel.x() + dx, 0, sourceBounds.right()),
                               std::clamp(center->sourcePixel.y() + dy, 0, sourceBounds.bottom()));
            const BayerSampleChannel channel =
                RawPlaneAccessor::channelAtSourcePixel(parameters.bayerPattern, point,
                                                       parameters.bayerSampling);
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
                                   qint64 maximumSamples, bool nativeRgb = false,
                                   const std::function<bool()>& cancelled = {}, bool lumaOnly = false) {
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
        DisplayAccumulator total;
        const auto [sampleColumns, sampleRows] =
            detail::histogramGridCounts(result.analyzedRegion.width(), result.analyzedRegion.height(),
                             maximumSamples);
        std::optional<YuvHistogramConverter> yuv;
        if (rawAccessor.isYuv())
            yuv.emplace(frame, currentDisplayColorSpace(),
                        qint64(sampleColumns) * sampleRows >= 1'000'000);
        if (!accumulateRawHistogram(total, result.analyzedRegion, sampleColumns, sampleRows,
                [&](const QPoint& pixel) {
                    return yuv ? yuv->rgbAt(pixel)
                               : displayBayerRgbAt(rawAccessor, *frame.rawParameters, pixel);
                }, cancelled, lumaOnly)) return {};
        result.sampledPixelCount = total.count;
        if (!lumaOnly) {
            finishChannel(total.red, total.count);
            finishChannel(total.green, total.count);
            finishChannel(total.blue, total.count);
            result.red = std::move(total.red.channel);
            result.green = std::move(total.green.channel);
            result.blue = std::move(total.blue.channel);
        }
        finishChannel(total.luma, total.count);
        result.luma = std::move(total.luma.channel);
        return result;
    }

    const QImage* source = frame.qImage();
    if (!source || source->isNull() || maximumSamples <= 0) {
        return result;
    }

    QImage colorConverted;
    const QColorSpace displaySpace = displayQColorSpace(currentDisplayColorSpace());
    if (!nativeRgb && source->colorSpace().isValid() && source->colorSpace() != displaySpace) {
        // Readouts follow the presentation space so the numbers describe the pixels the canvas
        // samples, not the file's own space.
        colorConverted = source->convertedToColorSpace(displaySpace);
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
        detail::histogramGridCounts(result.analyzedRegion.width(), result.analyzedRegion.height(),
                         maximumSamples);
    for (int rowIndex = 0; rowIndex < sampleRows; ++rowIndex) {
        if (cancelled && cancelled()) return {};
        const int y = result.analyzedRegion.top() +
                      static_cast<int>(static_cast<qint64>(rowIndex) *
                                       result.analyzedRegion.height() / sampleRows);
        const uchar* row = source->constScanLine(y);
        for (int columnIndex = 0; columnIndex < sampleColumns; ++columnIndex) {
            const QPoint point = detail::histogramSamplePoint(result.analyzedRegion,
                columnIndex, rowIndex, sampleColumns, sampleRows);
            const int x = point.x();
            const uchar* pixelRow = point.y() == y ? row : source->constScanLine(point.y());
            int r = 0;
            int g = 0;
            int b = 0;
            if (highBitDepth) {
                const QRgba64 pixel = reinterpret_cast<const QRgba64*>(pixelRow)[x];
                r = pixel.red();
                g = pixel.green();
                b = pixel.blue();
                if (!nativeRgb) {
                    r = (r + 128) / 257;
                    g = (g + 128) / 257;
                    b = (b + 128) / 257;
                }
            } else {
                const uchar* pixel = pixelRow + x * 4;
                r = pixel[0];
                g = pixel[1];
                b = pixel[2];
            }
            const int yPrime = (2126 * r + 7152 * g + 722 * b + 5000) / 10000;
            if (!lumaOnly) {
                addSample(red, r);
                addSample(green, g);
                addSample(blue, b);
            }
            addSample(luma, yPrime);
            ++result.sampledPixelCount;
        }
    }

    if (!lumaOnly) {
        finishChannel(red, result.sampledPixelCount);
        finishChannel(green, result.sampledPixelCount);
        finishChannel(blue, result.sampledPixelCount);
        result.red = std::move(red.channel);
        result.green = std::move(green.channel);
        result.blue = std::move(blue.channel);
    }
    finishChannel(luma, result.sampledPixelCount);
    result.luma = std::move(luma.channel);
    return result;
}

} // namespace

DisplayHistogram DisplayHistogramAnalyzer::analyzeLuma(
    const ImageFrame& frame, const std::function<bool()>& cancelled, qint64 maximumSamples) {
    return analyzeRegionImpl(frame, QRectF(0.0, 0.0, 1.0, 1.0), maximumSamples,
                             false, cancelled, true);
}

DisplayHistogram DisplayHistogramAnalyzer::analyzeNativeRgb(const ImageFrame& frame, const std::function<bool()>& cancelled) {
    return analyzeRegionImpl(frame, QRectF(0.0, 0.0, 1.0, 1.0), kDefaultMaximumSamples, true, cancelled);
}

DisplayHistogram DisplayHistogramAnalyzer::analyze(const ImageFrame& frame,
                                                    qint64 maximumSamples, const std::function<bool()>& cancelled) {
    return analyzeRegionImpl(frame, QRectF(0.0, 0.0, 1.0, 1.0), maximumSamples, false, cancelled);
}

DisplayHistogram DisplayHistogramAnalyzer::analyzeRegion(const ImageFrame& frame,
                                                          const QRectF& normalizedRegion,
                                                          qint64 maximumSamples, const std::function<bool()>& cancelled) {
    return analyzeRegionImpl(frame, normalizedRegion, maximumSamples, false, cancelled);
}

} // namespace mvpview
