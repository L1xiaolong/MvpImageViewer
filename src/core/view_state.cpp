#include "core/view_state.h"

#include <algorithm>
#include <cmath>

namespace ispview {

double ViewTransform::fitScale(const QSize& imageSize, const QSize& viewportSize) {
    if (imageSize.isEmpty() || viewportSize.isEmpty()) {
        return 1.0;
    }
    const double horizontal = static_cast<double>(viewportSize.width()) / imageSize.width();
    const double vertical = static_cast<double>(viewportSize.height()) / imageSize.height();
    return std::max(0.0001, std::min(horizontal, vertical));
}

QPointF ViewTransform::widgetToImage(const QPointF& widgetPoint, const QSizeF& viewportSize,
                                     const QSize& imageSize, const ViewState& state) {
    if (imageSize.isEmpty() || state.pixelsPerImagePixel <= 0.0) {
        return {};
    }
    const QPointF viewportCenter(viewportSize.width() * 0.5, viewportSize.height() * 0.5);
    const QPointF imageCenter(state.normalizedCenter.x() * imageSize.width(),
                              state.normalizedCenter.y() * imageSize.height());
    return imageCenter + (widgetPoint - viewportCenter) / state.pixelsPerImagePixel;
}

QPointF ViewTransform::imageToWidget(const QPointF& imagePoint, const QSizeF& viewportSize,
                                     const QSize& imageSize, const ViewState& state) {
    const QPointF viewportCenter(viewportSize.width() * 0.5, viewportSize.height() * 0.5);
    const QPointF imageCenter(state.normalizedCenter.x() * imageSize.width(),
                              state.normalizedCenter.y() * imageSize.height());
    return viewportCenter + (imagePoint - imageCenter) * state.pixelsPerImagePixel;
}

std::optional<QPoint>
ViewTransform::imagePixelAtWidgetPoint(const QPointF& widgetPoint, const QSizeF& viewportSize,
                                       const QSize& imageSize, const ViewState& state) {
    const QPointF imagePoint = widgetToImage(widgetPoint, viewportSize, imageSize, state);
    if (!std::isfinite(imagePoint.x()) || !std::isfinite(imagePoint.y()) ||
        imagePoint.x() < 0.0 || imagePoint.y() < 0.0 || imagePoint.x() >= imageSize.width() ||
        imagePoint.y() >= imageSize.height()) {
        return std::nullopt;
    }
    return QPoint(static_cast<int>(std::floor(imagePoint.x())),
                  static_cast<int>(std::floor(imagePoint.y())));
}

ViewState ViewTransform::zoomAt(const ViewState& state, double newScale,
                                const QPointF& anchorInWidget, const QSizeF& viewportSize,
                                const QSize& imageSize) {
    if (imageSize.isEmpty()) {
        return state;
    }

    ViewState result = state;
    newScale = std::clamp(newScale, 0.005, 64.0);
    const QPointF anchorImage = widgetToImage(anchorInWidget, viewportSize, imageSize, state);
    const QPointF viewportCenter(viewportSize.width() * 0.5, viewportSize.height() * 0.5);
    const QPointF newCenterImage = anchorImage - (anchorInWidget - viewportCenter) / newScale;

    result.pixelsPerImagePixel = newScale;
    result.normalizedCenter = clampedCenter(
        {newCenterImage.x() / imageSize.width(), newCenterImage.y() / imageSize.height()});
    result.fitMode = FitMode::Manual;
    return result;
}

ViewState ViewTransform::panBy(const ViewState& state, const QPointF& widgetDelta,
                               const QSize& imageSize) {
    if (imageSize.isEmpty() || state.pixelsPerImagePixel <= 0.0) {
        return state;
    }
    ViewState result = state;
    result.normalizedCenter -=
        QPointF(widgetDelta.x() / (imageSize.width() * state.pixelsPerImagePixel),
                widgetDelta.y() / (imageSize.height() * state.pixelsPerImagePixel));
    result.normalizedCenter = clampedCenter(result.normalizedCenter);
    result.fitMode = FitMode::Manual;
    return result;
}

QRectF ViewTransform::visibleNormalizedRect(const QSizeF& viewportSize, const QSize& imageSize,
                                            const ViewState& state) {
    if (viewportSize.isEmpty() || imageSize.isEmpty() || state.pixelsPerImagePixel <= 0.0) {
        return {};
    }

    const QPointF topLeft = widgetToImage(QPointF(0.0, 0.0), viewportSize, imageSize, state);
    const QPointF bottomRight = widgetToImage(QPointF(viewportSize.width(), viewportSize.height()),
                                              viewportSize, imageSize, state);
    const QRectF visiblePixels =
        QRectF(topLeft, bottomRight).normalized().intersected(QRectF(QPointF{}, imageSize));
    if (visiblePixels.isEmpty()) {
        return {};
    }

    return QRectF(visiblePixels.x() / imageSize.width(), visiblePixels.y() / imageSize.height(),
                  visiblePixels.width() / imageSize.width(),
                  visiblePixels.height() / imageSize.height());
}

QPointF ViewTransform::clampedCenter(QPointF center) {
    center.setX(std::clamp(center.x(), 0.0, 1.0));
    center.setY(std::clamp(center.y(), 0.0, 1.0));
    return center;
}

std::optional<QRectF> ViewTransform::clampedNormalizedRoi(QRectF roi) {
    roi = roi.normalized().intersected(QRectF(0.0, 0.0, 1.0, 1.0));
    if (roi.width() <= 0.0 || roi.height() <= 0.0) {
        return std::nullopt;
    }
    return roi;
}

} // namespace ispview
