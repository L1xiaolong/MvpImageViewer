#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QPointer>
#include <QWindow>

namespace mvpview {

// Qt owns the extended client area; QML paints the caption. Native hit targets
// preserve Windows snap/drag gestures without mixing two caption click handlers.
class WindowChromeController final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
    Q_PROPERTY(bool supported READ supported CONSTANT)
    Q_PROPERTY(qreal captionButtonsWidth READ captionButtonsWidth NOTIFY metricsChanged)
    Q_PROPERTY(int hoveredCaptionButton READ hoveredCaptionButton NOTIFY captionStateChanged)
    Q_PROPERTY(int pressedCaptionButton READ pressedCaptionButton NOTIFY captionStateChanged)
public:
    explicit WindowChromeController(QObject* parent = nullptr);
    ~WindowChromeController() override;
    bool supported() const;
    qreal captionButtonsWidth() const;
    int hoveredCaptionButton() const { return hoveredButton_; }
    int pressedCaptionButton() const { return pressedButton_; }
    Q_INVOKABLE void activateCaptionButton(int index);
    void attach(QWindow* window);
    void setSuspended(bool suspended);
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;
signals:
    void metricsChanged();
    void captionStateChanged();
private:
    QPointer<QWindow> window_;
    quintptr nativeHandle_ = 0;
    bool suspended_ = false;
    int hoveredButton_ = -1;
    int pressedButton_ = -1;
    void setCaptionState(int hovered, int pressed);
};

} // namespace mvpview
