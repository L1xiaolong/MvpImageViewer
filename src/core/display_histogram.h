#pragma once

#include "core/image_types.h"
#include "core/histogram_sampling.h"

#include <QSize>
#include <QRect>
#include <QRectF>

#include <QVector>
#include <limits>
#include <functional>

namespace mvpview {

struct HistogramChannel {
    QVector<quint64> bins;
    int minimum = 0;
    int maximum = 0;
    double mean = 0.0;
    double standardDeviation = 0.0;
};

struct DisplayHistogram {
    HistogramChannel red;
    HistogramChannel green;
    HistogramChannel blue;
    HistogramChannel luma;
    QSize analyzedSize;
    QSize logicalSize;
    QRect analyzedRegion;
    QRect logicalRegion;
    qint64 availablePixelCount = 0;
    qint64 sampledPixelCount = 0;
    int maximumValue = 255;

    [[nodiscard]] bool isValid() const { return sampledPixelCount > 0; }
    [[nodiscard]] bool usesDisplayProxy() const { return analyzedSize != logicalSize; }
    [[nodiscard]] bool isSubsampled() const {
        return sampledPixelCount > 0 && sampledPixelCount < availablePixelCount;
    }
    [[nodiscard]] bool isRegionLimited() const {
        return isValid() && logicalRegion != QRect(QPoint{}, logicalSize);
    }
};

class DisplayHistogramAnalyzer final {
  public:
    // Bound interactive analysis work. Small images still visit every pixel.
    static constexpr qint64 kDefaultMaximumSamples = detail::kHistogramMaximumSamples;

    // Display-space luma for comparison overlays; RGB channels remain empty.
    [[nodiscard]] static DisplayHistogram analyzeLuma(
        const ImageFrame& frame, const std::function<bool()>& cancelled = {},
        qint64 maximumSamples = kDefaultMaximumSamples);

    // Decoded RGB code values, preserving 16-bit precision (before histogram normalization).
    [[nodiscard]] static DisplayHistogram analyzeNativeRgb(const ImageFrame& frame, const std::function<bool()>& cancelled = {});

    [[nodiscard]] static DisplayHistogram analyze(
        const ImageFrame& frame, qint64 maximumSamples = kDefaultMaximumSamples,
        const std::function<bool()>& cancelled = {});
    [[nodiscard]] static DisplayHistogram analyzeRegion(
        const ImageFrame& frame, const QRectF& normalizedRegion,
        qint64 maximumSamples = kDefaultMaximumSamples,
        const std::function<bool()>& cancelled = {});
};

} // namespace mvpview
