#pragma once

#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QSizeF>

#include <optional>

namespace ispview {

enum class FitMode { Fit, Manual };

struct ViewState {
    double pixelsPerImagePixel = 1.0;
    QPointF normalizedCenter{0.5, 0.5};
    FitMode fitMode = FitMode::Fit;
    std::optional<QRectF> normalizedRoi;

    friend bool operator==(const ViewState&, const ViewState&) = default;
};

class ViewTransform final {
  public:
    [[nodiscard]] static double fitScale(const QSize& imageSize, const QSize& viewportSize);
    [[nodiscard]] static QPointF widgetToImage(const QPointF& widgetPoint,
                                               const QSizeF& viewportSize, const QSize& imageSize,
                                               const ViewState& state);
    [[nodiscard]] static QPointF imageToWidget(const QPointF& imagePoint,
                                               const QSizeF& viewportSize,
                                               const QSize& imageSize, const ViewState& state);
    [[nodiscard]] static std::optional<QPoint>
    imagePixelAtWidgetPoint(const QPointF& widgetPoint, const QSizeF& viewportSize,
                            const QSize& imageSize, const ViewState& state);
    [[nodiscard]] static ViewState zoomAt(const ViewState& state, double newScale,
                                          const QPointF& anchorInWidget,
                                          const QSizeF& viewportSize,
                                          const QSize& imageSize);
    [[nodiscard]] static ViewState panBy(const ViewState& state, const QPointF& widgetDelta,
                                         const QSize& imageSize);
    [[nodiscard]] static QRectF visibleNormalizedRect(const QSizeF& viewportSize,
                                                      const QSize& imageSize,
                                                      const ViewState& state);
    [[nodiscard]] static QPointF clampedCenter(QPointF center);
    [[nodiscard]] static std::optional<QRectF> clampedNormalizedRoi(QRectF roi);
};

} // namespace ispview
