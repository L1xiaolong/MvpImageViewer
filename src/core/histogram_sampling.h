#pragma once

#include <QPair>
#include <QPoint>
#include <QRect>

#include <algorithm>
#include <cmath>

namespace mvpview::detail {

inline constexpr qint64 kHistogramMaximumSamples = 65'536;

inline QPair<int, int> histogramGridCounts(int width, int height, qint64 maximumSamples) {
    if (width <= 0 || height <= 0 || maximumSamples <= 0) return {0, 0};
    if (qint64(width) * height <= maximumSamples) return {width, height};
    const int maximumRows = int(std::min<qint64>(height, maximumSamples));
    const double idealRows = std::sqrt(double(maximumSamples) * height / width);
    const int rows = std::clamp(int(std::floor(idealRows)), 1, maximumRows);
    const int columns = int(std::clamp(maximumSamples / rows, qint64{1}, qint64(width)));
    return {columns, rows};
}

inline quint32 histogramSampleHash(quint32 value) {
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    return value ^ (value >> 16);
}

// One deterministic, jittered point per spatial cell. A fixed grid step can alias
// stripes/CFA patterns; varying the offset also keeps repeated analyses stable.
inline QPoint histogramSamplePoint(const QRect& region, int column, int row,
                                   int columns, int rows) {
    const int x = int(qint64(column) * region.width() / columns);
    const int y = int(qint64(row) * region.height() / rows);
    if (columns == region.width() && rows == region.height())
        return region.topLeft() + QPoint(x, y);
    const int width = int(qint64(column + 1) * region.width() / columns) - x;
    const int height = int(qint64(row + 1) * region.height() / rows) - y;
    const quint32 seed = quint32(row) * 0x9e3779b9U + quint32(column);
    return region.topLeft() + QPoint(x + int(histogramSampleHash(seed) % quint32(width)),
                                    y + int(histogramSampleHash(seed ^ 0xa511e9b3U) % quint32(height)));
}

// Bayer channels have separate native grids. Stratify their concatenated ordinals
// so every channel retains its own sample budget and CFA membership.
inline qint64 histogramSampleOrdinal(qint64 index, qint64 available, qint64 target) {
    const qint64 first = index * available / target;
    const qint64 end = (index + 1) * available / target;
    return first + histogramSampleHash(quint32(index)) % (end - first);
}

} // namespace mvpview::detail
