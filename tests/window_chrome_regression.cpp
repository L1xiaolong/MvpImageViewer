#include "qml/window_chrome_controller.h"
#include "platform/full_screen_presentation_controller.h"
#include <QGuiApplication>
#include <QQuickWindow>
#include <QRectF>
#include <QVariant>
#include <qt_windows.h>
#include <windowsx.h>
#include <cstdio>

int main(int argc, char** argv) {
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext&, const QString& message) {
        std::fprintf(stderr, "%s\n", qPrintable(message));
        std::fflush(stderr);
    });
    QGuiApplication app(argc, argv);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    mvpview::WindowChromeController chrome;
    if (!chrome.supported()) return 0;
    QQuickWindow window;
    window.setFlags(Qt::Window | Qt::CustomizeWindowHint | Qt::WindowSystemMenuHint
        | Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint
        | Qt::WindowCloseButtonHint | Qt::ExpandedClientAreaHint | Qt::NoTitleBarBackgroundHint);
    window.setTitle(QStringLiteral("Unified title bar regression"));
    window.resize(980, 640);
    window.setProperty("unifiedTitleBarHeight", 44);
    window.setProperty("titleBarLeftControls", QRectF(0, 0, 420, 44));
    window.setProperty("titleBarRightControls", QRectF(650, 0, 180, 44));
    window.showNormal();
    int closeCount = 0;
    QObject::connect(&window, &QQuickWindow::closing, &app, [&] { ++closeCount; });
    QCoreApplication::processEvents();
    chrome.attach(&window);
    const auto hwnd = reinterpret_cast<HWND>(window.winId());
    auto hit = [&](int x, int y) {
        POINT point{qRound(x * window.devicePixelRatio()), qRound(y * window.devicePixelRatio())};
        ClientToScreen(hwnd, &point);
        return SendMessageW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(point.x, point.y));
    };
    auto require = [](bool ok, const char* message) { if (!ok) qFatal("%s", message); };
    require(hit(100, 20) == HTCLIENT, "Toolbar controls must receive clicks");
    require(hit(700, 20) == HTCLIENT, "Search must receive clicks");
    require(hit(500, 20) == HTCAPTION, "Blank header must be a native drag region");
    require(hit(1, 1) == HTTOPLEFT, "Resize corners must remain native");
    const auto width = chrome.captionButtonsWidth();
    require(width > 100 && width < 200, "Caption reserve must stay in logical pixels");
    require(hit(980 - qRound(width / 2), 20) == HTMAXBUTTON,
            "Maximize hover must expose the native Snap Layout hit target");
    require(hit(965, 20) == HTCLOSE, "Close must retain its native hit target");
    auto captionClick = [&](int index) {
        POINT point{qRound((window.width() - 138 + 46 * index + 23) * window.devicePixelRatio()),
                    qRound(22 * window.devicePixelRatio())};
        POINT screenPoint = point;
        ClientToScreen(hwnd, &screenPoint);
        const int target = index == 0 ? HTMINBUTTON : index == 1 ? HTMAXBUTTON : HTCLOSE;
        SendMessageW(hwnd, WM_NCMOUSEMOVE, target, MAKELPARAM(screenPoint.x, screenPoint.y));
        if (chrome.hoveredCaptionButton() != index)
            qInfo("Hover target=%d actual=%d window=%dx%d point=%ld,%ld", index,
                chrome.hoveredCaptionButton(), window.width(), window.height(), point.x, point.y);
        require(chrome.hoveredCaptionButton() == index, "Native hover must update the QML caption");
        SendMessageW(hwnd, WM_NCLBUTTONDOWN, target, MAKELPARAM(screenPoint.x, screenPoint.y));
        require(chrome.pressedCaptionButton() == index, "Caption press must be visible");
        SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(point.x, point.y));
        QCoreApplication::processEvents();
        require(closeCount == 0, "Minimize/maximize clicks must never close the window");
    };
    for (int cycle = 0; cycle < 3; ++cycle) {
        captionClick(1);
        require(IsZoomed(hwnd), "Caption click must maximize");
        captionClick(1);
        require(!IsZoomed(hwnd), "Caption click must restore");
        captionClick(0);
        require(IsIconic(hwnd), "Caption click must minimize without closing");
        window.showNormal();
        QCoreApplication::processEvents();
    }
    SendMessageW(hwnd, WM_NCMOUSELEAVE, 0, 0);
    require(chrome.hoveredCaptionButton() == -1, "Caption hover must clear on leave");
    SendMessageW(hwnd, WM_SYSCOMMAND, SC_MAXIMIZE, 0);
    QCoreApplication::processEvents();
    require(IsZoomed(hwnd), "Native maximize must work");
    require(hit(500, 1) == HTCAPTION, "Maximized top edge must allow drag-to-restore");
    SendMessageW(hwnd, WM_SYSCOMMAND, SC_RESTORE, 0);
    QCoreApplication::processEvents();
    require(!IsZoomed(hwnd), "Native restore must work");
    SendMessageW(hwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
    require(IsIconic(hwnd), "Native minimize must work");
    SendMessageW(hwnd, WM_SYSCOMMAND, SC_RESTORE, 0);
    QCoreApplication::processEvents();
    mvpview::FullScreenPresentationController fullscreen;
    QObject::connect(&fullscreen, &mvpview::FullScreenPresentationController::activeChanged,
                     &chrome, [&] { chrome.setSuspended(fullscreen.active()); });
    require(fullscreen.begin(&window), "Instant fullscreen must open");
    require(hit(500, 20) == HTCLIENT, "Fullscreen must have no caption hit regions");
    fullscreen.end();
    require(hit(500, 20) == HTCAPTION, "Exiting fullscreen must restore the header");
    window.close();
    qInfo("Unified chrome: controls, drag, resize, native caption, maximize, minimize, fullscreen passed");
    return 0;
}
