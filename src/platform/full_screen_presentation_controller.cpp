#include "platform/full_screen_presentation_controller.h"

#ifdef Q_OS_MACOS
#import <AppKit/AppKit.h>

#include <QGuiApplication>
#endif

namespace ispview {

#ifdef Q_OS_MACOS
namespace {

NSWindow* nativeWindowFor(QWindow* window) {
    if (!window) return nil;
    auto* view = reinterpret_cast<NSView*>(window->winId());
    return view.window;
}

} // namespace
#endif

FullScreenPresentationController::FullScreenPresentationController(QObject* parent)
    : QObject(parent) {}

FullScreenPresentationController::~FullScreenPresentationController() { end(); }

bool FullScreenPresentationController::begin(QWindow* window) {
    if (!window) return false;
    if (active_) return window_ == window;

#ifdef Q_OS_MACOS
    // QML tests use the offscreen platform plugin. Only touch AppKit for the real Cocoa window
    // system so the controller remains deterministic in headless test runs.
    if (QGuiApplication::platformName() == QStringLiteral("cocoa")) {
        NSWindow* nativeWindow = nativeWindowFor(window);
        if (!nativeWindow) return false;

        NSApplication* application = [NSApplication sharedApplication];
        previousPresentationOptions_ =
            static_cast<unsigned long long>(application.presentationOptions);
        previousStyleMask_ = static_cast<unsigned long long>(nativeWindow.styleMask);
        previousLevel_ = static_cast<long long>(nativeWindow.level);
        const NSRect frame = nativeWindow.frame;
        previousNativeFrame_ = QRectF(frame.origin.x, frame.origin.y,
                                      frame.size.width, frame.size.height);
        previousHasShadow_ = nativeWindow.hasShadow;
        previousMovable_ = nativeWindow.movable;
        nativeStateCaptured_ = true;
        previousWindowFlags_ = window->flags();
        window_ = window;

        auto presentationOptions = application.presentationOptions;
        presentationOptions &= ~(NSApplicationPresentationAutoHideDock |
                                 NSApplicationPresentationAutoHideMenuBar);
        presentationOptions |= NSApplicationPresentationHideDock |
                               NSApplicationPresentationHideMenuBar;
        application.presentationOptions = presentationOptions;

        NSScreen* targetScreen = nativeWindow.screen;
        if (!targetScreen) targetScreen = NSScreen.mainScreen;
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

    window_ = window;
    active_ = true;
    emit activeChanged();
    return true;
}

void FullScreenPresentationController::end() {
    if (!active_) return;

#ifdef Q_OS_MACOS
    if (QGuiApplication::platformName() == QStringLiteral("cocoa")) {
        if (window_ && nativeStateCaptured_)
            window_->setFlags(previousWindowFlags_);
        NSWindow* nativeWindow = nativeWindowFor(window_);
        if (nativeWindow && nativeStateCaptured_) {
            nativeWindow.styleMask =
                static_cast<NSWindowStyleMask>(previousStyleMask_);
            const NSRect frame = NSMakeRect(previousNativeFrame_.x(),
                                            previousNativeFrame_.y(),
                                            previousNativeFrame_.width(),
                                            previousNativeFrame_.height());
            [nativeWindow setFrame:frame display:YES animate:NO];
            nativeWindow.level = static_cast<NSWindowLevel>(previousLevel_);
            nativeWindow.hasShadow = previousHasShadow_;
            nativeWindow.movable = previousMovable_;
            [nativeWindow makeKeyAndOrderFront:nil];
        }

        NSApplication* application = [NSApplication sharedApplication];
        application.presentationOptions = static_cast<NSApplicationPresentationOptions>(
            previousPresentationOptions_);
    }
    nativeStateCaptured_ = false;
#endif

    window_.clear();
    active_ = false;
    emit activeChanged();
}

} // namespace ispview
