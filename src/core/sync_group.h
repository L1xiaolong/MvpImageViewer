#pragma once

#include "core/view_state.h"

namespace mvpview {

class SyncGroup final {
  public:
    void setZoomSynchronized(bool enabled) { syncZoom_ = enabled; }
    void setPanSynchronized(bool enabled) { syncPan_ = enabled; }
    void setRoiSynchronized(bool enabled) { syncRoi_ = enabled; }

    [[nodiscard]] bool zoomSynchronized() const { return syncZoom_; }
    [[nodiscard]] bool panSynchronized() const { return syncPan_; }
    [[nodiscard]] bool roiSynchronized() const { return syncRoi_; }

    [[nodiscard]] ViewState synchronizedState(const ViewState& source,
                                              const ViewState& target) const;
    [[nodiscard]] ViewState relativelySynchronizedState(const ViewState& previousSource,
                                                        const ViewState& source,
                                                        const ViewState& target) const;

  private:
    bool syncZoom_ = true;
    bool syncPan_ = true;
    bool syncRoi_ = true;
};

} // namespace mvpview
