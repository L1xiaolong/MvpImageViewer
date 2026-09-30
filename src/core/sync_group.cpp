#include "core/sync_group.h"

#include "core/view_state.h"

#include <algorithm>
#include <cmath>

namespace mvpview {

ViewState SyncGroup::synchronizedState(const ViewState& source, const ViewState& target) const {
    ViewState result = target;
    if (syncZoom_) {
        result.pixelsPerImagePixel = source.pixelsPerImagePixel;
        result.fitMode = source.fitMode;
    }
    if (syncPan_) {
        result.normalizedCenter = source.normalizedCenter;
    }
    if (syncRoi_) {
        result.normalizedRoi = source.normalizedRoi;
    }
    return result;
}

ViewState SyncGroup::relativelySynchronizedState(const ViewState& previousSource,
                                                 const ViewState& source,
                                                 const ViewState& target) const {
    ViewState result = target;
    if (syncZoom_ && std::isfinite(previousSource.pixelsPerImagePixel)
        && previousSource.pixelsPerImagePixel > 0.0
        && std::isfinite(source.pixelsPerImagePixel)) {
        const double ratio = source.pixelsPerImagePixel / previousSource.pixelsPerImagePixel;
        result.pixelsPerImagePixel =
            std::clamp(target.pixelsPerImagePixel * ratio, 0.005, 64.0);
        result.fitMode = FitMode::Manual;
    }
    if (syncPan_) {
        const QPointF centerDelta = source.normalizedCenter - previousSource.normalizedCenter;
        result.normalizedCenter =
            ViewTransform::clampedCenter(target.normalizedCenter + centerDelta);
        result.fitMode = FitMode::Manual;
    }
    if (syncRoi_) {
        result.normalizedRoi = source.normalizedRoi;
    }
    return result;
}

} // namespace mvpview
