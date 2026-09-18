#include "core/raw_plane_histogram.h"

#include <algorithm>
#include <cmath>

namespace ispview {
namespace {

struct ChannelAccumulator {
    RawHistogramChannel channel;
    quint64 sum = 0;
    quint64 squaredSum = 0;
};

QRect pixelRegion(const QSize& size, const QRectF& normalizedRegion) {
    const QRectF clipped = normalizedRegion.normalized().intersected(QRectF(0.0, 0.0, 1.0, 1.0));
    if (size.isEmpty() || clipped.isEmpty()) {
        return {};
    }
    const int left =
        std::clamp(static_cast<int>(std::floor(clipped.left() * size.width())), 0, size.width());
    const int top =
        std::clamp(static_cast<int>(std::floor(clipped.top() * size.height())), 0, size.height());
    const int right =
        std::clamp(static_cast<int>(std::ceil(clipped.right() * size.width())), left, size.width());
    const int bottom = std::clamp(static_cast<int>(std::ceil(clipped.bottom() * size.height())),
                                  top, size.height());
    return {left, top, right - left, bottom - top};
}

QRect sourceRegionForDisplayRegion(const QRect& displayRegion,
                                   const RawImageParameters& parameters) {
    if (displayRegion.isEmpty()) {
        return {};
    }
    const QPoint corners[]{displayRegion.topLeft(), displayRegion.topRight(),
                           displayRegion.bottomLeft(), displayRegion.bottomRight()};
    int left = parameters.size.width();
    int top = parameters.size.height();
    int right = -1;
    int bottom = -1;
    for (const QPoint& corner : corners) {
        const QPoint source = displayToSourcePixel(corner, parameters.size, parameters.orientation);
        left = std::min(left, source.x());
        top = std::min(top, source.y());
        right = std::max(right, source.x());
        bottom = std::max(bottom, source.y());
    }
    return right >= left && bottom >= top ? QRect(left, top, right - left + 1, bottom - top + 1)
                                          : QRect{};
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
    const double idealRows = std::sqrt(static_cast<double>(maximumSamples) * height / width);
    const int rows = std::clamp(static_cast<int>(std::floor(idealRows)), 1, maximumRows);
    const int columns = static_cast<int>(
        std::clamp(maximumSamples / rows, qint64{1}, static_cast<qint64>(width)));
    return {columns, rows};
}

void addSample(ChannelAccumulator& accumulator, int value) {
    if (value < 0 || accumulator.channel.bins.isEmpty()) {
        return;
    }
    value = std::min(value, static_cast<int>(accumulator.channel.bins.size()) - 1);
    ++accumulator.channel.bins[value];
    accumulator.sum += static_cast<quint64>(value);
    accumulator.squaredSum += static_cast<quint64>(value) * static_cast<quint64>(value);
    ++accumulator.channel.sampledSampleCount;
}

void finishChannel(ChannelAccumulator& accumulator) {
    const auto first =
        std::find_if(accumulator.channel.bins.cbegin(), accumulator.channel.bins.cend(),
                     [](quint64 count) { return count != 0; });
    const auto last =
        std::find_if(accumulator.channel.bins.crbegin(), accumulator.channel.bins.crend(),
                     [](quint64 count) { return count != 0; });
    if (first == accumulator.channel.bins.cend() || last == accumulator.channel.bins.crend() ||
        accumulator.channel.sampledSampleCount <= 0) {
        return;
    }
    accumulator.channel.minimum =
        static_cast<int>(std::distance(accumulator.channel.bins.cbegin(), first));
    accumulator.channel.maximum =
        static_cast<int>(accumulator.channel.bins.size()) - 1 -
        static_cast<int>(std::distance(accumulator.channel.bins.crbegin(), last));
    const double count = static_cast<double>(accumulator.channel.sampledSampleCount);
    accumulator.channel.mean = static_cast<double>(accumulator.sum) / count;
    const double meanSquare = static_cast<double>(accumulator.squaredSum) / count;
    const double variance =
        std::max(0.0, meanSquare - accumulator.channel.mean * accumulator.channel.mean);
    accumulator.channel.standardDeviation = std::sqrt(variance);
}

ChannelAccumulator makeAccumulator(RawHistogramChannelId id, int maximumValue) {
    ChannelAccumulator result;
    result.channel.id = id;
    result.channel.bins.resize(maximumValue + 1);
    return result;
}

template <typename Callback>
void sampleGrid(const QRect& region, qint64 maximumSamples, Callback&& callback) {
    if (region.isEmpty() || maximumSamples <= 0) {
        return;
    }
    const auto [sampleColumns, sampleRows] =
        gridSampleCounts(region.width(), region.height(), maximumSamples);
    for (int rowIndex = 0; rowIndex < sampleRows; ++rowIndex) {
        const int y = region.top() +
                      static_cast<int>(static_cast<qint64>(rowIndex) * region.height() /
                                       sampleRows);
        for (int columnIndex = 0; columnIndex < sampleColumns; ++columnIndex) {
            const int x = region.left() +
                          static_cast<int>(static_cast<qint64>(columnIndex) * region.width() /
                                           sampleColumns);
            callback(QPoint(x, y));
        }
    }
}

bool matchesChannel(RawHistogramChannelId channel, BayerSampleChannel candidate) {
    return (channel == RawHistogramChannelId::Red && candidate == BayerSampleChannel::Red) ||
           (channel == RawHistogramChannelId::GreenRedRow &&
            candidate == BayerSampleChannel::GreenRedRow) ||
           (channel == RawHistogramChannelId::GreenBlueRow &&
            candidate == BayerSampleChannel::GreenBlueRow) ||
           (channel == RawHistogramChannelId::Blue && candidate == BayerSampleChannel::Blue);
}

int firstWithResidue(int minimum, int residue, int period) {
    const int current = ((minimum % period) + period) % period;
    return minimum + (residue - current + period) % period;
}

struct BayerSampleGrid {
    int firstX = 0;
    int firstY = 0;
    int columns = 0;
    int rows = 0;
};

RawPlaneHistogram analyzeRegionImpl(const ImageFrame& frame, const QRectF& normalizedRegion,
                                    qint64 maximumSamplesPerChannel) {
    RawPlaneHistogram result;
    RawPlaneAccessor accessor(frame);
    if (!accessor.isValid() || !frame.rawParameters || maximumSamplesPerChannel <= 0) {
        return result;
    }
    const RawImageParameters& parameters = *frame.rawParameters;
    result.domain = parameters.isYuv() ? RawHistogramDomain::Yuv : RawHistogramDomain::Bayer;
    result.logicalSize = accessor.displaySize();
    result.logicalRegion = pixelRegion(result.logicalSize, normalizedRegion);
    result.sourceRegion = sourceRegionForDisplayRegion(result.logicalRegion, parameters);
    result.validBits = accessor.validBits();
    if (result.logicalRegion.isEmpty() || result.sourceRegion.isEmpty()) {
        return {};
    }
    // White level controls rendering, not the engineering sample domain.
    const int maximumValue = accessor.maximumSampleValue();
    result.maximumValue = maximumValue;

    if (parameters.isYuv()) {
        ChannelAccumulator y = makeAccumulator(RawHistogramChannelId::Y, maximumValue);
        ChannelAccumulator u = makeAccumulator(RawHistogramChannelId::U, maximumValue);
        ChannelAccumulator v = makeAccumulator(RawHistogramChannelId::V, maximumValue);
        y.channel.availableSampleCount =
            static_cast<qint64>(result.sourceRegion.width()) * result.sourceRegion.height();
        sampleGrid(result.sourceRegion, maximumSamplesPerChannel, [&](const QPoint& point) {
            if (const auto sample = accessor.yuvAtSourcePixel(point)) {
                addSample(y, sample->y);
            }
        });

        const int chromaLeft = result.sourceRegion.left() / 2;
        const int chromaTop = result.sourceRegion.top() / 2;
        const int chromaRight = result.sourceRegion.right() / 2;
        const int chromaBottom = result.sourceRegion.bottom() / 2;
        const QRect chromaRegion(chromaLeft, chromaTop, chromaRight - chromaLeft + 1,
                                 chromaBottom - chromaTop + 1);
        u.channel.availableSampleCount =
            static_cast<qint64>(chromaRegion.width()) * chromaRegion.height();
        v.channel.availableSampleCount = u.channel.availableSampleCount;
        sampleGrid(chromaRegion, maximumSamplesPerChannel, [&](const QPoint& chromaPoint) {
            const QPoint sourcePoint(chromaPoint.x() * 2, chromaPoint.y() * 2);
            if (const auto sample = accessor.yuvAtSourcePixel(sourcePoint)) {
                addSample(u, sample->u);
                addSample(v, sample->v);
            }
        });
        finishChannel(y);
        finishChannel(u);
        finishChannel(v);
        result.channels = {std::move(y.channel), std::move(u.channel), std::move(v.channel)};
        return result;
    }

    const RawHistogramChannelId ids[]{
        RawHistogramChannelId::Red, RawHistogramChannelId::GreenRedRow,
        RawHistogramChannelId::GreenBlueRow, RawHistogramChannelId::Blue};
    for (RawHistogramChannelId id : ids) {
        ChannelAccumulator accumulator = makeAccumulator(id, maximumValue);
        const int blockSize = bayerSampleBlockSize(parameters.bayerSampling);
        const int period = blockSize * 2;
        QVector<BayerSampleGrid> grids;
        grids.reserve(blockSize * blockSize);
        for (int residueY = 0; residueY < period; ++residueY) {
            for (int residueX = 0; residueX < period; ++residueX) {
                const QPoint residue(residueX, residueY);
                const auto candidate = RawPlaneAccessor::channelAtSourcePixel(
                    parameters.bayerPattern, residue, parameters.bayerSampling);
                if (!matchesChannel(id, candidate)) continue;
                const int firstX = firstWithResidue(result.sourceRegion.left(), residueX, period);
                const int firstY = firstWithResidue(result.sourceRegion.top(), residueY, period);
                if (firstX > result.sourceRegion.right() || firstY > result.sourceRegion.bottom())
                    continue;
                BayerSampleGrid grid;
                grid.firstX = firstX;
                grid.firstY = firstY;
                grid.columns = (result.sourceRegion.right() - firstX) / period + 1;
                grid.rows = (result.sourceRegion.bottom() - firstY) / period + 1;
                grids.push_back(grid);
                accumulator.channel.availableSampleCount +=
                    static_cast<qint64>(grid.columns) * grid.rows;
            }
        }
        const qint64 target = std::min(maximumSamplesPerChannel,
                                       accumulator.channel.availableSampleCount);
        qsizetype gridIndex = 0;
        qint64 gridStart = 0;
        qint64 gridEnd = grids.isEmpty()
                             ? 0
                             : static_cast<qint64>(grids.constFirst().columns) *
                                   grids.constFirst().rows;
        for (qint64 sampleIndex = 0; sampleIndex < target; ++sampleIndex) {
            const qint64 ordinal =
                (sampleIndex + 1) * accumulator.channel.availableSampleCount / target - 1;
            while (ordinal >= gridEnd && gridIndex + 1 < grids.size()) {
                gridStart = gridEnd;
                ++gridIndex;
                gridEnd += static_cast<qint64>(grids.at(gridIndex).columns) *
                           grids.at(gridIndex).rows;
            }
            const BayerSampleGrid& grid = grids.at(gridIndex);
            const qint64 local = ordinal - gridStart;
            const int row = static_cast<int>(local / grid.columns);
            const int column = static_cast<int>(local % grid.columns);
            const QPoint point(grid.firstX + column * period, grid.firstY + row * period);
            if (const auto sample = accessor.bayerAtSourcePixel(point))
                addSample(accumulator, sample->value);
        }
        finishChannel(accumulator);
        result.channels.push_back(std::move(accumulator.channel));
    }
    return result;
}

} // namespace

bool RawPlaneHistogram::isValid() const {
    return !channels.isEmpty() &&
           std::any_of(channels.cbegin(), channels.cend(),
                       [](const RawHistogramChannel& channel) { return channel.isValid(); });
}

RawPlaneHistogram RawPlaneHistogramAnalyzer::analyze(const ImageFrame& frame,
                                                     qint64 maximumSamplesPerChannel) {
    return analyzeRegionImpl(frame, QRectF(0.0, 0.0, 1.0, 1.0), maximumSamplesPerChannel);
}

RawPlaneHistogram RawPlaneHistogramAnalyzer::analyzeRegion(const ImageFrame& frame,
                                                           const QRectF& normalizedRegion,
                                                           qint64 maximumSamplesPerChannel) {
    return analyzeRegionImpl(frame, normalizedRegion, maximumSamplesPerChannel);
}

QString rawHistogramChannelName(RawHistogramChannelId id) {
    switch (id) {
    case RawHistogramChannelId::Y:
        return QStringLiteral("Y");
    case RawHistogramChannelId::U:
        return QStringLiteral("U");
    case RawHistogramChannelId::V:
        return QStringLiteral("V");
    case RawHistogramChannelId::Red:
        return QStringLiteral("R");
    case RawHistogramChannelId::GreenRedRow:
        return QStringLiteral("Gr");
    case RawHistogramChannelId::GreenBlueRow:
        return QStringLiteral("Gb");
    case RawHistogramChannelId::Blue:
        return QStringLiteral("B");
    }
    return {};
}

} // namespace ispview
