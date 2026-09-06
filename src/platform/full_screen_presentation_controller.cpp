#include "platform/full_screen_presentation_controller.h"

#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
#include <QGuiApplication>
#endif

#ifdef Q_OS_MACOS
#import <AppKit/AppKit.h>
#endif

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace ispview {

#ifdef Q_OS_MACOS
namespace {

NSWindow* nativeWindowFor(QWindow* window) {
    if (!window)
        return nil;
    auto* view = reinterpret_cast<NSView*>(window->winId());
    return view.window;
}

} // namespace
#endif

#ifdef Q_OS_WIN
struct FullScreenPresentationController::WindowsState {
    HWND nativeWindow = nullptr;
    LONG_PTR style = 0;
    LONG_PTR extendedStyle = 0;
    WINDOWPLACEMENT placement{static_cast<UINT>(sizeof(WINDOWPLACEMENT))};
    RECT frame{};
};

namespace {

bool setWindowStyle(HWND window, int index, LONG_PTR value) {
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR previousValue = SetWindowLongPtrW(window, index, value);
    return previousValue != 0 || GetLastError() == ERROR_SUCCESS;
}

} // namespace

void FullScreenPresentationController::restoreWindowsState(const WindowsState& state) {
    if (!IsWindow(state.nativeWindow))
        return;

    (void)setWindowStyle(state.nativeWindow, GWL_STYLE, state.style);
    (void)setWindowStyle(state.nativeWindow, GWL_EXSTYLE, state.extendedStyle);
    // Make the restored non-client style effective before applying WINDOWPLACEMENT; otherwise
    // Windows can interpret its normal-position rectangle using the temporary borderless frame.
    (void)SetWindowPos(state.nativeWindow, nullptr, 0, 0, 0, 0,
                       SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE |
                           SWP_NOOWNERZORDER | SWP_NOZORDER);
    if (!SetWindowPlacement(state.nativeWindow, &state.placement)) {
        (void)SetWindowPos(state.nativeWindow, nullptr, static_cast<int>(state.frame.left),
                           static_cast<int>(state.frame.top),
                           static_cast<int>(state.frame.right - state.frame.left),
                           static_cast<int>(state.frame.bottom - state.frame.top),
                           SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);
    }
}
#endif

FullScreenPresentationController::FullScreenPresentationController(QObject* parent)
    : QObject(parent) {}

FullScreenPresentationController::~FullScreenPresentationController() { end(); }

bool FullScreenPresentationController::begin(QWindow* window) {
    if (!window)
        return false;
    if (active_)
        return window_ == window;

#ifdef Q_OS_MACOS
    // QML tests use the offscreen platform plugin. Only touch AppKit for the real Cocoa window
    // system so the controller remains deterministic in headless test runs.
    if (QGuiApplication::platformName() == QStringLiteral("cocoa")) {
        NSWindow* nativeWindow = nativeWindowFor(window);
        if (!nativeWindow)
            return false;

        NSApplication* application = [NSApplication sharedApplication];
        previousPresentationOptions_ =
            static_cast<unsigned long long>(application.presentationOptions);
        previousStyleMask_ = static_cast<unsigned long long>(nativeWindow.styleMask);
        previousLevel_ = static_cast<long long>(nativeWindow.level);
        const NSRect frame = nativeWindow.frame;
        previousNativeFrame_ =
            QRectF(frame.origin.x, frame.origin.y, frame.size.width, frame.size.height);
        previousHasShadow_ = nativeWindow.hasShadow;
        previousMovable_ = nativeWindow.movable;
        nativeStateCaptured_ = true;
        previousWindowFlags_ = window->flags();
        window_ = window;

        auto presentationOptions = application.presentationOptions;
        presentationOptions &=
            ~(NSApplicationPresentationAutoHideDock | NSApplicationPresentationAutoHideMenuBar);
        presentationOptions |=
            NSApplicationPresentationHideDock | NSApplicationPresentationHideMenuBar;
        application.presentationOptions = presentationOptions;

        NSScreen* targetScreen = nativeWindow.screen;
        if (!targetScreen)
            targetScreen = NSScreen.mainScreen;
        // Keep Qt's platform-window flags synchronized with Cocoa. Changing only the native
        // style mask makes QNSWindow treat the borderless window as non-key, which breaks Escape
        // and other keyboard shortcuts even though pointer input still works.
        window->setFlag(Qt::FramelessWindowHint, true);
        nativeWindow = nativeWindowFor(window);
        if (!nativeWindow) {
            application.presentationOptions =
                static_cast<NSApplicationPresentationOptions>(previousPresentationOptions_);
            window->setFlags(previousWindowFlags_);
            window_.clear();
            nativeStateCaptured_ = false;
            return false;
        }
        nativeWindow.hasShadow = NO;
        nativeWindow.movable = NO;
        [nativeWindow setFrame:targetScreen.frame display:YES animate:NO];
        [nativeWindow makeKeyAndOrderFront:nil];
        window->requestActivate();
    }
#endif

#ifdef Q_OS_WIN
    // Qt's native full-screen state can animate and briefly expose a second composition of the
    // main page. Resize the existing HWND directly instead. The monitor rectangle deliberately
    // includes the taskbar area, unlike rcWork, so no strip of the desktop remains visible.
    if (QGuiApplication::platformName() == QStringLiteral("windows")) {
        HWND nativeWindow = reinterpret_cast<HWND>(window->winId());
        if (!IsWindow(nativeWindow))
            return false;

        auto state = std::make_unique<WindowsState>();
        state->nativeWindow = nativeWindow;
        state->style = GetWindowLongPtrW(nativeWindow, GWL_STYLE);
        state->extendedStyle = GetWindowLongPtrW(nativeWindow, GWL_EXSTYLE);
        if (!GetWindowPlacement(nativeWindow, &state->placement) ||
            !GetWindowRect(nativeWindow, &state->frame)) {
            return false;
        }

        const HMONITOR monitor = MonitorFromWindow(nativeWindow, MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitorInfo{static_cast<DWORD>(sizeof(MONITORINFO))};
        if (!monitor || !GetMonitorInfoW(monitor, &monitorInfo))
            return false;

        // Preserve WS_MAXIMIZE when the main window was maximized. This keeps Qt's window-state
        // bookkeeping stable and prevents a restore/maximize animation on the way back.
        constexpr LONG_PTR overlappedWindowMask = static_cast<LONG_PTR>(WS_OVERLAPPEDWINDOW);
        constexpr LONG_PTR popupStyle = static_cast<LONG_PTR>(WS_POPUP);
        constexpr LONG_PTR extendedFrameMask = static_cast<LONG_PTR>(
            WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);
        const LONG_PTR borderlessStyle = (state->style & ~overlappedWindowMask) | popupStyle;
        const LONG_PTR borderlessExtendedStyle = state->extendedStyle & ~extendedFrameMask;
        if (!setWindowStyle(nativeWindow, GWL_STYLE, borderlessStyle) ||
            !setWindowStyle(nativeWindow, GWL_EXSTYLE, borderlessExtendedStyle)) {
            restoreWindowsState(*state);
            return false;
        }

        const RECT& bounds = monitorInfo.rcMonitor;
        if (!SetWindowPos(nativeWindow, HWND_TOP, static_cast<int>(bounds.left),
                          static_cast<int>(bounds.top),
                          static_cast<int>(bounds.right - bounds.left),
                          static_cast<int>(bounds.bottom - bounds.top),
                          SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_SHOWWINDOW)) {
            restoreWindowsState(*state);
            return false;
        }

        windowsState_ = std::move(state);
        window_ = window;
        (void)SetForegroundWindow(nativeWindow);
        window->requestActivate();
    }
#endif

    window_ = window;
    active_ = true;
    emit activeChanged();
    return true;
}

void FullScreenPresentationController::end() {
    if (!active_)
        return;

#ifdef Q_OS_MACOS
    if (QGuiApplication::platformName() == QStringLiteral("cocoa")) {
        if (window_ && nativeStateCaptured_)
            window_->setFlags(previousWindowFlags_);
        NSWindow* nativeWindow = nativeWindowFor(window_);
        if (nativeWindow && nativeStateCaptured_) {
            nativeWindow.styleMask = static_cast<NSWindowStyleMask>(previousStyleMask_);
            const NSRect frame =
                NSMakeRect(previousNativeFrame_.x(), previousNativeFrame_.y(),
                           previousNativeFrame_.width(), previousNativeFrame_.height());
            [nativeWindow setFrame:frame display:YES animate:NO];
            nativeWindow.level = static_cast<NSWindowLevel>(previousLevel_);
            nativeWindow.hasShadow = previousHasShadow_;
            nativeWindow.movable = previousMovable_;
            [nativeWindow makeKeyAndOrderFront:nil];
        }

        NSApplication* application = [NSApplication sharedApplication];
        application.presentationOptions =
            static_cast<NSApplicationPresentationOptions>(previousPresentationOptions_);
    }
    nativeStateCaptured_ = false;
#endif

#ifdef Q_OS_WIN
    if (windowsState_) {
        restoreWindowsState(*windowsState_);
        if (window_) {
            (void)SetForegroundWindow(windowsState_->nativeWindow);
            window_->requestActivate();
        }
        windowsState_.reset();
    }
#endif

    window_.clear();
    active_ = false;
    emit activeChanged();
}

} // namespace ispview
