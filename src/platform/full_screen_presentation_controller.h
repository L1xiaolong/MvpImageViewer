#pragma once

#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QWindow>

#include <memory>

namespace ispview {

// Turns an existing native window into a borderless full-display window without entering the
// platform's animated full-screen state, then restores its exact placement and decoration.
class FullScreenPresentationController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)

  public:
    explicit FullScreenPresentationController(QObject* parent = nullptr);
    ~FullScreenPresentationController() override;

    [[nodiscard]] bool active() const { return active_; }

    Q_INVOKABLE bool begin(QWindow* window);
    Q_INVOKABLE void end();

  signals:
    void activeChanged();

  private:
    bool active_ = false;
    QPointer<QWindow> window_;
    Qt::WindowFlags previousWindowFlags_;
#ifdef Q_OS_MACOS
    unsigned long long previousPresentationOptions_ = 0;
    unsigned long long previousStyleMask_ = 0;
    long long previousLevel_ = 0;
    QRectF previousNativeFrame_;
    bool previousHasShadow_ = true;
    bool previousMovable_ = true;
    bool nativeStateCaptured_ = false;
#endif
#ifdef Q_OS_WIN
    struct WindowsState;
    static void restoreWindowsState(const WindowsState& state);
    std::unique_ptr<WindowsState> windowsState_;
#endif
};

} // namespace ispview
