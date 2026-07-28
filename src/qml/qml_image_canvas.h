#pragma once

#include "core/image_types.h"
#include "core/sync_group.h"
#include "core/view_state.h"

#include <QQuickRhiItem>
#include <QVariantMap>
#include <QtQml/qqml.h>

namespace ispview {

// A single scene-graph item renders the complete 2-4 image comparison. Qt 6.9
// does not reliably composite several sibling QQuickRhiItems on Metal, while a
// single item also avoids duplicating offscreen color buffers and preserves the
// full-resolution GPU RAW/YUV/Bayer path used by ImageCanvas.
class QmlImageCanvas : public QQuickRhiItem {
    Q_OBJECT
    QML_NAMED_ELEMENT(ImageCanvas)
    Q_PROPERTY(int presentationMode READ presentationMode WRITE setPresentationMode
                   NOTIFY presentationModeChanged)
    Q_PROPERTY(qreal compareAmount READ compareAmount WRITE setCompareAmount
                   NOTIFY compareAmountChanged)
    Q_PROPERTY(qreal dividerPosition READ dividerPosition NOTIFY dividerPositionChanged)
    Q_PROPERTY(bool viewSynchronized READ synchronized WRITE setSynchronized
                   NOTIFY synchronizedChanged)
    Q_PROPERTY(int imageCount READ imageCount NOTIFY imageCountChanged)
    Q_PROPERTY(int navigationRevision READ navigationRevision NOTIFY navigationRevisionChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor
                   NOTIFY backgroundColorChanged)

  public:
    explicit QmlImageCanvas(QQuickItem* parent = nullptr);

    int presentationMode() const { return presentationMode_; }
    qreal compareAmount() const { return compareAmount_; }
    qreal dividerPosition() const;
    bool synchronized() const { return synchronized_; }
    int imageCount() const { return static_cast<int>(frames_.size()); }
    int navigationRevision() const { return navigationRevision_; }
    QColor backgroundColor() const { return backgroundColor_; }
    const QVector<ImageFramePtr>& frames() const { return frames_; }

    void setPresentationMode(int mode);
    void setCompareAmount(qreal amount);
    void setSynchronized(bool enabled);
    void setBackgroundColor(const QColor& color);
    void setFrames(const QVector<ImageFramePtr>& frames, int changedSlot = -1,
                   bool resetChangedView = false);

    // Compatibility helpers used by the comparison controller while a
    // temporary B-over-A frame is active.
    void setFrameAt(int slot, ImageFramePtr frame, bool resetView = false);
    ImageFramePtr frameAt(int slot) const;
    QSize logicalImageSize(int slot) const;
    ViewState effectiveViewState(int slot) const;

    Q_INVOKABLE void fitAll();
    Q_INVOKABLE void actualPixelsAll();
    Q_INVOKABLE QVariantMap navigationState(int slot) const;

  signals:
    void presentationModeChanged();
    void compareAmountChanged();
    void dividerPositionChanged();
    void synchronizedChanged();
    void imageCountChanged();
    void navigationRevisionChanged();
    void backgroundColorChanged();
    void viewStateChanged(int slot, const ispview::ViewState& state);
    void pixelHovered(int sourceSlot, const QPoint& pixel, const QColor& color, bool valid);
    void slotActivated(int slot);
    void contextMenuRequested(const QPointF& position);

  protected:
    QQuickRhiItemRenderer* createRenderer() override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void hoverMoveEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;

  private:
    QVector<ImageFramePtr> frames_;
    QVector<ViewState> viewStates_;
    SyncGroup syncGroup_;
    int presentationMode_ = 0;
    qreal compareAmount_ = 0.5;
    bool synchronized_ = true;
    bool dragging_ = false;
    bool dividerDragging_ = false;
    int activeSlot_ = -1;
    QPointF lastMousePosition_;
    int navigationRevision_ = 0;
    QColor backgroundColor_{160, 160, 160};

    QRectF cellRect(int slot) const;
    int slotAt(const QPointF& position) const;
    QPointF normalizedPoint(int slot, const QPointF& position) const;
    void setViewState(int slot, const ViewState& state, bool notify = true,
                      bool synchronizeViews = true);
    void notifyNavigationChanged();
    void emitPixelAt(const QPointF& position);
};

} // namespace ispview
