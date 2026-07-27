#include "core/display_histogram.h"

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
    ++accumulator.channel.bins[static_cast<std::size_t>(value)];
    accumulator.sum += static_cast<quint64>(value);
    accumulator.squaredSum += static_cast<quint64>(value * value);
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
        255 - static_cast<int>(std::distance(accumulator.channel.bins.crbegin(), last));
    accumulator.channel.mean = static_cast<double>(accumulator.sum) /
                               static_cast<double>(sampleCount);
    const double meanSquare = static_cast<double>(accumulator.squaredSum) /
                              static_cast<double>(sampleCount);
    const double variance =
        std::max(0.0, meanSquare - accumulator.channel.mean * accumulator.channel.mean);
    accumulator.channel.standardDeviation = std::sqrt(variance);
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
                                   qint64 maximumSamples) {
    DisplayHistogram result;
    const QImage* source = frame.qImage();
    if (!source || source->isNull() || maximumSamples <= 0) {
        return result;
    }

    QImage converted;
    if (source->format() != QImage::Format_RGBA8888 &&
        source->format() != QImage::Format_RGBX8888) {
        converted = source->convertToFormat(QImage::Format_RGBA8888);
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

    ChannelAccumulator red;
    ChannelAccumulator green;
    ChannelAccumulator blue;
    ChannelAccumulator luma;
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
            const uchar* pixel = row + x * 4;
            const int r = pixel[0];
            const int g = pixel[1];
            const int b = pixel[2];
            const int yPrime = (54 * r + 183 * g + 19 * b + 128) >> 8;
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
