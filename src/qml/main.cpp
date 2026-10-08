#include "diagnostics/diagnostics.h"
#include "diagnostics/diagnostics_controller.h"
#include <QStandardPaths>
#include "io/default_image_decoder.h"
#include "core/performance_trace.h"
#include "browser/file_clipboard.h"
#include "platform/full_screen_presentation_controller.h"
#include "qml/app_settings.h"
#include "qml/browse_controller.h"
#include "qml/browse_workspace_controller.h"
#include "qml/compare_controller.h"
#include "qml/image_properties_controller.h"
#include "qml/full_screen_controller.h"
#include "qml/raw_parameters_controller.h"
#include "qml/thumbnail_image_provider.h"

#include <QCoreApplication>
#include <algorithm>
#include <QDir>
#include <QFileInfo>
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

static int runApplication(int argc, char* argv[], mvpview::diagnostics::Service& diagnosticService) {
    QGuiApplication app(argc, argv);
    mvpview::FileClipboard::initialize();
#ifdef Q_OS_WIN
    // Qt defaults to D3D11 on Windows. Some Intel drivers crash while Qt Quick creates
    // or migrates RHI resources during full-screen and cross-monitor transitions.
    // OpenGL avoids that driver path while preserving hardware-accelerated rendering.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
#endif
    QGuiApplication::setQuitOnLastWindowClosed(false);
    QCoreApplication::setApplicationName(QStringLiteral("MVP Image Viewer"));
    QCoreApplication::setOrganizationName(QStringLiteral("MvpView"));
    QCoreApplication::setApplicationVersion(QStringLiteral(MVPVIEW_PROJECT_VERSION));
#ifndef Q_OS_MACOS
    // macOS should keep the icon declared by CFBundleIconFile for the whole
    // application lifetime. Setting the generic runtime PNG here replaces the
    // padded macOS artwork in the Dock after launch.
    app.setWindowIcon(QIcon(QStringLiteral(":/brand/app_icon.png")));
#endif
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QSettings settings;
    mvpview::AppSettings appSettings(&app);
    mvpview::DiagnosticsController diagnosticsController(diagnosticService);
    QObject::connect(&appSettings, &mvpview::AppSettings::diagnosticsChanged, &app, [&] {
        diagnosticService.configure(appSettings.loggingEnabled(), mvpview::diagnostics::parseLevel(appSettings.logLevel()));
    });
    bool lastMainWindowStateWasMaximized =
        settings.value(QStringLiteral("window/maximized"), false).toBool();

    QString initialDirectory;
    QString screenshotPath;
    QString selectedPath;
    QStringList initialComparePaths;
    QString displayMode;
    int initialFileManagerCount = 1;
    QString performanceScenario;
    bool nativeScreenshot = false;
    bool showSettings = false;
    int settingsStartupSection = 0;
    int screenshotDelay = 1800;
    const QStringList arguments = app.arguments();
    for (int i = 1; i < arguments.size(); ++i) {
        if (arguments.at(i) == QStringLiteral("--perf-scenario") && i + 1 < arguments.size()) {
            performanceScenario = arguments.at(++i);
        } else if (arguments.at(i) == QStringLiteral("--screenshot") && i + 1 < arguments.size()) {
            screenshotPath = arguments.at(++i);
        } else if (arguments.at(i) == QStringLiteral("--screenshot-native")) {
            nativeScreenshot = true;
        } else if (arguments.at(i) == QStringLiteral("--show-settings")) {
            showSettings = true;
        } else if (arguments.at(i) == QStringLiteral("--show-diagnostics-settings")) {
            showSettings = true;
            settingsStartupSection = 6;
        } else if (arguments.at(i) == QStringLiteral("--show-appearance-settings")) {
            showSettings = true;
            settingsStartupSection = 1;
        } else if (arguments.at(i) == QStringLiteral("--show-shortcuts-settings")) {
            showSettings = true;
            settingsStartupSection = 3;
        } else if (arguments.at(i) == QStringLiteral("--show-color-settings")) {
            showSettings = true;
            settingsStartupSection = 2;
        } else if (arguments.at(i) == QStringLiteral("--show-update-settings")) {
            showSettings = true;
            settingsStartupSection = 4;
        } else if (arguments.at(i) == QStringLiteral("--show-help-settings")) {
            showSettings = true;
            settingsStartupSection = 5;
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
    if (initialDirectory.isEmpty() && !appSettings.restoreLastDirectory()) {
        initialDirectory = QDir::homePath();
    }
    // The browse page can use the software scene graph for deterministic CI
    // captures. ComparePage contains a QQuickRhiItem and therefore must keep a
    // hardware RHI backend even when --screenshot-native was not specified.
    if (!screenshotPath.isEmpty() && !nativeScreenshot && initialComparePaths.isEmpty()) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    }

    const auto decoder = mvpview::createDefaultImageDecoder();
    // Keep filesystem restoration out of the pre-window startup path. On a cold first launch,
    // QML and shader caches are also being populated; touching a slow or unavailable previous
    // directory here can otherwise prevent the first frame from appearing at all.
    mvpview::BrowseWorkspaceController browseController(decoder, initialDirectory, true);
    while (browseController.paneCount() < initialFileManagerCount)
        browseController.addFileManagerPane();
    mvpview::CompareController compareController(browseController.loader());
    mvpview::ImagePropertiesController imagePropertiesController(browseController.loader());
    mvpview::FullScreenController fullScreenController(browseController.loader());
    mvpview::FullScreenPresentationController fullScreenPresentationController;
    mvpview::RawParametersController rawParametersController(browseController.loader());
    QObject::connect(&appSettings, &mvpview::AppSettings::colorDisplayChanged, &app, [&] {
        mvpview::diagnostics::event(mvpview::diagnostics::Level::Info, mvpview::diagnostics::settings(), QStringLiteral("settings.color_display"),
            {{"colorProfiles", appSettings.applyEmbeddedColorProfiles()}, {"highBitDepth", appSettings.preserveHighBitDepth()},
             {"displayColorSpace", appSettings.displayColorSpace()},
             {"exifOrientation", appSettings.honorExifOrientation()}}, true);
        browseController.loader()->clearCache();
        browseController.refreshAll();
        fullScreenController.reload();
        compareController.reload();
    });

    QQmlApplicationEngine engine;
    QObject::connect(&appSettings, &mvpview::AppSettings::languageChanged,
                     &engine, &QQmlApplicationEngine::retranslate);
    engine.rootContext()->setContextProperty(QStringLiteral("appSettings"), &appSettings);
    engine.rootContext()->setContextProperty(QStringLiteral("diagnosticsController"), &diagnosticsController);
    engine.rootContext()->setContextProperty(QStringLiteral("systemUiFontFamily"),
                                             app.font().family());
    engine.rootContext()->setContextProperty(
        QStringLiteral("systemFixedFontFamily"),
        QFontDatabase::systemFont(QFontDatabase::FixedFont).family());
    engine.rootContext()->setContextProperty(QStringLiteral("browseController"), &browseController);
    engine.rootContext()->setContextProperty(QStringLiteral("compareController"), &compareController);
    engine.rootContext()->setContextProperty(QStringLiteral("imagePropertiesController"),
                                             &imagePropertiesController);
    engine.rootContext()->setContextProperty(QStringLiteral("fullScreenController"),
                                             &fullScreenController);
    engine.rootContext()->setContextProperty(QStringLiteral("fullScreenPresentationController"),
                                             &fullScreenPresentationController);
    engine.rootContext()->setContextProperty(QStringLiteral("rawParametersController"),
                                             &rawParametersController);
    engine.rootContext()->setContextProperty(QStringLiteral("initialComparePaths"), initialComparePaths);
    engine.rootContext()->setContextProperty(QStringLiteral("showSettingsOnStartup"), showSettings);
    engine.rootContext()->setContextProperty(QStringLiteral("settingsStartupSection"),
                                             settingsStartupSection);
    engine.addImageProvider(QStringLiteral("thumbnail"),
                            new mvpview::ThumbnailImageProvider(decoder,
                                                                browseController.loader()));
#ifdef Q_OS_WIN
    engine.addImageProvider(QStringLiteral("system-folder"),
                            new mvpview::SystemFolderIconProvider());
#endif
    mvpview::diagnostics::event(mvpview::diagnostics::Level::Info, mvpview::diagnostics::startup(), QStringLiteral("qml.loading"), {}, true);
    engine.load(QUrl(QStringLiteral("qrc:/MvpViewQml/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }
    auto* mainWindow = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    if (!mainWindow) {
        return 1;
    }
    QObject::connect(mainWindow, &QQuickWindow::sceneGraphInitialized, mainWindow, [mainWindow] {
        mvpview::diagnostics::event(mvpview::diagnostics::Level::Info, mvpview::diagnostics::render(), QStringLiteral("scenegraph.initialized"),
            {{"graphicsApi", static_cast<int>(mainWindow->rendererInterface()->graphicsApi())}}, true);
    }, Qt::DirectConnection);
    // The canvas buffer is interpreted in the colour space the platform reports for the window
    // surface, so "auto" has to follow it. The report is only populated once the platform window
    // exists, and these callbacks may arrive from the render thread, so they are queued to the
    // main thread through the settings object.
    const auto applySurfaceColorSpace = [mainWindow, &appSettings] {
        appSettings.setSurfaceColorSpace(mainWindow->format().colorSpace());
    };
    QObject::connect(mainWindow, &QQuickWindow::sceneGraphInitialized, &appSettings,
                     [applySurfaceColorSpace] { applySurfaceColorSpace(); });
    mainWindow->create();
    applySurfaceColorSpace();
    QObject::connect(mainWindow, &QWindow::screenChanged, mainWindow, [applySurfaceColorSpace] {
        mvpview::diagnostics::event(mvpview::diagnostics::Level::Info, mvpview::diagnostics::render(), QStringLiteral("window.screen_changed"), {}, true);
        applySurfaceColorSpace();
    });
    QObject::connect(mainWindow, &QWindow::visibilityChanged, mainWindow, [](QWindow::Visibility visibility) {
        mvpview::diagnostics::event(mvpview::diagnostics::Level::Info, mvpview::diagnostics::render(), QStringLiteral("window.visibility"), {{"visibility", static_cast<int>(visibility)}}, true);
    });
    // QML emits this signal only for a real application close; closing a compare/full-screen
    // session is deliberately kept inside QML.
    QObject::connect(mainWindow, SIGNAL(quitApplicationRequested()), &app, SLOT(quit()),
                     Qt::QueuedConnection);
    if (lastMainWindowStateWasMaximized) {
        mainWindow->showMaximized();
    } else {
        mainWindow->showNormal();
    }
    // Remember the most recent stable state so closing from the taskbar still restores to
    // normal/maximized instead of starting minimized.
    // The Hidden fallback is gated by applicationExitPending so rejecting a compare/full-screen
    // close can never terminate the process.
    QObject::connect(
        mainWindow, &QWindow::visibilityChanged, &app,
        [mainWindow, &lastMainWindowStateWasMaximized](QWindow::Visibility visibility) {
#ifndef Q_OS_WIN
            Q_UNUSED(mainWindow);
#endif
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
    QObject::connect(&app, &QCoreApplication::aboutToQuit, mainWindow, [mainWindow] {
        // Make any transient helper window accept shutdown so none can strand the event loop
        // after the main window has disappeared.
        mainWindow->setProperty("forceApplicationClose", true);
        mainWindow->setProperty("applicationExitPending", true);
        const auto windows = QGuiApplication::allWindows();
        for (QWindow* window : windows) {
            if (window != mainWindow) window->close();
        }
    });
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app,
                     [&lastMainWindowStateWasMaximized] {
                         if (mvpview::AppSettings::softwareCacheDeletionRequested())
                             return;
                         QSettings().setValue(QStringLiteral("window/maximized"),
                                              lastMainWindowStateWasMaximized);
                     });
    auto firstFrameConnection = std::make_shared<QMetaObject::Connection>();
    *firstFrameConnection = QObject::connect(mainWindow, &QQuickWindow::frameSwapped, &browseController,
        [&browseController, firstFrameConnection, initialDirectory, performanceScenario, displayMode] {
            QObject::disconnect(*firstFrameConnection);
            mvpview::performance::mark(QStringLiteral("startup.first_frame"));
            browseController.startDeferredInitialDirectory();
            if (mvpview::performance::enabled() && performanceScenario == QStringLiteral("scroll")) {
                const auto panes = browseController.panes();
                for (int index = 0; index < panes.size(); ++index) {
                    auto* pane = qobject_cast<mvpview::BrowseController*>(panes.at(index).value<QObject*>());
                    if (!pane) continue;
                    if (index > 0) pane->openDirectory(initialDirectory);
                    pane->setDisplayMode(displayMode == QStringLiteral("list") ? 1 :
                                         displayMode == QStringLiteral("gallery") && panes.size() == 1 ? 2 : 0);
                }
            }
        }, Qt::QueuedConnection);
    if (mvpview::performance::enabled()) {
        auto* heartbeat = new QTimer(&app);
        heartbeat->setInterval(16);
        auto lastTick = std::make_shared<QElapsedTimer>(); lastTick->start();
        QObject::connect(heartbeat, &QTimer::timeout, &app, [lastTick] {
            const qint64 gap = lastTick->restart();
            if (gap > 50) mvpview::performance::mark(QStringLiteral("ui.stall"), {{"gapMs", gap}});
        });
        heartbeat->start();
        auto lastFrame = std::make_shared<QElapsedTimer>(); lastFrame->start();
        QObject::connect(mainWindow, &QQuickWindow::frameSwapped, &app, [lastFrame] {
            mvpview::performance::mark(QStringLiteral("ui.frame"), {{"intervalMs", lastFrame->restart()}});
        }, Qt::QueuedConnection);
    }

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

    if (mvpview::performance::enabled() && !performanceScenario.isEmpty()) {
        QTimer::singleShot(1000, mainWindow, [mainWindow, performanceScenario, selectedPath] {
            if (performanceScenario == QStringLiteral("fullscreen") && !selectedPath.isEmpty()) {
                QMetaObject::invokeMethod(mainWindow, "openFullScreen",
                    Q_ARG(QVariant, QVariant(QStringList{selectedPath})), Q_ARG(QVariant, QVariant(0)));
                return;
            }
            auto* scrollTimer = new QTimer(mainWindow);
            scrollTimer->setInterval(16);
            auto step = std::make_shared<int>(0);
            QObject::connect(scrollTimer, &QTimer::timeout, mainWindow, [mainWindow, step, scrollTimer] {
                ++*step;
                if (*step == 1 || *step == 91 || *step == 151 || *step == 241)
                    mvpview::performance::mark(QStringLiteral("scenario.motion"),
                        {{"active", *step == 1 || *step == 151}});
                const auto sheets = mainWindow->findChildren<QObject*>();
                for (auto* sheet : sheets) {
                    if (!sheet->objectName().startsWith(QStringLiteral("paneContactSheet-")) &&
                        sheet->objectName() != QStringLiteral("galleryStrip")) continue;
                    sheet->setProperty("benchmarkMoving", *step <= 90 || (*step > 150 && *step <= 240));
                    const qreal height = sheet->property("height").toDouble();
                    const qreal contentHeight = sheet->property("contentHeight").toDouble();
                    const qreal maximum = std::max<qreal>(0, contentHeight - height);
                    qreal y = sheet->property("contentY").toDouble();
                    if (*step <= 90) y += height * 0.08;
                    else if (*step == 120) y = maximum * 0.8;
                    else if (*step > 150 && *step <= 240) y -= height * 0.08;
                    sheet->setProperty("contentY", std::clamp(y, qreal(0), maximum));
                }
                if (*step >= 270) {
                    scrollTimer->stop();
                    mvpview::performance::mark(QStringLiteral("scenario.complete"));
                }
            });
            scrollTimer->start();
        });
    }

    if (!screenshotPath.isEmpty()) {
        QTimer::singleShot(screenshotDelay, &app,
                           [&app, &engine, screenshotPath] {
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

int main(int argc, char* argv[]) {
    (void)mvpview::performance::clock();
    mvpview::performance::mark(QStringLiteral("startup.process"));
    QCoreApplication::setApplicationName(QStringLiteral("MVP Image Viewer"));
    QCoreApplication::setOrganizationName(QStringLiteral("MvpView"));
    QCoreApplication::setApplicationVersion(QStringLiteral(MVPVIEW_PROJECT_VERSION));
    int result = 0;
    {
        const QSettings diagnosticSettings;
        mvpview::diagnostics::Options diagnosticOptions;
        diagnosticOptions.root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                                 + QStringLiteral("/diagnostics");
        diagnosticOptions.loggingEnabled = diagnosticSettings.value(QStringLiteral("diagnostics/loggingEnabled"), true).toBool();
        diagnosticOptions.crashEnabled = diagnosticSettings.value(QStringLiteral("diagnostics/crashReportingEnabled"), true).toBool();
        diagnosticOptions.level = mvpview::diagnostics::parseLevel(diagnosticSettings.value(QStringLiteral("diagnostics/logLevel"), QStringLiteral("Info")).toString());
        mvpview::diagnostics::Service diagnosticService(diagnosticOptions);
        diagnosticService.startCrashCapture({});
        mvpview::diagnostics::event(mvpview::diagnostics::Level::Info, mvpview::diagnostics::startup(), QStringLiteral("application.start"), {}, true);
        result = runApplication(argc, argv, diagnosticService);
        diagnosticService.markCleanExit();
    }
    // Active diagnostics and QML cache handles are closed at this point. Removing their parent
    // directories earlier would leave a partial reset on platforms that lock open files.
    const QString deletionError = mvpview::AppSettings::finishSoftwareCacheDeletion();
    if (!deletionError.isEmpty())
        qWarning().noquote() << deletionError;
    return result;
}
