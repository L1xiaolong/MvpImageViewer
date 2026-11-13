#include "window_chrome_controller.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QRectF>
#include <QVariant>
#include <QtMath>
#include <QTimer>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <windowsx.h>
#include <commctrl.h>
#endif

namespace mvpview {

#ifdef Q_OS_WIN
static LRESULT CALLBACK chromeWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                         UINT_PTR subclassId, DWORD_PTR data) {
    auto* chrome = reinterpret_cast<WindowChromeController*>(data);
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, chromeWindowProc, subclassId);
        return DefSubclassProc(hwnd, message, wParam, lParam);
    }
    MSG msg{};
    msg.hwnd = hwnd;
    msg.message = message;
    msg.wParam = wParam;
    msg.lParam = lParam;
    qintptr result = 0;
    if (chrome->nativeEventFilter({}, &msg, &result)) return result;
    return DefSubclassProc(hwnd, message, wParam, lParam);
}
#endif

WindowChromeController::WindowChromeController(QObject* parent) : QObject(parent) {}

WindowChromeController::~WindowChromeController() {
#ifdef Q_OS_WIN
    if (nativeHandle_ && IsWindow(reinterpret_cast<HWND>(nativeHandle_)))
        RemoveWindowSubclass(reinterpret_cast<HWND>(nativeHandle_), chromeWindowProc,
                             reinterpret_cast<UINT_PTR>(this));
#endif
}

bool WindowChromeController::supported() const {
#ifdef Q_OS_WIN
    return QGuiApplication::platformName() == QStringLiteral("windows");
#else
    return false;
#endif
}

qreal WindowChromeController::captionButtonsWidth() const {
    return supported() ? 138 : 0;
}

void WindowChromeController::attach(QWindow* window) {
    window_ = window;
    nativeHandle_ = static_cast<quintptr>(window->winId());
#ifdef Q_OS_WIN
    if (supported()) {
        // Qt can consume non-client mouse moves before application native filters.
        // Subclass this HWND so hover, press and release all reach one handler.
        SetWindowSubclass(reinterpret_cast<HWND>(nativeHandle_), chromeWindowProc,
                          reinterpret_cast<UINT_PTR>(this), reinterpret_cast<DWORD_PTR>(this));
    }
#endif
    connect(window, &QWindow::screenChanged, this, &WindowChromeController::metricsChanged);
    setSuspended(false);
    emit metricsChanged();
}

void WindowChromeController::setSuspended(bool suspended) {
    suspended_ = suspended;
    setCaptionState(-1, -1);
#ifdef Q_OS_WIN
    if (!window_ || !supported()) return;
    // ExpandedClientAreaHint also creates Qt's own caption overlay. It must stay
    // hidden: the QML caption has the same 44px geometry as the toolbar.
    EnumChildWindows(reinterpret_cast<HWND>(nativeHandle_), [](HWND child, LPARAM) -> BOOL {
        constexpr LONG_PTR captionStyle = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
        if ((GetWindowLongPtrW(child, GWL_EXSTYLE) & captionStyle) == captionStyle)
            ShowWindow(child, SW_HIDE);
        return TRUE;
    }, 0);
#endif
}

void WindowChromeController::setCaptionState(int hovered, int pressed) {
    if (hoveredButton_ == hovered && pressedButton_ == pressed) return;
    hoveredButton_ = hovered;
    pressedButton_ = pressed;
    emit captionStateChanged();
}

void WindowChromeController::activateCaptionButton(int index) {
    // Resize/show operations must run after the native input callback has returned.
    QTimer::singleShot(0, this, [this, index] {
        if (!window_ || suspended_) return;
        setCaptionState(-1, -1);
        if (index == 0) window_->showMinimized();
        else if (index == 1) {
            if (window_->visibility() == QWindow::Maximized) window_->showNormal();
            else window_->showMaximized();
        } else if (index == 2) window_->close();
    });
}

bool WindowChromeController::nativeEventFilter(const QByteArray&, void* message, qintptr* result) {
#ifdef Q_OS_WIN
    if (!window_ || !supported()) return false;
    auto* msg = static_cast<MSG*>(message);
    const auto hwnd = reinterpret_cast<HWND>(nativeHandle_);
    if (msg->hwnd != hwnd) return false;
    if (msg->message == WM_DPICHANGED) emit metricsChanged();
    const bool fullscreen = suspended_ || window_->visibility() == QWindow::FullScreen;
    const auto captionAt = [&](POINT point) {
        RECT bounds{};
        GetClientRect(hwnd, &bounds);
        const qreal scale = window_->devicePixelRatio();
        const qreal x = point.x / scale;
        const qreal y = point.y / scale;
        const qreal width = bounds.right / scale;
        if (fullscreen || y < 0 || y >= window_->property("unifiedTitleBarHeight").toReal()
            || x < width - captionButtonsWidth() || x >= width) return -1;
        return qBound(0, static_cast<int>((x - width + captionButtonsWidth()) / 46), 2);
    };
    const auto screenPoint = [&] {
        POINT point{GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam)};
        ScreenToClient(hwnd, &point);
        return point;
    };
    if (msg->message == WM_NCHITTEST) {
        if (fullscreen) { *result = HTCLIENT; return true; }
        POINT point{GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam)};
        ScreenToClient(hwnd, &point);
        RECT bounds{};
        GetClientRect(hwnd, &bounds);
        const UINT dpi = GetDpiForWindow(hwnd);
        const int border = IsZoomed(hwnd) ? 0 : GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi)
                                                + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
        const bool left = point.x < border, right = point.x >= bounds.right - border;
        const bool top = point.y < border, bottom = point.y >= bounds.bottom - border;
        if (left || right || top || bottom) {
            *result = left ? (top ? HTTOPLEFT : bottom ? HTBOTTOMLEFT : HTLEFT)
                : right ? (top ? HTTOPRIGHT : bottom ? HTBOTTOMRIGHT : HTRIGHT)
                : top ? HTTOP : HTBOTTOM;
            return true;
        }
        const int button = captionAt(point);
        if (button >= 0) {
            *result = button == 0 ? HTMINBUTTON : button == 1 ? HTMAXBUTTON : HTCLOSE;
            return true;
        }
        const QPointF local(point.x / window_->devicePixelRatio(), point.y / window_->devicePixelRatio());
        if (local.y() < window_->property("unifiedTitleBarHeight").toReal()) {
            for (const char* name : {"titleBarLeftControls", "titleBarRightControls"}) {
                if (window_->property(name).toRectF().contains(local)) { *result = HTCLIENT; return true; }
            }
            *result = HTCAPTION;
            return true;
        }
        *result = HTCLIENT;
        return true;
    }
    if (msg->message == WM_NCMOUSEMOVE && !fullscreen) {
        setCaptionState(captionAt(screenPoint()), pressedButton_);
        TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT), TME_LEAVE | TME_NONCLIENT, hwnd, 0};
        TrackMouseEvent(&tracking);
        // HTMAXBUTTON remains visible to Windows; prevent default caption painting
        // from competing with the QML hover/press state.
        *result = 0;
        return true;
    }
    if (msg->message == WM_NCMOUSELEAVE) {
        setCaptionState(-1, pressedButton_);
        *result = 0;
        return true;
    }
    if (msg->message == WM_NCLBUTTONDOWN && !fullscreen) {
        const int button = captionAt(screenPoint());
        if (button >= 0) {
            setCaptionState(button, button);
            SetCapture(hwnd);
            *result = 0;
            return true;
        }
    }
    if (pressedButton_ >= 0 && msg->message == WM_MOUSEMOVE) {
        POINT point{GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam)};
        setCaptionState(captionAt(point), pressedButton_);
        *result = 0;
        return true;
    }
    if (pressedButton_ >= 0 && (msg->message == WM_LBUTTONUP || msg->message == WM_NCLBUTTONUP)) {
        const int pressed = pressedButton_;
        const POINT point = msg->message == WM_NCLBUTTONUP ? screenPoint()
            : POINT{GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam)};
        const bool activate = captionAt(point) == pressed;
        setCaptionState(-1, -1);
        ReleaseCapture();
        if (activate) activateCaptionButton(pressed);
        *result = 0;
        return true;
    }
    if (msg->message == WM_CAPTURECHANGED || msg->message == WM_CANCELMODE) {
        setCaptionState(-1, -1);
    }
    // Only real caption drags/resize/system-menu messages reach DefWindowProc.
    // Caption button input is consumed above and never triggers Qt's synthetic clicks.
    if (!fullscreen && (msg->message == WM_NCLBUTTONDOWN || msg->message == WM_NCLBUTTONUP
        || msg->message == WM_NCLBUTTONDBLCLK || msg->message == WM_NCRBUTTONUP)) {
        if (msg->wParam == HTMINBUTTON || msg->wParam == HTMAXBUTTON || msg->wParam == HTCLOSE) {
            *result = 0;
            return true;
        }
        *result = DefWindowProcW(hwnd, msg->message, msg->wParam, msg->lParam);
        return true;
    }
#else
    Q_UNUSED(message);
    Q_UNUSED(result);
#endif
    return false;
}

} // namespace mvpview
