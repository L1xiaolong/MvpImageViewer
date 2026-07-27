#include "io/default_image_decoder.h"
#include "qml/browse_controller.h"
#include "qml/browse_workspace_controller.h"
#include "qml/compare_controller.h"
#include "qml/image_properties_controller.h"
#include "qml/full_screen_controller.h"
#include "qml/raw_parameters_controller.h"
#include "qml/thumbnail_image_provider.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSettings>
#include <QTimer>
#include <QUrl>

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("ISP Image Viewer"));
    QCoreApplication::setOrganizationName(QStringLiteral("ISPView"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.2.3"));
    app.setWindowIcon(QIcon(QStringLiteral(":/brand/app_icon.png")));
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QSettings settings;
    bool lastMainWindowStateWasMaximized =
        settings.value(QStringLiteral("window/maximized"), false).toBool();

    QFont defaultFont = app.font();
    defaultFont.setWeight(QFont::Medium);
    app.setFont(defaultFont);

    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/Inter-Regular.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/Inter-Bold.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/JetBrainsMono-Regular.ttf"));

    QString initialDirectory;
    QString screenshotPath;
    QString selectedPath;
    QStringList initialComparePaths;
    QString displayMode;
    int initialFileManagerCount = 1;
    bool nativeScreenshot = false;
    int screenshotDelay = 1800;
    const QStringList arguments = app.arguments();
    for (int i = 1; i < arguments.size(); ++i) {
        if (arguments.at(i) == QStringLiteral("--screenshot") && i + 1 < arguments.size()) {
            screenshotPath = arguments.at(++i);
        } else if (arguments.at(i) == QStringLiteral("--screenshot-native")) {
            nativeScreenshot = true;
        } else if (arguments.at(i) == QStringLiteral("--select") && i + 1 < arguments.size()) {
            selectedPath = QFileInfo(arguments.at(++i)).absoluteFilePath();
        } else if (arguments.at(i) == QStringLiteral("--display-mode") &&
                   i + 1 < arguments.size()) {
            displayMode = arguments.at(++i).toLower();
        } else if (arguments.at(i) == QStringLiteral("--screenshot-delay") &&
                   i + 1 < arguments.size()) {
            screenshotDelay = qMax(250, arguments.at(++i).toInt());
        } else if (arguments.at(i) == QStringLiteral("--file-managers") &&
                   i + 1 < arguments.size()) {
            initialFileManagerCount = qBound(1, arguments.at(++i).toInt(), 4);
        } else if (arguments.at(i) == QStringLiteral("--qml-compare")) {
            while (i + 1 < arguments.size() && !arguments.at(i + 1).startsWith(QLatin1Char('-'))) {
                initialComparePaths.append(QFileInfo(arguments.at(++i)).absoluteFilePath());
            }
        } else if (!arguments.at(i).startsWith(QLatin1Char('-'))) {
            initialDirectory = arguments.at(i);
        }
    }
    // The browse page can use the software scene graph for deterministic CI
    // captures. ComparePage contains a QQuickRhiItem and therefore must keep a
    // hardware RHI backend even when --screenshot-native was not specified.
    if (!screenshotPath.isEmpty() && !nativeScreenshot && initialComparePaths.isEmpty()) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    }

    const auto decoder = ispview::createDefaultImageDecoder();
    // Keep filesystem restoration out of the pre-window startup path. On a cold first launch,
    // QML and shader caches are also being populated; touching a slow or unavailable previous
    // directory here can otherwise prevent the first frame from appearing at all.
    ispview::BrowseWorkspaceController browseController(decoder, initialDirectory, true);
    while (browseController.paneCount() < initialFileManagerCount)
        browseController.addFileManagerPane();
    ispview::CompareController compareController(browseController.loader());
    ispview::ImagePropertiesController imagePropertiesController(browseController.loader());
    ispview::FullScreenController fullScreenController(browseController.loader());
    ispview::RawParametersController rawParametersController(browseController.loader());

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("browseController"), &browseController);
    engine.rootContext()->setContextProperty(QStringLiteral("compareController"), &compareController);
    engine.rootContext()->setContextProperty(QStringLiteral("imagePropertiesController"),
                                             &imagePropertiesController);
    engine.rootContext()->setContextProperty(QStringLiteral("fullScreenController"),
                                             &fullScreenController);
    engine.rootContext()->setContextProperty(QStringLiteral("rawParametersController"),
                                             &rawParametersController);
    engine.rootContext()->setContextProperty(QStringLiteral("initialComparePaths"), initialComparePaths);
    engine.addImageProvider(QStringLiteral("thumbnail"),
                            new ispview::ThumbnailImageProvider(decoder,
                                                                browseController.loader()));
#ifdef Q_OS_WIN
    engine.addImageProvider(QStringLiteral("system-folder"),
                            new ispview::SystemFolderIconProvider());
#endif
    engine.load(QUrl(QStringLiteral("qrc:/ISPViewQml/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }
    auto* mainWindow = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    if (!mainWindow) {
        return 1;
    }
    // The hidden full-screen helper Window means quitOnLastWindowClosed cannot be the owner of
    // application shutdown. QML emits this signal only for a real application close; closing a
    // compare/full-screen session is deliberately kept inside QML.
    QObject::connect(mainWindow, SIGNAL(quitApplicationRequested()), &app, SLOT(quit()));
    if (lastMainWindowStateWasMaximized) {
        mainWindow->showMaximized();
    } else {
        mainWindow->showNormal();
    }
    // Remember the most recent stable state so closing from the taskbar still restores to
    // normal/maximized instead of starting minimized.
    // The QML scene owns an additional hidden full-screen Window on Windows, so Qt's
    // quitOnLastWindowClosed mechanism cannot reliably terminate the event loop when the main
    // window is closed. The Hidden fallback is gated by applicationExitPending so rejecting a
    // compare/full-screen close can never terminate the process.
    QObject::connect(
        mainWindow, &QWindow::visibilityChanged, &app,
        [mainWindow, &lastMainWindowStateWasMaximized](QWindow::Visibility visibility) {
#ifdef Q_OS_WIN
            if (visibility == QWindow::Hidden
                && mainWindow->property("applicationExitPending").toBool()) {
                QCoreApplication::quit();
                return;
            }
#endif
            if (visibility == QWindow::Maximized) {
                lastMainWindowStateWasMaximized = true;
            } else if (visibility == QWindow::Windowed) {
                lastMainWindowStateWasMaximized = false;
            }
        });
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app,
                     [&lastMainWindowStateWasMaximized] {
                         QSettings().setValue(QStringLiteral("window/maximized"),
                                              lastMainWindowStateWasMaximized);
                     });
    QTimer::singleShot(250, &browseController,
                       [&browseController] { browseController.startDeferredInitialDirectory(); });

    if (!displayMode.isEmpty()) {
        const int mode = displayMode == QStringLiteral("list")
                             ? 1
                             : displayMode == QStringLiteral("gallery") ? 2 : 0;
        browseController.setActiveDisplayMode(mode);
    }

    if (!selectedPath.isEmpty()) {
        QTimer::singleShot(700, &browseController,
                           [&browseController, selectedPath] {
                               if (auto* pane = browseController.activeBrowsePane())
                                   pane->selectPath(selectedPath);
                           });
    }

    if (!screenshotPath.isEmpty()) {
        QTimer::singleShot(screenshotDelay, &app, [&app, &engine, screenshotPath] {
            auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
            if (!window || !window->grabWindow().save(screenshotPath)) {
                app.exit(2);
                return;
            }
            window->setProperty("forceApplicationClose", true);
            app.quit();
        });
    }
    return app.exec();
}
