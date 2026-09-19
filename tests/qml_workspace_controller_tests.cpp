#include "browser/file_clipboard.h"
#include "browser/thumbnail_model.h"
#include "diagnostics/diagnostics.h"
#include "io/encoded_color_management.h"
#include "io/image_loader.h"
#include "io/qt_image_decoder.h"
#include "platform/full_screen_presentation_controller.h"
#include "qml/app_settings.h"
#include "qml/browse_controller.h"
#include "qml/browse_workspace_controller.h"
#include "qml/compare_controller.h"
#include "qml/full_screen_controller.h"
#include "qml/image_properties_controller.h"
#include "qml/qml_image_canvas.h"
#include "qml/raw_parameters_controller.h"
#include "qml/thumbnail_image_provider.h"

#include <QDir>
#include <QDirIterator>
#include <QCryptographicHash>
#include <QFile>
#include <QGuiApplication>
#include <QHash>
#include <QHoverEvent>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QQuickWindow>
#include <QMutexLocker>
#include <QSettings>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QWheelEvent>
#include <QtEndian>

#include <atomic>
#include <cmath>
#include <memory>

namespace ispview {
namespace {
QString sessionLogText(const ispview::diagnostics::Service& service) {
    QString text;
    QDirIterator it(service.sessionDirectory(), {QStringLiteral("*.jsonl")}, QDir::Files);
    while (it.hasNext()) {
        QFile file(it.next());
        if (file.open(QIODevice::ReadOnly)) text += QString::fromUtf8(file.readAll());
    }
    return text;
}

class RawParameterColorDecoder final : public IImageDecoder {
  public:
    [[nodiscard]] bool canDecode(const QString&) const override { return true; }

    [[nodiscard]] DecodeResult decode(const DecodeRequest& request) const override {
        calls.fetch_add(1, std::memory_order_relaxed);
        QImage image(8, 8, QImage::Format_RGBA8888);
        image.fill(request.rawParameters && request.rawParameters->size.width() == 4
                       ? QColor(Qt::red)
                       : QColor(Qt::green));
        auto frame = std::make_shared<ImageFrame>();
        frame->descriptor.size = image.size();
        frame->metadata.path = request.path;
        frame->metadata.sourceSize =
            request.rawParameters ? request.rawParameters->size : image.size();
        frame->descriptor.validBits = request.rawParameters
                                          ? request.rawParameters->validBits()
                                          : 8;
        frame->rawParameters = request.rawParameters;
        frame->storage = std::move(image);
        return {std::move(frame), {}};
    }

    mutable std::atomic<int> calls{0};
};

class PurposeTrackingDecoder final : public IImageDecoder {
  public:
    explicit PurposeTrackingDecoder(QSize sourceSize) : sourceSize_(sourceSize) {}
    [[nodiscard]] bool canDecode(const QString&) const override { return true; }

    [[nodiscard]] DecodeResult decode(const DecodeRequest& request) const override {
        {
            const QMutexLocker lock(&mutex_);
            ++purposeCounts_[static_cast<int>(request.purpose)];
        }
        QImage image(request.purpose == DecodePurpose::Full ? QSize(32, 24) : QSize(16, 12),
                     QImage::Format_RGBA8888);
        image.fill(request.purpose == DecodePurpose::Full ? Qt::green : Qt::blue);
        if (request.purpose == DecodePurpose::Full)
            image.setPixelColor(1, 0, Qt::red);
        auto frame = std::make_shared<ImageFrame>();
        frame->descriptor.size = image.size();
        frame->descriptor.storageBits = 8;
        frame->metadata.path = request.path;
        frame->metadata.fileName = QFileInfo(request.path).fileName();
        frame->metadata.sourceSize = sourceSize_;
        frame->storage = std::move(image);
        return {std::move(frame), {}};
    }

    [[nodiscard]] int count(DecodePurpose purpose) const {
        const QMutexLocker lock(&mutex_);
        return purposeCounts_.value(static_cast<int>(purpose));
    }

  private:
    QSize sourceSize_;
    mutable QMutex mutex_;
    mutable QHash<int, int> purposeCounts_;
};

class RawPreviewTrackingDecoder final : public IImageDecoder {
  public:
    [[nodiscard]] bool canDecode(const QString&) const override { return true; }

    [[nodiscard]] DecodeResult decode(const DecodeRequest& request) const override {
        {
            const QMutexLocker lock(&mutex_);
            ++purposeCounts_[static_cast<int>(request.purpose)];
        }
        auto frame = std::make_shared<ImageFrame>();
        RawImageParameters parameters;
        parameters.size = {4, 4};
        parameters.format = RawPixelFormat::Raw16;
        parameters.validBitsOverride = 12;
        parameters.bayerPattern = BayerPattern::RGGB;
        parameters.demosaic = false;
        frame->rawParameters = parameters;
        frame->descriptor.size = {4, 4};
        frame->metadata.path = request.path;
        frame->metadata.fileName = QFileInfo(request.path).fileName();
        frame->metadata.sourceSize = QSize(4, 4);
        if (request.purpose == DecodePurpose::Full) {
            auto storage = std::make_shared<PlaneBufferSet>();
            storage->storage.resize(4 * 4 * 2);
            for (int index = 0; index < 16; ++index) {
                qToLittleEndian<quint16>(
                    static_cast<quint16>(index + 1),
                    reinterpret_cast<uchar*>(storage->storage.data() + index * 2));
            }
            storage->planes = {{0, 8, 32}};
            storage->displayImage = QImage(4, 4, QImage::Format_RGBA8888);
            storage->displayImage.fill(QColor(20, 30, 40));
            frame->storage = std::shared_ptr<const PlaneBufferSet>(storage);
        } else {
            QImage proxy(2, 2, QImage::Format_RGBA8888);
            proxy.fill(QColor(20, 30, 40));
            frame->descriptor.size = proxy.size();
            frame->sourceSamplesPending = true;
            frame->storage = std::move(proxy);
        }
        return {std::move(frame), {}};
    }

    [[nodiscard]] int count(DecodePurpose purpose) const {
        const QMutexLocker lock(&mutex_);
        return purposeCounts_.value(static_cast<int>(purpose));
    }

  private:
    mutable QMutex mutex_;
    mutable QHash<int, int> purposeCounts_;
};

BrowseController* paneAt(const BrowseWorkspaceController& workspace, int index) {
    const QVariantList panes = workspace.panes();
    return qobject_cast<BrowseController*>(panes.at(index).value<QObject*>());
}

QString createImage(QTemporaryDir& directory, const QString& name) {
    const QString path = directory.filePath(name);
    QImage image(4, 4, QImage::Format_RGBA8888);
    image.fill(Qt::black);
    if (!image.save(path))
        return {};
    return path;
}

class UpdateHttpServer final : public QObject {
  public:
    explicit UpdateHttpServer(QObject* parent = nullptr) : QObject(parent) {
        connect(&server_, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* socket = server_.nextPendingConnection()) {
                socket->setParent(this);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    QByteArray request = socket->property("request").toByteArray();
                    request += socket->readAll();
                    socket->setProperty("request", request);
                    const qsizetype headerEnd = request.indexOf("\r\n\r\n");
                    if (headerEnd < 0 || socket->property("handled").toBool())
                        return;
                    socket->setProperty("handled", true);
                    const QList<QByteArray> requestLine = request.left(request.indexOf("\r\n")).split(' ');
                    const QByteArray path = requestLine.size() >= 2 ? requestLine.at(1) : QByteArray{};
                    const bool found = responses_.contains(path);
                    const QByteArray body = responses_.value(path);
                    QByteArray response = found ? QByteArrayLiteral("HTTP/1.1 200 OK\r\n")
                                                : QByteArrayLiteral("HTTP/1.1 404 Not Found\r\n");
                    response += QByteArrayLiteral("Connection: close\r\nContent-Length: ") +
                                QByteArray::number(body.size()) + QByteArrayLiteral("\r\n\r\n") + body;
                    socket->write(response);
                    socket->disconnectFromHost();
                });
            }
        });
    }

    bool listen() { return server_.listen(QHostAddress::LocalHost, 0); }

    QUrl url(const QByteArray& path) const {
        return QUrl(QStringLiteral("http://127.0.0.1:%1%2")
                        .arg(server_.serverPort())
                        .arg(QString::fromLatin1(path)));
    }

    void respond(const QByteArray& path, const QByteArray& body) { responses_.insert(path, body); }

  private:
    QTcpServer server_;
    QHash<QByteArray, QByteArray> responses_;
};

QString testInstallerName() {
#if defined(Q_OS_MACOS)
    return QStringLiteral("MVPImageViewer-v99.0.0-macos-arm64.dmg");
#elif defined(Q_OS_WIN)
    return QStringLiteral("MVPImageViewer-v99.0.0-windows-x64-setup.exe");
#else
    return {};
#endif
}

QByteArray updateReleaseJson(const UpdateHttpServer& server, const QString& installerName,
                             qsizetype installerSize, const QByteArray& digest) {
    const QJsonObject asset{
        {QStringLiteral("name"), installerName},
        {QStringLiteral("browser_download_url"), server.url("/installer").toString()},
        {QStringLiteral("size"), installerSize},
        {QStringLiteral("digest"), QStringLiteral("sha256:%1").arg(QString::fromLatin1(digest))}};
    return QJsonDocument(
               QJsonObject{{QStringLiteral("tag_name"), QStringLiteral("v99.0.0")},
                           {QStringLiteral("html_url"), server.url("/release-page").toString()},
                           {QStringLiteral("assets"), QJsonArray{asset}}})
        .toJson(QJsonDocument::Compact);
}

} // namespace

class QmlWorkspaceControllerTests final : public QObject {
    Q_OBJECT

  private:
    std::unique_ptr<QTemporaryDir> settingsDirectory_;
    std::unique_ptr<QTemporaryDir> diagnosticsDirectory_;
    std::unique_ptr<diagnostics::Service> diagnosticsService_;

  private slots:
    void initTestCase();
    void diagnosticSettingsPersistAndNormalize();
    void addsActivatesAndClosesOneToFourPanes();
    void defersStartupDirectoryUntilExplicitlyStarted();
    void exposesNativeFolderNavigationStructure();
    void loadsFolderTreeChildrenWithoutNavigating();
    void usesPlatformFolderIconForThumbnailDirectories();
    void keepsPaneStateIndependent();
    void typedLocationNavigationMaintainsHistory();
    void aggregatesUniqueSelectionsInStableOrder();
    void copiesDropsIntoSubfoldersAndAcrossPanes();
    void emptyPaneOpensDroppedFoldersAndImageLocations();
    void rawParametersRefreshEveryPaneAndQmlProvider();
    void thumbnailMetadataSurvivesDemosaicCacheRoundTrip();
    void galleryUsesPreviewUntilPixelProbeRequestsFullResolution();
    void galleryStateClearsWhenChangingFolders();
    void browseFileDialogsAreRequestedByQmlAndActionsStayInBackend();
    void imagePropertiesAreExposedWithoutWidgetUi();
    void fullScreenPresentationLifecycleIsIdempotent();
    void imageCanvasSmoothDisplayCanBeToggled();
    void fullScreenCanvasPromotesRawPreviewWhenProbingSourceSamples();
    void canvasProbesPixelsFromWindowHoverEvents();
    void canvasPixelProbeReportsExactPixelAtHighZoom();
    void fullScreenSessionKeepsNavigationAndFileOperationsOutOfQml();
    void fullScreenExactPixelsPromotePreviewWithoutLosingFullResolution();
    void fullScreenAutomaticallyPromotesBudgetedImageAfterNavigationSettles();
    void rawParameterEditorAppliesValuesAndManagesPresetsWithoutWidgets();
    void comparePreferencesPersistAndHorizontalModeIsUnavailable();
    void compareUsesSourcePixelTextAndLumaOnlyHistogram();
    void compareViewSyncTemporarilyBypassesWithControl();
    void compareDefersOversizedAutomaticFullLoadsButExactToolsStillPromote();
    void compareAutomaticallyPromotesBudgetedImages();
    void applicationSettingsPersistAndRestoreDefaults();
    void otaDownloadsAndVerifiesPlatformInstaller();
    void otaRejectsInstallerWithWrongChecksum();
    void otaDownloadCanBeCancelled();
    void diagnosticsReceivesPresentationSessionEvents();
};

void QmlWorkspaceControllerTests::diagnosticSettingsPersistAndNormalize() {
    auto* application = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
    { AppSettings settings(application); settings.setLoggingEnabled(false); settings.setLogLevel(QStringLiteral("Error")); settings.setCrashReportingEnabled(false); }
    AppSettings restored(application);
    QVERIFY(!restored.loggingEnabled());
    QCOMPARE(restored.logLevel(), QStringLiteral("Error"));
    QVERIFY(!restored.crashReportingEnabled());
    restored.setLogLevel(QStringLiteral("invalid"));
    QCOMPARE(restored.logLevel(), QStringLiteral("Info"));
    restored.restoreDefaults();
    QVERIFY(restored.loggingEnabled()); QVERIFY(restored.crashReportingEnabled());
}

void QmlWorkspaceControllerTests::initTestCase() {
    settingsDirectory_ = std::make_unique<QTemporaryDir>();
    QVERIFY(settingsDirectory_->isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("ISPViewTests"));
    QCoreApplication::setApplicationName(QStringLiteral("QmlWorkspaceControllerTests"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory_->path());
    QSettings().clear();
    // The production controllers are instrumented, so this run also proves that compare and
    // full-screen session events reach the diagnostics store.
    diagnosticsDirectory_ = std::make_unique<QTemporaryDir>();
    QVERIFY(diagnosticsDirectory_->isValid());
    diagnosticsService_ = std::make_unique<diagnostics::Service>(
        diagnostics::Options{diagnosticsDirectory_->path(), true, false, diagnostics::Level::Debug});
}

void QmlWorkspaceControllerTests::addsActivatesAndClosesOneToFourPanes() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    BrowseWorkspaceController workspace(std::make_shared<QtImageDecoder>(), directory.path());

    QCOMPARE(workspace.paneCount(), 1);
    QCOMPARE(workspace.activePaneIndex(), 0);
    QVERIFY(workspace.hasActivePane());
    workspace.setActiveDisplayMode(2);
    QCOMPARE(paneAt(workspace, 0)->displayMode(), 2);

    workspace.addFileManagerPane();
    QCOMPARE(workspace.paneCount(), 2);
    QCOMPARE(workspace.activePaneIndex(), 1);
    QCOMPARE(paneAt(workspace, 0)->displayMode(), 0);
    QCOMPARE(paneAt(workspace, 1)->displayMode(), 0);
    QVERIFY(paneAt(workspace, 1)->currentDirectory().isEmpty());
    workspace.setActiveDisplayMode(2);
    QCOMPARE(paneAt(workspace, 1)->displayMode(), 0);

    workspace.addFileManagerPane();
    QCOMPARE(workspace.activePaneIndex(), 2);
    for (int index = 0; index < workspace.paneCount(); ++index)
        QCOMPARE(paneAt(workspace, index)->displayMode(), 1);
    workspace.setActiveDisplayMode(0);
    QCOMPARE(paneAt(workspace, 2)->displayMode(), 1);
    workspace.addFileManagerPane();
    QCOMPARE(workspace.paneCount(), 4);
    QCOMPARE(workspace.activePaneIndex(), 3);
    for (int index = 0; index < workspace.paneCount(); ++index)
        QCOMPARE(paneAt(workspace, index)->displayMode(), 1);
    QVERIFY(!workspace.canAddPane());
    workspace.addFileManagerPane();
    QCOMPARE(workspace.paneCount(), 4);

    workspace.activatePane(2);
    QCOMPARE(workspace.activePaneIndex(), 2);
    QSignalSpy activePaneSpy(&workspace, &BrowseWorkspaceController::activePaneChanged);
    workspace.closePane(1);
    QCOMPARE(workspace.paneCount(), 3);
    QCOMPARE(workspace.activePaneIndex(), 1);
    QCOMPARE(activePaneSpy.size(), 1);
    workspace.closePane(1);
    QCOMPARE(workspace.activePaneIndex(), 1);
    workspace.closePane(1);
    QCOMPARE(workspace.activePaneIndex(), 0);
    workspace.closePane(0);
    QCOMPARE(workspace.paneCount(), 1);
    QCOMPARE(workspace.activePaneIndex(), 0);
    QVERIFY(workspace.hasActivePane());
    QVERIFY(paneAt(workspace, 0)->currentDirectory().isEmpty());
}

void QmlWorkspaceControllerTests::applicationSettingsPersistAndRestoreDefaults() {
    QSettings().clear();
    auto* application = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
    QVERIFY(application);
    AppSettings settings(application);

    QCOMPARE(settings.language(), QStringLiteral("system"));
    QCOMPARE(settings.theme(), QStringLiteral("system"));
    QVERIFY(settings.restoreLastDirectory());
    QVERIFY(settings.confirmTrash());
    QVERIFY(settings.automaticUpdateChecks());
    QVERIFY(settings.applyEmbeddedColorProfiles());
    QVERIFY(settings.preserveHighBitDepth());
    QCOMPARE(settings.displayColorSpace(), QStringLiteral("auto"));
    // With no surface reported yet, auto falls back to sRGB.
    QCOMPARE(settings.resolvedDisplayColorSpace(), QStringLiteral("sRGB"));
    QCOMPARE(currentDisplayColorSpace(), DisplayColorSpace::Srgb);
    QVERIFY(settings.honorExifOrientation());
    QCOMPARE(settings.canvasBackground(), QStringLiteral("neutral"));
    QVERIFY(settings.smoothDisplay());
    QCOMPARE(settings.shortcutFor(QStringLiteral("compare")), QStringLiteral("C"));
    QCOMPARE(settings.shortcutEntries().size(), 7);

    QSignalSpy languageSpy(&settings, &AppSettings::languageChanged);
    QSignalSpy themeSpy(&settings, &AppSettings::themeChanged);
    QSignalSpy shortcutsSpy(&settings, &AppSettings::shortcutsChanged);
    QSignalSpy colorDisplaySpy(&settings, &AppSettings::colorDisplayChanged);
    QSignalSpy smoothDisplaySpy(&settings, &AppSettings::smoothDisplayChanged);
    settings.setLanguage(QStringLiteral("en"));
    settings.setTheme(QStringLiteral("dark"));
    settings.setRestoreLastDirectory(false);
    settings.setConfirmTrash(false);
    settings.setAutomaticUpdateChecks(false);
    settings.setApplyEmbeddedColorProfiles(false);
    settings.setPreserveHighBitDepth(false);
    settings.setDisplayColorSpace(QStringLiteral("display-p3"));
    settings.setHonorExifOrientation(false);
    settings.setCanvasBackground(QStringLiteral("black"));
    settings.setSmoothDisplay(false);
    QCOMPARE(settings.setShortcut(QStringLiteral("compare"), QStringLiteral("Ctrl+Shift+C")),
             QString{});
    QCOMPARE(settings.setShortcut(QStringLiteral("rename"), QStringLiteral("Ctrl+Shift+C")),
             QStringLiteral("compare"));
    QCOMPARE(settings.setShortcut(QStringLiteral("rename"), QStringLiteral("not a shortcut")),
             QStringLiteral("invalid"));

    QCOMPARE(languageSpy.count(), 1);
    QCOMPARE(themeSpy.count(), 1);
    QCOMPARE(shortcutsSpy.count(), 1);
    QCOMPARE(colorDisplaySpy.count(), 5);
    QCOMPARE(smoothDisplaySpy.count(), 1);
    QCOMPARE(QSettings().value(QStringLiteral("general/language")).toString(),
             QStringLiteral("en"));
    QCOMPARE(QSettings().value(QStringLiteral("appearance/theme")).toString(),
             QStringLiteral("dark"));
    QVERIFY(settings.darkTheme());
    QVERIFY(!settings.restoreLastDirectory());
    QVERIFY(!settings.confirmTrash());
    QVERIFY(!settings.automaticUpdateChecks());
    QVERIFY(!settings.applyEmbeddedColorProfiles());
    QVERIFY(!settings.preserveHighBitDepth());
    QCOMPARE(settings.displayColorSpace(), QStringLiteral("display-p3"));
    QVERIFY(!settings.honorExifOrientation());
    QCOMPARE(settings.canvasBackground(), QStringLiteral("black"));
    QVERIFY(!settings.smoothDisplay());
    QVERIFY(!QSettings().value(QStringLiteral("display/smoothDisplay")).toBool());
    AppSettings reloadedSettings(application);
    QVERIFY(!reloadedSettings.smoothDisplay());
    QCOMPARE(reloadedSettings.displayColorSpace(), QStringLiteral("display-p3"));
    QCOMPARE(currentDisplayColorSpace(), DisplayColorSpace::DisplayP3);
    QVERIFY(!EncodedColorManagement::isEnabled());
    QVERIFY(!QtImageDecoder::preserveHighBitDepth());
    QVERIFY(!QtImageDecoder::autoOrientationEnabled());
    QCOMPARE(settings.shortcutFor(QStringLiteral("compare")), QStringLiteral("Ctrl+Shift+C"));
    QCOMPARE(QSettings().value(QStringLiteral("shortcuts/compare")).toString(),
             QStringLiteral("Ctrl+Shift+C"));

    // "auto" follows the space the platform reports for the window surface.
    settings.setDisplayColorSpace(QStringLiteral("auto"));
    settings.setSurfaceColorSpace(QColorSpace(QColorSpace::DisplayP3));
    QCOMPARE(currentDisplayColorSpace(), DisplayColorSpace::DisplayP3);
    QCOMPARE(settings.resolvedDisplayColorSpace(), QStringLiteral("Display P3"));
    // A pinned space ignores the surface report.
    settings.setDisplayColorSpace(QStringLiteral("srgb"));
    QCOMPARE(currentDisplayColorSpace(), DisplayColorSpace::Srgb);
    settings.setSurfaceColorSpace(QColorSpace(QColorSpace::AdobeRgb));
    QCOMPARE(currentDisplayColorSpace(), DisplayColorSpace::Srgb);
    settings.setDisplayColorSpace(QStringLiteral("auto"));
    QCOMPARE(currentDisplayColorSpace(), DisplayColorSpace::AdobeRgb);
    // A surface space that is none of the SDR targets (linear sRGB here) falls back to sRGB.
    settings.setSurfaceColorSpace(QColorSpace(QColorSpace::SRgbLinear));
    QCOMPARE(currentDisplayColorSpace(), DisplayColorSpace::Srgb);
    // An unknown preference is normalised to auto instead of pinning something arbitrary.
    settings.setDisplayColorSpace(QStringLiteral("nonsense"));
    QCOMPARE(settings.displayColorSpace(), QStringLiteral("auto"));

    settings.restoreDefaults();
    QCOMPARE(settings.language(), QStringLiteral("system"));
    QCOMPARE(settings.theme(), QStringLiteral("system"));
    QVERIFY(settings.restoreLastDirectory());
    QVERIFY(settings.confirmTrash());
    QVERIFY(settings.automaticUpdateChecks());
    QVERIFY(settings.applyEmbeddedColorProfiles());
    QVERIFY(settings.preserveHighBitDepth());
    QVERIFY(settings.honorExifOrientation());
    QCOMPARE(settings.canvasBackground(), QStringLiteral("neutral"));
    QVERIFY(settings.smoothDisplay());
    QCOMPARE(settings.displayColorSpace(), QStringLiteral("auto"));
    QCOMPARE(currentDisplayColorSpace(), DisplayColorSpace::Srgb);
    QCOMPARE(settings.shortcutFor(QStringLiteral("compare")), QStringLiteral("C"));
}

void QmlWorkspaceControllerTests::defersStartupDirectoryUntilExplicitlyStarted() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    BrowseWorkspaceController workspace(std::make_shared<QtImageDecoder>(), directory.path(), true);

    QCOMPARE(paneAt(workspace, 0)->currentDirectory(), QString{});
    workspace.addFileManagerPane();
    workspace.startDeferredInitialDirectory();
    QTRY_COMPARE_WITH_TIMEOUT(paneAt(workspace, 0)->currentDirectory(),
                              QFileInfo(directory.path()).absoluteFilePath(), 3000);
    QCOMPARE(paneAt(workspace, 1)->currentDirectory(), QString{});

    // Starting twice must not restore over a directory the user selected in the meantime.
    QTemporaryDir selectedDirectory;
    QVERIFY(selectedDirectory.isValid());
    paneAt(workspace, 0)->openDirectory(selectedDirectory.path());
    workspace.startDeferredInitialDirectory();
    QCOMPARE(paneAt(workspace, 0)->currentDirectory(),
             QFileInfo(selectedDirectory.path()).absoluteFilePath());
}

void QmlWorkspaceControllerTests::exposesNativeFolderNavigationStructure() {
    BrowseController controller(std::make_shared<QtImageDecoder>(), {}, this);
    const QVariantList places = controller.nativeSidebarPlaces();
    QVERIFY(!places.isEmpty());

    QSet<QString> paths;
    for (const QVariant& value : places) {
        const QVariantMap place = value.toMap();
        QVERIFY(!place.value(QStringLiteral("label")).toString().isEmpty());
        const QString path = place.value(QStringLiteral("path")).toString();
        QVERIFY(!path.isEmpty());
        QVERIFY(!paths.contains(path));
        paths.insert(path);
#ifdef Q_OS_WIN
        QVERIFY(place.value(QStringLiteral("icon"))
                    .toString()
                    .startsWith(QStringLiteral("image://system-folder/")));
#else
        QVERIFY(!place.contains(QStringLiteral("icon")));
#endif
    }

#ifdef Q_OS_WIN
    const QVariantMap firstPlace = places.constFirst().toMap();
    const QString encodedPath = QString::fromLatin1(
        QUrl::toPercentEncoding(firstPlace.value(QStringLiteral("path")).toString()));
    SystemFolderIconProvider provider;
    QSize iconSize;
    const QImage nativeIcon = provider.requestImage(encodedPath, &iconSize, QSize(32, 32));
    // The offscreen QPA used by CTest intentionally has no native Windows pixmap backend.
    // URL wiring remains testable there; pixel conversion is validated only on a native QPA.
    if (QGuiApplication::platformName() != QStringLiteral("offscreen"))
        QVERIFY(!nativeIcon.isNull());
    if (!nativeIcon.isNull())
        QCOMPARE(iconSize, nativeIcon.size());
#endif

    auto* folderModel = qobject_cast<QFileSystemModel*>(controller.folderTree());
    QVERIFY(folderModel);
    QVERIFY(folderModel->filter().testFlag(QDir::Dirs));
    QVERIFY(!folderModel->filter().testFlag(QDir::Files));
}

void QmlWorkspaceControllerTests::loadsFolderTreeChildrenWithoutNavigating() {
    // Keep this QFileSystemModel fixture under the build tree. The managed Windows test
    // environment intentionally blocks the model's worker thread from enumerating AppData.
    QTemporaryDir directory(QDir::current().filePath(QStringLiteral("folder-tree-XXXXXX")));
    QVERIFY(directory.isValid());
    const QString previewDirectory = directory.filePath(QStringLiteral("preview"));
    const QString expandableDirectory = directory.filePath(QStringLiteral("expandable"));
    const QString childDirectory = QDir(expandableDirectory).filePath(QStringLiteral("child"));
    QVERIFY(QDir().mkpath(previewDirectory));
    QVERIFY(QDir().mkpath(childDirectory));

    BrowseController controller(std::make_shared<QtImageDecoder>(), previewDirectory, this);
    auto* folderModel = qobject_cast<QFileSystemModel*>(controller.folderTree());
    QVERIFY(folderModel);
    const QString currentDirectory = controller.currentDirectory();

    controller.loadFolderTreeChildren(expandableDirectory);
    const QModelIndex expandableIndex = folderModel->index(expandableDirectory);
    QVERIFY(expandableIndex.isValid());
    QTRY_VERIFY_WITH_TIMEOUT(folderModel->rowCount(expandableIndex) > 0, 3000);
    QVERIFY(folderModel->index(childDirectory).isValid());
    QCOMPARE(controller.currentDirectory(), currentDirectory);
}

void QmlWorkspaceControllerTests::usesPlatformFolderIconForThumbnailDirectories() {
    ImageLoader loader(std::make_shared<QtImageDecoder>());
    ThumbnailModel model(&loader);
    ImageFileRecord directory;
    directory.path = QStringLiteral("C:/Pictures");
    directory.fileName = QStringLiteral("Pictures");
    directory.isDirectory = true;
    model.setFiles({directory});

#ifdef Q_OS_MACOS
    const QString expected = QStringLiteral("qrc:/icons/ui/macos-folder.svg");
#elif defined(Q_OS_WIN)
    const QString expected = QStringLiteral("qrc:/icons/ui/windows-folder.svg");
#else
    const QString expected = QStringLiteral("qrc:/icons/ui/folder.svg");
#endif
    QCOMPARE(model.index(0).data(ThumbnailModel::ThumbnailUrlRole).toString(), expected);
}

void QmlWorkspaceControllerTests::keepsPaneStateIndependent() {
    QTemporaryDir firstDirectory;
    QTemporaryDir secondDirectory;
    QVERIFY(firstDirectory.isValid());
    QVERIFY(secondDirectory.isValid());
    BrowseWorkspaceController workspace(std::make_shared<QtImageDecoder>(), firstDirectory.path());
    workspace.addFileManagerPane();

    BrowseController* first = paneAt(workspace, 0);
    BrowseController* second = paneAt(workspace, 1);
    QCOMPARE(first->loader(), second->loader());
    QCOMPARE(first->folderTree(), second->folderTree());
    second->openDirectory(secondDirectory.path());
    QCOMPARE(first->recentLocations().constFirst(), secondDirectory.path());
    QCOMPARE(second->recentLocations().constFirst(), secondDirectory.path());
    first->setFilterText(QStringLiteral("first"));
    first->setSortMode(2);
    second->setFilterText(QStringLiteral("second"));
    second->setSortMode(3);
    second->setDisplayMode(1);

    QCOMPARE(first->currentDirectory(), firstDirectory.path());
    QCOMPARE(second->currentDirectory(), secondDirectory.path());
    QCOMPARE(first->filterText(), QStringLiteral("first"));
    QCOMPARE(second->filterText(), QStringLiteral("second"));
    QCOMPARE(first->sortMode(), 2);
    QCOMPARE(second->sortMode(), 3);
    QCOMPARE(first->displayMode(), 0);
    QCOMPARE(second->displayMode(), 1);

    first->setSharedRecentFolders({firstDirectory.path()});
    QCOMPARE(second->recentFolders(), QStringList({firstDirectory.path()}));
}

void QmlWorkspaceControllerTests::typedLocationNavigationMaintainsHistory() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString child = directory.filePath(QStringLiteral("child folder"));
    QVERIFY(QDir().mkpath(child));
    const QString filePath = createImage(directory, QStringLiteral("not-a-folder.png"));
    QVERIFY(!filePath.isEmpty());

    BrowseController controller(std::make_shared<QtImageDecoder>(), directory.path(), this);
    QCOMPARE(controller.recentLocations().constFirst(), directory.path());
    QVERIFY(!controller.canGoBack());

    QCOMPARE(controller.navigateToTypedPath(QStringLiteral("child folder")), QString{});
    QCOMPARE(controller.currentDirectory(), child);
    QVERIFY(controller.canGoBack());
    QCOMPARE(controller.recentLocations().constFirst(), child);

    controller.navigateBack();
    QCOMPARE(controller.currentDirectory(), directory.path());
    QVERIFY(controller.canGoForward());
    controller.navigateForward();
    QCOMPARE(controller.currentDirectory(), child);
    controller.navigateUp();
    QCOMPARE(controller.currentDirectory(), directory.path());

    const QString beforeError = controller.currentDirectory();
    QVERIFY(controller.navigateToTypedPath(filePath).contains(QStringLiteral("not a folder")));
    QCOMPARE(controller.currentDirectory(), beforeError);
    QVERIFY(controller.navigateToTypedPath(QStringLiteral("missing")).contains(
        QStringLiteral("does not exist")));
    QCOMPARE(controller.currentDirectory(), beforeError);

    controller.clearRecentLocations();
    QVERIFY(controller.recentLocations().isEmpty());
}

void QmlWorkspaceControllerTests::aggregatesUniqueSelectionsInStableOrder() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString a = createImage(directory, QStringLiteral("a.png"));
    const QString b = createImage(directory, QStringLiteral("b.png"));
    const QString c = createImage(directory, QStringLiteral("c.png"));
    const QString d = createImage(directory, QStringLiteral("d.png"));
    const QString e = createImage(directory, QStringLiteral("e.png"));
    QVERIFY(!a.isEmpty() && !b.isEmpty() && !c.isEmpty() && !d.isEmpty() && !e.isEmpty());

    BrowseWorkspaceController workspace(std::make_shared<QtImageDecoder>(), directory.path());
    workspace.addFileManagerPane();

    workspace.selectPath(0, a);
    workspace.selectPath(0, b, false, true);
    workspace.selectPath(1, b, false, true);
    workspace.selectPath(1, c, false, true);
    QCOMPARE(workspace.workspaceSelectedPaths(), QStringList({a, b, c}));
    QCOMPARE(workspace.workspaceSelectionCount(), 3);
    QVERIFY(workspace.canCompare());

    QSignalSpy compareSpy(&workspace, &BrowseWorkspaceController::compareRequested);
    workspace.compareSelected();
    QCOMPARE(compareSpy.size(), 1);
    QCOMPARE(compareSpy.constFirst().constFirst().toStringList(), QStringList({a, b, c}));

    workspace.selectPath(1, d, false, true);
    workspace.selectPath(1, e, false, true);
    QCOMPARE(workspace.workspaceSelectionCount(), 5);
    QVERIFY(!workspace.canCompare());
    workspace.compareSelected();
    QCOMPARE(compareSpy.size(), 1);

    workspace.closePane(1);
    QCOMPARE(workspace.workspaceSelectedPaths(), QStringList({a, b}));
    QVERIFY(workspace.canCompare());

    workspace.addFileManagerPane();
    workspace.selectPath(1, c, false, false);
    QCOMPARE(workspace.workspaceSelectedPaths(), QStringList({a, b, c}));
    workspace.selectPath(0, a, false, true);
    QCOMPARE(workspace.workspaceSelectedPaths(), QStringList({b, c}));
    workspace.selectPath(0, a, false, true);
    QCOMPARE(workspace.workspaceSelectedPaths(), QStringList({b, a, c}));
}

void QmlWorkspaceControllerTests::copiesDropsIntoSubfoldersAndAcrossPanes() {
    QTemporaryDir sourceDirectory;
    QTemporaryDir targetDirectory;
    QVERIFY(sourceDirectory.isValid());
    QVERIFY(targetDirectory.isValid());
    const QString source = createImage(sourceDirectory, QStringLiteral("drag.png"));
    const QString child = sourceDirectory.filePath(QStringLiteral("child"));
    QVERIFY(QDir().mkdir(child));

    BrowseWorkspaceController workspace(std::make_shared<QtImageDecoder>(), sourceDirectory.path());
    QSignalSpy transferConfirmation(&workspace,
                                    &BrowseWorkspaceController::transferConfirmationRequested);
    BrowseController* first = paneAt(workspace, 0);
    first->copyDroppedUrlsInto({QUrl::fromLocalFile(source)}, child);
    QCOMPARE(transferConfirmation.size(), 1);
    QCOMPARE(transferConfirmation.constFirst().at(0).toBool(), false);
    QCOMPARE(transferConfirmation.constFirst().at(1).toInt(), 1);
    QCOMPARE(transferConfirmation.constFirst().at(2).toString(), child);
    QVERIFY(!QFileInfo::exists(QDir(child).filePath(QStringLiteral("drag.png"))));
    workspace.confirmPendingTransfer();
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(QDir(child).filePath(QStringLiteral("drag.png"))),
                             3000);

    workspace.addFileManagerPane();
    BrowseController* second = paneAt(workspace, 1);
    second->openDirectory(targetDirectory.path());
    second->copyDroppedUrls({QUrl::fromLocalFile(source)});
    QCOMPARE(transferConfirmation.size(), 2);
    workspace.cancelPendingTransfer();
    QTest::qWait(100);
    QVERIFY(!QFileInfo::exists(targetDirectory.filePath(QStringLiteral("drag.png"))));

    second->copyDroppedUrls({QUrl::fromLocalFile(source)});
    QCOMPARE(transferConfirmation.size(), 3);
    workspace.confirmPendingTransfer();
    QTRY_VERIFY_WITH_TIMEOUT(
        QFileInfo::exists(targetDirectory.filePath(QStringLiteral("drag.png"))), 3000);

    const QString moveSource = createImage(sourceDirectory, QStringLiteral("move.png"));
    FileClipboard::setPaths({moveSource}, true);
    second->pasteItems();
    QCOMPARE(transferConfirmation.size(), 4);
    QCOMPARE(transferConfirmation.constLast().at(0).toBool(), true);
    QVERIFY(QFileInfo::exists(moveSource));
    workspace.confirmPendingTransfer();
    QTRY_VERIFY_WITH_TIMEOUT(
        QFileInfo::exists(targetDirectory.filePath(QStringLiteral("move.png"))), 3000);
    QVERIFY(!QFileInfo::exists(moveSource));
}

void QmlWorkspaceControllerTests::emptyPaneOpensDroppedFoldersAndImageLocations() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString image = createImage(directory, QStringLiteral("open.png"));
    QVERIFY(!image.isEmpty());

    BrowseWorkspaceController workspace(std::make_shared<QtImageDecoder>(), directory.path());
    workspace.addFileManagerPane();
    BrowseController* empty = paneAt(workspace, 1);
    QVERIFY(empty->currentDirectory().isEmpty());
    empty->openDroppedUrls({QUrl::fromLocalFile(image)});
    QCOMPARE(empty->currentDirectory(), directory.path());
    QCOMPARE(empty->selectedPaths(), QStringList({image}));

    QTemporaryDir anotherDirectory;
    QVERIFY(anotherDirectory.isValid());
    workspace.addFileManagerPane();
    empty = paneAt(workspace, 2);
    empty->openDroppedUrls({QUrl::fromLocalFile(anotherDirectory.path())});
    QCOMPARE(empty->currentDirectory(), anotherDirectory.path());
}

void QmlWorkspaceControllerTests::rawParametersRefreshEveryPaneAndQmlProvider() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString rawPath = directory.filePath(QStringLiteral("capture.raw"));
    QFile rawFile(rawPath);
    QVERIFY(rawFile.open(QIODevice::WriteOnly));
    QCOMPARE(rawFile.write(QByteArray(256, '\0')), 256);
    rawFile.close();

    auto decoder = std::make_shared<RawParameterColorDecoder>();
    BrowseWorkspaceController workspace(decoder, directory.path());
    workspace.addFileManagerPane();
    paneAt(workspace, 1)->openDirectory(directory.path());

    const auto indexForPath = [&rawPath](BrowseController* pane) {
        QAbstractItemModel* model = pane->thumbnails();
        for (int row = 0; row < model->rowCount(); ++row) {
            const QModelIndex index = model->index(row, 0);
            if (index.data(ThumbnailModel::PathRole).toString() == rawPath)
                return index;
        }
        return QModelIndex{};
    };
    QTRY_VERIFY_WITH_TIMEOUT(indexForPath(paneAt(workspace, 0)).isValid(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(indexForPath(paneAt(workspace, 1)).isValid(), 3000);
    const QString beforeFirst =
        indexForPath(paneAt(workspace, 0)).data(ThumbnailModel::ThumbnailUrlRole).toString();
    const QString beforeSecond =
        indexForPath(paneAt(workspace, 1)).data(ThumbnailModel::ThumbnailUrlRole).toString();
    QCOMPARE(decoder->calls.load(std::memory_order_relaxed), 0);

    RawImageParameters parameters;
    parameters.size = {4, 4};
    parameters.format = RawPixelFormat::Raw16;
    parameters.rowStride = 8;
    workspace.loader()->setRawParameters(rawPath, parameters);

    const QString firstUrl =
        indexForPath(paneAt(workspace, 0)).data(ThumbnailModel::ThumbnailUrlRole).toString();
    const QString secondUrl =
        indexForPath(paneAt(workspace, 1)).data(ThumbnailModel::ThumbnailUrlRole).toString();
    QVERIFY(firstUrl != beforeFirst);
    QVERIFY(secondUrl != beforeSecond);
    QCOMPARE(firstUrl, secondUrl);

    ThumbnailImageProvider provider(decoder, workspace.loader());
    const QString encodedPath = QString::fromLatin1(QUrl::toPercentEncoding(rawPath));
    QImage firstImage =
        provider.requestImage(encodedPath + QStringLiteral("?v=first"), nullptr, QSize(16, 16));
    QCOMPARE(firstImage.pixelColor(0, 0), QColor(Qt::red));
    QCOMPARE(decoder->calls.load(std::memory_order_relaxed), 1);

    parameters.size = {8, 4};
    parameters.rowStride = 16;
    workspace.loader()->setRawParameters(rawPath, parameters);
    const QString refreshedUrl =
        indexForPath(paneAt(workspace, 1)).data(ThumbnailModel::ThumbnailUrlRole).toString();
    QVERIFY(refreshedUrl != secondUrl);
    QImage refreshedImage =
        provider.requestImage(encodedPath + QStringLiteral("?v=second"), nullptr, QSize(16, 16));
    QCOMPARE(refreshedImage.pixelColor(0, 0), QColor(Qt::green));
    QCOMPARE(decoder->calls.load(std::memory_order_relaxed), 2);
}

void QmlWorkspaceControllerTests::thumbnailMetadataSurvivesDemosaicCacheRoundTrip() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString rawPath = directory.filePath(QStringLiteral("capture.raw"));
    QFile rawFile(rawPath);
    QVERIFY(rawFile.open(QIODevice::WriteOnly));
    QCOMPARE(rawFile.write(QByteArray(256, '\0')), 256);
    rawFile.close();
    const QString pngPath = createImage(directory, QStringLiteral("reference.png"));
    QVERIFY(!pngPath.isEmpty());

    auto decoder = std::make_shared<RawParameterColorDecoder>();
    BrowseWorkspaceController workspace(decoder, directory.path());
    BrowseController* pane = paneAt(workspace, 0);
    const auto indexForPath = [pane](const QString& path) {
        QAbstractItemModel* model = pane->thumbnails();
        for (int row = 0; row < model->rowCount(); ++row) {
            const QModelIndex index = model->index(row, 0);
            if (index.data(ThumbnailModel::PathRole).toString() == path)
                return index;
        }
        return QModelIndex{};
    };
    QTRY_VERIFY_WITH_TIMEOUT(indexForPath(rawPath).isValid(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(indexForPath(pngPath).isValid(), 3000);

    ThumbnailImageProvider provider(decoder, workspace.loader());
    const QString encodedRaw = QString::fromLatin1(QUrl::toPercentEncoding(rawPath));
    const QString encodedPng = QString::fromLatin1(QUrl::toPercentEncoding(pngPath));
    QVERIFY(!provider.requestImage(encodedPng + QStringLiteral("?v=encoded"), nullptr,
                                   QSize(16, 16)).isNull());
    const QString encodedLabel =
        indexForPath(pngPath).data(ThumbnailModel::TechnicalLabelRole).toString();
    QVERIFY(!encodedLabel.contains(QStringLiteral("Reading")));

    RawImageParameters mosaic;
    mosaic.size = {4, 4};
    mosaic.format = RawPixelFormat::Raw16;
    mosaic.rowStride = 8;
    mosaic.validBitsOverride = 10;
    mosaic.demosaic = false;
    workspace.loader()->setRawParameters(rawPath, mosaic);
    QVERIFY(!provider.requestImage(encodedRaw + QStringLiteral("?v=mosaic"), nullptr,
                                   QSize(16, 16)).isNull());
    QCOMPARE(decoder->calls.load(std::memory_order_relaxed), 2);

    RawImageParameters demosaiced = mosaic;
    demosaiced.demosaic = true;
    workspace.loader()->setRawParameters(rawPath, demosaiced);
    QVERIFY(!provider.requestImage(encodedRaw + QStringLiteral("?v=demosaiced"), nullptr,
                                   QSize(16, 16)).isNull());
    QCOMPARE(decoder->calls.load(std::memory_order_relaxed), 3);

    workspace.loader()->setRawParameters(rawPath, mosaic);
    const QString rawLabel =
        indexForPath(rawPath).data(ThumbnailModel::TechnicalLabelRole).toString();
    QVERIFY(!rawLabel.contains(QStringLiteral("Reading")));
    QVERIFY(rawLabel.contains(QStringLiteral("4")));
    QVERIFY(rawLabel.contains(QStringLiteral("10 bit")));
    QCOMPARE(indexForPath(pngPath).data(ThumbnailModel::TechnicalLabelRole).toString(),
             encodedLabel);

    QSignalSpy metadataReady(workspace.loader(), &ImageLoader::thumbnailMetadataReady);
    QVERIFY(!provider.requestImage(encodedRaw + QStringLiteral("?v=mosaic-again"), nullptr,
                                   QSize(16, 16)).isNull());
    // Returning to the first configuration reuses its cached pixels and must still republish
    // metadata so a newly created or reset model can populate its information roles.
    QCOMPARE(decoder->calls.load(std::memory_order_relaxed), 3);
    QCOMPARE(metadataReady.count(), 1);
    QVERIFY(!indexForPath(rawPath)
                 .data(ThumbnailModel::TechnicalLabelRole)
                 .toString()
                 .contains(QStringLiteral("Reading")));
}

void QmlWorkspaceControllerTests::galleryUsesPreviewUntilPixelProbeRequestsFullResolution() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("gallery.png"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("fixture"), 7);
    file.close();

    auto decoder = std::make_shared<PurposeTrackingDecoder>(QSize(12000, 10000));
    BrowseController controller(decoder, directory.path());
    controller.setGalleryPath(path);
    QTRY_COMPARE_WITH_TIMEOUT(decoder->count(DecodePurpose::Preview), 1, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(controller.galleryImageReady(), 2000);
    QCOMPARE(decoder->count(DecodePurpose::Full), 0);
    QCOMPARE(controller.probeGalleryPixel(0, 0), QStringLiteral("Loading pixel data…"));
    QTRY_COMPARE_WITH_TIMEOUT(decoder->count(DecodePurpose::Full), 1, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(controller.galleryFullResolution(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(
        controller.probeGalleryPixel(0, 0).contains(QStringLiteral("RGB(0,255,0)")), 2000);
    QVERIFY(controller.probeGalleryPixel(1, 0).contains(QStringLiteral("RGB(255,0,0)")));

    ThumbnailImageProvider provider(decoder, controller.loader());
    const QString encodedPath = QString::fromLatin1(QUrl::toPercentEncoding(path));
    const QImage fullTexture = provider.requestImage(
        encodedPath + QStringLiteral("?purpose=gallery-full"), nullptr, QSize(8, 8));
    QCOMPARE(fullTexture.size(), QSize(32, 24));
    QCOMPARE(fullTexture.pixelColor(0, 0), QColor(Qt::green));
    QCOMPARE(fullTexture.pixelColor(1, 0), QColor(Qt::red));
    QCOMPARE(decoder->count(DecodePurpose::Full), 1);
}

void QmlWorkspaceControllerTests::galleryStateClearsWhenChangingFolders() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString firstDirectory = directory.filePath(QStringLiteral("first"));
    const QString secondDirectory = directory.filePath(QStringLiteral("second"));
    QVERIFY(QDir().mkpath(firstDirectory));
    QVERIFY(QDir().mkpath(secondDirectory));
    const QString firstImage = createImage(directory, QStringLiteral("first/gallery.png"));
    QVERIFY(!firstImage.isEmpty());

    BrowseController controller(std::make_shared<QtImageDecoder>(), firstDirectory);
    controller.setGalleryPath(firstImage);
    QTRY_VERIFY_WITH_TIMEOUT(controller.galleryImageReady(), 2000);
    QVERIFY(controller.galleryImageSize().isValid());

    QSignalSpy galleryChanged(&controller, &BrowseController::galleryImageChanged);
    controller.openDirectory(secondDirectory);

    QVERIFY(!controller.galleryImageReady());
    QVERIFY(!controller.galleryImageSize().isValid());
    QVERIFY(controller.galleryInfoText().isEmpty());
    QVERIFY(galleryChanged.count() >= 1);
}

void QmlWorkspaceControllerTests::browseFileDialogsAreRequestedByQmlAndActionsStayInBackend() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString imagePath = createImage(directory, QStringLiteral("original.png"));
    QVERIFY(!imagePath.isEmpty());
    QVERIFY(QDir(directory.path()).mkdir(QStringLiteral("other")));

    BrowseController controller(std::make_shared<QtImageDecoder>(), directory.path());

    QSignalSpy directoryRequest(&controller, &BrowseController::directorySelectionRequested);
    controller.chooseDirectory();
    QCOMPARE(directoryRequest.size(), 1);
    QCOMPARE(directoryRequest.constFirst().constFirst().toUrl(),
             QUrl::fromLocalFile(directory.path()));

    const QString otherDirectory = QDir(directory.path()).filePath(QStringLiteral("other"));
    controller.openDirectoryUrl(QUrl::fromLocalFile(otherDirectory));
    QCOMPARE(controller.currentDirectory(), QFileInfo(otherDirectory).absoluteFilePath());
    controller.openDirectory(directory.path());

    controller.selectPath(imagePath);
    QSignalSpy renameRequest(&controller, &BrowseController::renameRequested);
    controller.renameSelected();
    QCOMPARE(renameRequest.size(), 1);
    QCOMPARE(renameRequest.constFirst().constFirst().toString(), QStringLiteral("original.png"));
    QVERIFY(!controller.renameSelectedTo(QStringLiteral("../invalid.png")).isEmpty());

    const QString renamedPath = directory.filePath(QStringLiteral("renamed.png"));
    QCOMPARE(controller.renameSelectedTo(QStringLiteral("renamed.png")), QString{});
    QVERIFY(QFileInfo::exists(renamedPath));
    QVERIFY(!QFileInfo::exists(imagePath));
    QCOMPARE(controller.selectedPaths(), QStringList{renamedPath});

    QSignalSpy trashRequest(&controller, &BrowseController::trashConfirmationRequested);
    controller.moveSelectedToTrash();
    QCOMPARE(trashRequest.size(), 1);
    QCOMPARE(trashRequest.constFirst().constFirst().toInt(), 1);

    const QString missingPath = directory.filePath(QStringLiteral("missing.png"));
    controller.selectPath(missingPath);
    const QString trashError = controller.moveSelectedToTrashConfirmed();
    QVERIFY(trashError.contains(QStringLiteral("missing.png")));
    QCOMPARE(controller.selectionCount(), 0);
}

void QmlWorkspaceControllerTests::imagePropertiesAreExposedWithoutWidgetUi() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString imagePath = createImage(directory, QStringLiteral("properties.png"));
    QVERIFY(!imagePath.isEmpty());

    ImageLoader loader(std::make_shared<QtImageDecoder>());
    ImagePropertiesController properties(&loader);
    QSignalSpy stateSpy(&properties, &ImagePropertiesController::stateChanged);
    properties.loadPath(imagePath);
    QTRY_VERIFY_WITH_TIMEOUT(!properties.loading(), 5000);
    QVERIFY(stateSpy.size() >= 2);
    QCOMPARE(properties.fileName(), QStringLiteral("properties.png"));
    QVERIFY(!properties.directory());
    QVERIFY(properties.errorText().isEmpty());
    QVERIFY(properties.basicFields().size() >= 7);
    const auto hasBasicField = [&properties](const QString& label) {
        return std::any_of(properties.basicFields().cbegin(), properties.basicFields().cend(),
                           [&label](const QVariant& entry) {
                               return entry.toMap().value(QStringLiteral("label")).toString() ==
                                      label;
                           });
    };
    QVERIFY(hasBasicField(QStringLiteral("Source Color Space")));
    QVERIFY(hasBasicField(QStringLiteral("Display Color Space")));
    QVERIFY(hasBasicField(QStringLiteral("Display Transfer")));
    QVERIFY(properties.exifFields().size() >= 16);

    QSignalSpy histogramSpy(&properties, &ImagePropertiesController::histogramChanged);
    properties.requestHistogram(0);
    QTRY_VERIFY_WITH_TIMEOUT(properties.histogram(0).value(QStringLiteral("valid")).toBool(), 5000);
    QCOMPARE(properties.histogram(0).value(QStringLiteral("channels")).toList().size(), 4);
    QCOMPARE(properties.histogram(0).value(QStringLiteral("maximumValue")).toInt(), 255);
    QVERIFY(!properties.hasHistogramSource());

    properties.loadPath(directory.path());
    QCOMPARE(properties.directory(), true);
    QCOMPARE(properties.basicFields().size(), 3);
    QVERIFY(properties.exifFields().isEmpty());
}

void QmlWorkspaceControllerTests::fullScreenPresentationLifecycleIsIdempotent() {
    FullScreenPresentationController controller;
    QSignalSpy activeSpy(&controller, &FullScreenPresentationController::activeChanged);
    QWindow window;

    QVERIFY(!controller.active());
    QVERIFY(!controller.begin(nullptr));
    QVERIFY(controller.begin(&window));
    QVERIFY(controller.active());
    QCOMPARE(activeSpy.size(), 1);

    QVERIFY(controller.begin(&window));
    QVERIFY(controller.active());
    QCOMPARE(activeSpy.size(), 1);

    controller.end();
    QVERIFY(!controller.active());
    QCOMPARE(activeSpy.size(), 2);

    controller.end();
    QVERIFY(!controller.active());
    QCOMPARE(activeSpy.size(), 2);
}

void QmlWorkspaceControllerTests::imageCanvasSmoothDisplayCanBeToggled() {
    QmlImageCanvas canvas;
    QSignalSpy smoothDisplaySpy(&canvas, &QmlImageCanvas::smoothDisplayChanged);

    QVERIFY(canvas.smoothDisplay());
    canvas.setSmoothDisplay(false);
    QVERIFY(!canvas.smoothDisplay());
    QCOMPARE(smoothDisplaySpy.size(), 1);

    canvas.setSmoothDisplay(false);
    QCOMPARE(smoothDisplaySpy.size(), 1);

    canvas.setSmoothDisplay(true);
    QVERIFY(canvas.smoothDisplay());
    QCOMPARE(smoothDisplaySpy.size(), 2);
}

void QmlWorkspaceControllerTests::fullScreenCanvasPromotesRawPreviewWhenProbingSourceSamples() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("frame.raw"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("fixture"), 7);
    file.close();

    auto decoder = std::make_shared<RawPreviewTrackingDecoder>();
    ImageLoader loader(decoder);
    FullScreenController controller(&loader);
    controller.open({path}, 0);
    QTRY_COMPARE_WITH_TIMEOUT(decoder->count(DecodePurpose::Preview), 1, 2000);

    QmlImageCanvas canvas;
    canvas.setWidth(200);
    canvas.setHeight(200);
    controller.attachCanvas(&canvas);
    QCOMPARE(canvas.imageCount(), 1);

    QSignalSpy probeSpy(&canvas, &QmlImageCanvas::pixelHovered);
    QSignalSpy fullSpy(&canvas, &QmlImageCanvas::pixelProbeFullResolutionRequested);
    const QPointF center(100, 100);
    QHoverEvent hover(QEvent::HoverMove, center, center, center);
    QCoreApplication::sendEvent(&canvas, &hover);
    QCOMPARE(probeSpy.count(), 1);
    // Proxy pixels are never presented as source samples: either the pending state or, when the
    // controller's own promotion already landed, the real RAW-depth RGB value.
    const QString firstText = probeSpy.last().at(2).toString();
    QVERIFY2(firstText == QStringLiteral("Loading pixel data…") ||
                 firstText == QStringLiteral("RGB(11,0,0)"),
             qPrintable(firstText));
    QCOMPARE(fullSpy.count(), 1);

    // The promotion delivers sensor samples for the same position.
    QTRY_COMPARE_WITH_TIMEOUT(decoder->count(DecodePurpose::Full), 1, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(
        canvas.frames().value(0) && !canvas.frames().value(0)->sourceSamplesPending, 2000);
    probeSpy.clear();
    const QPointF offset = center + QPointF(2.0, 2.0);
    QHoverEvent promoted(QEvent::HoverMove, offset, offset, center);
    QCoreApplication::sendEvent(&canvas, &promoted);
    QCOMPARE(probeSpy.count(), 1);
    QCOMPARE(probeSpy.last().at(1).toPoint(), QPoint(2, 2));
    QCOMPARE(probeSpy.last().at(2).toString(), QStringLiteral("RGB(11,0,0)"));
    QVERIFY(probeSpy.last().at(3).toBool());
    // Repeating the hover does not request the full decode again.
    QCoreApplication::sendEvent(&canvas, &promoted);
    QCOMPARE(fullSpy.count(), 1);
}

void QmlWorkspaceControllerTests::canvasProbesPixelsFromWindowHoverEvents() {
    QQuickWindow window;
    window.resize(400, 300);
    QmlImageCanvas canvas(window.contentItem());
    canvas.setWidth(400);
    canvas.setHeight(300);

    auto frame = std::make_shared<ImageFrame>();
    QImage image(8, 8, QImage::Format_RGBA8888);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            image.setPixelColor(x, y, QColor(x * 16, y * 16, 0, 255));
        }
    }
    frame->descriptor.size = image.size();
    frame->storage = std::move(image);
    canvas.setFrames({frame});

    QSignalSpy probeSpy(&canvas, &QmlImageCanvas::pixelHovered);
    const QPointF center(200, 150);
    QHoverEvent centerHover(QEvent::HoverMove, center, center, center);
    // Only the window sees this event. Full-display presentation can leave item-level hover
    // tracking stale, so the canvas observes the window directly.
    QCoreApplication::sendEvent(&window, &centerHover);
    QCOMPARE(probeSpy.count(), 1);
    QCOMPARE(probeSpy.last().at(0).toInt(), 0);
    QCOMPARE(probeSpy.last().at(1).toPoint(), QPoint(4, 4));
    QCOMPARE(probeSpy.last().at(2).toString(), QStringLiteral("RGB(64,64,0)"));
    QVERIFY(probeSpy.last().at(3).toBool());

    // Delivery to the item as well must not report the same cursor position twice.
    QCoreApplication::sendEvent(&canvas, &centerHover);
    QCOMPARE(probeSpy.count(), 1);

    const QPointF offset = center + QPointF(4.0, 4.0);
    QHoverEvent offsetHover(QEvent::HoverMove, offset, offset, offset);
    QCoreApplication::sendEvent(&window, &offsetHover);
    QCOMPARE(probeSpy.count(), 2);
    QCOMPARE(probeSpy.last().at(1).toPoint(), QPoint(4, 4));
    QVERIFY(probeSpy.last().at(3).toBool());

    QHoverEvent leave(QEvent::HoverLeave, QPointF(-40, -40), QPointF(-40, -40), center);
    QCoreApplication::sendEvent(&window, &leave);
    QCOMPARE(probeSpy.count(), 3);
    QVERIFY(!probeSpy.last().at(3).toBool());
}

void QmlWorkspaceControllerTests::canvasPixelProbeReportsExactPixelAtHighZoom() {
    const auto makeFrame = [](int baseRed) {
        auto frame = std::make_shared<ImageFrame>();
        QImage image(8, 8, QImage::Format_RGBA8888);
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                image.setPixelColor(x, y, QColor(baseRed + x * 16, y * 16, 0, 255));
            }
        }
        frame->descriptor.size = image.size();
        frame->storage = std::move(image);
        return frame;
    };

    QmlImageCanvas canvas;
    canvas.setWidth(101);
    canvas.setHeight(101);
    canvas.setFrames({makeFrame(0), makeFrame(128)});
    canvas.actualPixelsAll();

    // Two side-by-side cells are (101 - 2) / 2 = 49.5 px wide, so an exact probe has to use the
    // fractional cell geometry instead of a rounded viewport of the same item.
    const QPointF imageCenterInCell(24.75, 50.5);
    for (int step = 0; step < 22; ++step) {
        QWheelEvent wheel(imageCenterInCell, imageCenterInCell, {}, QPoint(0, 120), Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(&canvas, &wheel);
    }
    QVERIFY(std::abs(canvas.effectiveViewState(0).pixelsPerImagePixel - std::pow(1.2, 22.0)) <
            0.001);

    QSignalSpy probeSpy(&canvas, &QmlImageCanvas::pixelHovered);
    const auto hover = [&](const QPointF& position) {
        probeSpy.clear();
        QHoverEvent event(QEvent::HoverMove, position, position, position);
        QCoreApplication::sendEvent(&canvas, &event);
        return probeSpy.count() == 1 ? probeSpy.last() : QList<QVariant>{};
    };

    // Cursor anchored zoom keeps the image center exactly under the pointer, so the boundary
    // between columns 3 and 4 stays on the cell center and a 0.2 px offset decides the pixel.
    const QList<QVariant> insideRight = hover(imageCenterInCell + QPointF(0.2, -0.2));
    QCOMPARE(insideRight.size(), 4);
    QCOMPARE(insideRight.at(0).toInt(), 0);
    QCOMPARE(insideRight.at(1).toPoint(), QPoint(4, 3));
    QCOMPARE(insideRight.at(2).toString(), QStringLiteral("RGB(64,48,0)"));
    QVERIFY(insideRight.at(3).toBool());

    const QList<QVariant> insideLeft = hover(imageCenterInCell + QPointF(-0.2, 0.2));
    QCOMPARE(insideLeft.size(), 4);
    QCOMPARE(insideLeft.at(1).toPoint(), QPoint(3, 4));
    QCOMPARE(insideLeft.at(2).toString(), QStringLiteral("RGB(48,64,0)"));
    QVERIFY(insideLeft.at(3).toBool());
}

void QmlWorkspaceControllerTests::fullScreenSessionKeepsNavigationAndFileOperationsOutOfQml() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString first = createImage(directory, QStringLiteral("first.png"));
    const QString second = createImage(directory, QStringLiteral("second.png"));
    QVERIFY(!first.isEmpty());
    QVERIFY(!second.isEmpty());

    ImageLoader loader(std::make_shared<QtImageDecoder>());
    FullScreenController controller(&loader);
    QSignalSpy filesystemSpy(&controller, &FullScreenController::filesystemChanged);
    controller.open({first, second}, 0);
    QCOMPARE(controller.currentPath(), first);
    QCOMPARE(controller.fileType(), QStringLiteral("PNG"));
    QVERIFY(!controller.fileSizeText().isEmpty());
    for (const QChar character : controller.fileSizeText())
        QVERIFY2(!character.isSpace(), qPrintable(controller.fileSizeText()));
    QTRY_VERIFY_WITH_TIMEOUT(controller.imageSize().isValid(), 2000);
    QVERIFY(!controller.canGoPrevious());
    QVERIFY(controller.canGoNext());
    controller.showNext();
    QCOMPARE(controller.currentPath(), second);

    QCOMPARE(controller.renameCurrentTo(QStringLiteral("renamed.png")), QString{});
    const QString renamed = directory.filePath(QStringLiteral("renamed.png"));
    QCOMPARE(controller.currentPath(), renamed);
    QVERIFY(QFileInfo::exists(renamed));
    QCOMPARE(filesystemSpy.size(), 1);

    controller.closeSession();
    QVERIFY(controller.paths().isEmpty());
    QCOMPARE(controller.currentIndex(), -1);
    QVERIFY(controller.currentPath().isEmpty());
    QVERIFY(!controller.loading());

    // Trash integration is exercised by the platform/UI suites; temporary test locations are
    // intentionally not assumed to be trash-capable on every CI host.
}

void QmlWorkspaceControllerTests::fullScreenExactPixelsPromotePreviewWithoutLosingFullResolution() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("large.png"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("fixture"), 7);
    file.close();

    auto decoder = std::make_shared<PurposeTrackingDecoder>(QSize(12000, 10000));
    ImageLoader loader(decoder);
    FullScreenController controller(&loader);
    QSignalSpy frameSpy(&controller, &FullScreenController::stateChanged);
    controller.open({path}, 0);
    QTRY_COMPARE_WITH_TIMEOUT(decoder->count(DecodePurpose::Preview), 1, 2000);
    QCOMPARE(decoder->count(DecodePurpose::Full), 0);

    controller.actualPixels();
    QTRY_COMPARE_WITH_TIMEOUT(decoder->count(DecodePurpose::Full), 1, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(frameSpy.size() >= 3, 2000);
}

void QmlWorkspaceControllerTests::
    fullScreenAutomaticallyPromotesBudgetedImageAfterNavigationSettles() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QStringList paths;
    for (const QString& name : {QStringLiteral("first.png"), QStringLiteral("second.png")}) {
        const QString path = directory.filePath(name);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("fixture"), 7);
        file.close();
        paths.append(path);
    }

    auto decoder = std::make_shared<PurposeTrackingDecoder>(QSize(4000, 3000));
    ImageLoader loader(decoder);
    FullScreenController controller(&loader);
    controller.open(paths, 0);
    QTRY_COMPARE_WITH_TIMEOUT(decoder->count(DecodePurpose::Preview), 1, 2000);
    controller.showNext();
    QTRY_COMPARE_WITH_TIMEOUT(decoder->count(DecodePurpose::Preview), 2, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(decoder->count(DecodePurpose::Full), 1, 2000);
    QCOMPARE(controller.currentPath(), paths.at(1));
}

void QmlWorkspaceControllerTests::
    compareDefersOversizedAutomaticFullLoadsButExactToolsStillPromote() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QStringList paths;
    for (const QString& name : {QStringLiteral("a.png"), QStringLiteral("b.png")}) {
        const QString path = directory.filePath(name);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("fixture"), 7);
        file.close();
        paths.append(path);
    }

    auto decoder = std::make_shared<PurposeTrackingDecoder>(QSize(12000, 10000));
    ImageLoader loader(decoder);
    CompareController controller(&loader);
    controller.setPixelValueVisible(false);
    controller.setHistogramVisible(false);
    controller.setPaths(paths);
    QTRY_COMPARE_WITH_TIMEOUT(decoder->count(DecodePurpose::Preview), 2, 2000);
    QTest::qWait(400);
    QCOMPARE(decoder->count(DecodePurpose::Full), 0);

    controller.setPixelValueVisible(true);
    QTRY_COMPARE_WITH_TIMEOUT(decoder->count(DecodePurpose::Full), 2, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(controller.frame(0) && controller.frame(1), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(controller.frame(0)->qImage()->pixelColor(0, 0), QColor(Qt::green),
                              2000);
    QTRY_COMPARE_WITH_TIMEOUT(controller.frame(1)->qImage()->pixelColor(0, 0), QColor(Qt::green),
                              2000);
}

void QmlWorkspaceControllerTests::compareAutomaticallyPromotesBudgetedImages() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QStringList paths;
    for (const QString& name : {QStringLiteral("a.png"), QStringLiteral("b.png")}) {
        const QString path = directory.filePath(name);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("fixture"), 7);
        file.close();
        paths.append(path);
    }

    auto decoder = std::make_shared<PurposeTrackingDecoder>(QSize(4000, 3000));
    ImageLoader loader(decoder);
    CompareController controller(&loader);
    controller.setPixelValueVisible(false);
    controller.setHistogramVisible(false);
    controller.setPaths(paths);
    QTRY_COMPARE_WITH_TIMEOUT(decoder->count(DecodePurpose::Preview), 2, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(decoder->count(DecodePurpose::Full), 2, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(controller.frame(0) && controller.frame(1), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(controller.frame(0)->qImage()->pixelColor(0, 0), QColor(Qt::green),
                              2000);
    QTRY_COMPARE_WITH_TIMEOUT(controller.frame(1)->qImage()->pixelColor(0, 0), QColor(Qt::green),
                              2000);
}

void QmlWorkspaceControllerTests::rawParameterEditorAppliesValuesAndManagesPresetsWithoutWidgets() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString rawPath = directory.filePath(QStringLiteral("4x4_nv12.raw"));
    QFile rawFile(rawPath);
    QVERIFY(rawFile.open(QIODevice::WriteOnly));
    QCOMPARE(rawFile.write(QByteArray(1024, '\0')), 1024);
    rawFile.close();

    ImageLoader loader(std::make_shared<RawParameterColorDecoder>());
    RawImageParameters initial;
    initial.size = {4, 4};
    initial.format = RawPixelFormat::NV12;
    initial.rowStride = 4;
    initial.chromaStride = 4;
    loader.setRawParameters(rawPath, initial);

    RawParametersController controller(&loader);
    controller.loadPath(rawPath);
    QCOMPARE(controller.values().value(QStringLiteral("width")).toInt(), 4);
    QVERIFY(controller.yuvFormat());
    QSignalSpy appliedSpy(&controller, &RawParametersController::parametersApplied);
    controller.setValue(QStringLiteral("width"), 8);
    controller.setValue(QStringLiteral("rowStride"), 8);
    controller.setValue(QStringLiteral("chromaStride"), 8);
    QTRY_VERIFY_WITH_TIMEOUT(!appliedSpy.isEmpty(), 2000);
    QCOMPARE(loader.rawParameters(rawPath)->size.width(), 8);
    QCOMPARE(loader.rawParameters(rawPath)->rowStride, 8);

    controller.setValue(QStringLiteral("format"), static_cast<int>(RawPixelFormat::Raw16));
    controller.setValue(QStringLiteral("rowStride"), 16);
    controller.setValue(QStringLiteral("bayerSampling"),
                        static_cast<int>(BayerSampling::QuadBayer4x4));
    QTRY_COMPARE_WITH_TIMEOUT(
        loader.rawParameters(rawPath)->bayerSampling, BayerSampling::QuadBayer4x4, 2000);

    const QString presetName = QStringLiteral("qml-editor-test");
    QCOMPARE(controller.savePreset(presetName), QString{});
    QVERIFY(controller.presetNames().contains(presetName));
    QCOMPARE(controller.selectedPreset(), presetName);
    QCOMPARE(controller.deleteSelectedPreset(), QString{});
    QVERIFY(!controller.presetNames().contains(presetName));
}

void QmlWorkspaceControllerTests::comparePreferencesPersistAndHorizontalModeIsUnavailable() {
    QSettings settings;
    settings.remove(QStringLiteral("compare"));

    CompareController first(nullptr);
    QVERIFY(first.fileInformationVisible());
    QVERIFY(!first.exifVisible());
    QVERIFY(!first.histogramVisible());
    QVERIFY(!first.pixelValueVisible());
    QVERIFY(first.thumbnailVisible());

    first.setFileInformationVisible(false);
    first.setExifVisible(true);
    first.setHistogramVisible(true);
    first.setPixelValueVisible(true);
    first.setThumbnailVisible(false);
    first.setPresentationMode(2);
    QCOMPARE(first.presentationMode(), 1);

    CompareController restored(nullptr);
    QVERIFY(!restored.fileInformationVisible());
    QVERIFY(restored.exifVisible());
    QVERIFY(restored.histogramVisible());
    QVERIFY(restored.pixelValueVisible());
    QVERIFY(!restored.thumbnailVisible());
}

void QmlWorkspaceControllerTests::compareUsesSourcePixelTextAndLumaOnlyHistogram() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("rgba.png"));
    QImage image(2, 2, QImage::Format_RGBA8888);
    image.fill(QColor(10, 20, 30, 40));
    QVERIFY(image.save(path));

    BrowseWorkspaceController workspace(std::make_shared<QtImageDecoder>(), directory.path());
    CompareController compare(workspace.loader());
    QSignalSpy frameSpy(&compare, &CompareController::frameChanged);
    compare.setPaths({path, path});
    QTRY_VERIFY_WITH_TIMEOUT(frameSpy.size() >= 2, 5000);

    const QStringList fileInformation = compare.fileText(0).split(QLatin1Char(','));
    QCOMPARE(fileInformation.size(), 3);
    QCOMPARE(fileInformation.at(0), QStringLiteral("rgba.png"));
    QCOMPARE(fileInformation.at(1), QStringLiteral("2*2"));
    for (const QChar character : fileInformation.at(2))
        QVERIFY2(!character.isSpace(), qPrintable(fileInformation.at(2)));

    const QVariantList values = compare.pixelTexts(0, 0, 0);
    QCOMPARE(values.size(), 2);
    QCOMPARE(values.at(0).toString(), QStringLiteral("(0,0) RGB(10,20,30)"));
    QVERIFY(!values.at(0).toString().contains(QStringLiteral("RAW")));
    QVERIFY(!values.at(0).toString().contains(QStringLiteral("YUV")));
    QVERIFY(!values.at(0).toString().contains(QChar(0x2022)));

    QSignalSpy histogramSpy(&compare, &CompareController::histogramChanged);
    compare.requestHistogram(0);
    QTRY_VERIFY_WITH_TIMEOUT(!histogramSpy.isEmpty(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(compare.histogram(0).value(QStringLiteral("valid")).toBool(), 5000);
    const QVariantMap histogram = compare.histogram(0);
    const QVariantList channels = histogram.value(QStringLiteral("channels")).toList();
    QCOMPARE(channels.size(), 1);
    QCOMPARE(channels.constFirst().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("Luma"));
    QVERIFY(!histogram.contains(QStringLiteral("summary")));

    compare.closeSession();
    QVERIFY(compare.paths().isEmpty());
    QVERIFY(!compare.frame(0));
    QVERIFY(compare.histogram(0).isEmpty());
}

void QmlWorkspaceControllerTests::compareViewSyncTemporarilyBypassesWithControl() {
    const auto makeFrame = [] {
        auto frame = std::make_shared<ImageFrame>();
        QImage image(100, 100, QImage::Format_RGBA8888);
        image.fill(Qt::black);
        frame->descriptor.size = image.size();
        frame->storage = std::move(image);
        return frame;
    };

    QmlImageCanvas canvas;
    canvas.setWidth(400);
    canvas.setHeight(200);
    canvas.setFrames({makeFrame(), makeFrame()});
    canvas.setSynchronized(true);
    canvas.setPresentationMode(2);
    QCOMPARE(canvas.presentationMode(), 1);
    canvas.setPresentationMode(0);
    const QVariantMap fittedNavigation = canvas.navigationState(0);
    QVERIFY(fittedNavigation.value(QStringLiteral("visible")).toBool());
    QVERIFY(!fittedNavigation.value(QStringLiteral("zoom")).toString().isEmpty());
    const QRectF fittedViewport = fittedNavigation.value(QStringLiteral("viewport")).toRectF();
    QCOMPARE(fittedViewport, QRectF(0.0, 0.0, 1.0, 1.0));
    canvas.actualPixelsAll();

    const double initialScale = canvas.effectiveViewState(0).pixelsPerImagePixel;
    QCOMPARE(initialScale, 1.0);
    QWheelEvent synchronizedWheel(QPointF(50, 100), QPointF(50, 100), {}, QPoint(0, 120),
                                  Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(&canvas, &synchronizedWheel);
    QVERIFY(canvas.effectiveViewState(0).pixelsPerImagePixel > initialScale);
    QCOMPARE(canvas.effectiveViewState(0).pixelsPerImagePixel,
             canvas.effectiveViewState(1).pixelsPerImagePixel);

    canvas.actualPixelsAll();
#if defined(Q_OS_MACOS)
    // Qt maps the physical Control key to MetaModifier on macOS.
    constexpr auto independentModifier = Qt::MetaModifier;
#else
    constexpr auto independentModifier = Qt::ControlModifier;
#endif
    QWheelEvent independentWheel(QPointF(50, 100), QPointF(50, 100), {}, QPoint(0, 120),
                                 Qt::NoButton, independentModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(&canvas, &independentWheel);
    const double independentlyAdjustedScale = canvas.effectiveViewState(0).pixelsPerImagePixel;
    const double unchangedScale = canvas.effectiveViewState(1).pixelsPerImagePixel;
    QVERIFY(independentlyAdjustedScale > initialScale);
    QCOMPARE(unchangedScale, initialScale);
    const double independentRatio = independentlyAdjustedScale / unchangedScale;

    // Releasing Ctrl does not reconcile either view. The next ordinary input
    // applies the same scale delta to both independent baselines.
    QCOMPARE(canvas.effectiveViewState(0).pixelsPerImagePixel, independentlyAdjustedScale);
    QCOMPARE(canvas.effectiveViewState(1).pixelsPerImagePixel, unchangedScale);

    QWheelEvent resumedWheel(QPointF(250, 100), QPointF(250, 100), {}, QPoint(0, 120), Qt::NoButton,
                             Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(&canvas, &resumedWheel);
    QVERIFY(canvas.effectiveViewState(0).pixelsPerImagePixel > independentlyAdjustedScale);
    QVERIFY(canvas.effectiveViewState(1).pixelsPerImagePixel > unchangedScale);
    const double resumedRatio = canvas.effectiveViewState(0).pixelsPerImagePixel /
                                canvas.effectiveViewState(1).pixelsPerImagePixel;
    QVERIFY(std::abs(resumedRatio - independentRatio) < 0.000001);
    const QVariantMap navigation = canvas.navigationState(0);
    QVERIFY(navigation.value(QStringLiteral("visible")).toBool());
    QVERIFY(navigation.value(QStringLiteral("width")).toInt() <= 96);
    QVERIFY(navigation.value(QStringLiteral("height")).toInt() <= 71);
}

void QmlWorkspaceControllerTests::otaDownloadsAndVerifiesPlatformInstaller() {
    const QString installerName = testInstallerName();
    if (installerName.isEmpty())
        QSKIP("OTA installers are currently supported on macOS and Windows only");

    UpdateHttpServer server;
    QVERIFY(server.listen());
    const QByteArray installerData("verified installer fixture\n");
    const QByteArray digest =
        QCryptographicHash::hash(installerData, QCryptographicHash::Sha256).toHex();
    server.respond("/release",
                   updateReleaseJson(server, installerName, installerData.size(), digest));
    server.respond("/installer", installerData);

    qputenv("ISPVIEW_UPDATE_API_URL", server.url("/release").toString().toUtf8());
    const auto resetEnvironment = qScopeGuard([] { qunsetenv("ISPVIEW_UPDATE_API_URL"); });
    auto* application = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
    QVERIFY(application);
    AppSettings settings(application);
    settings.checkForUpdates();
    QTRY_COMPARE_WITH_TIMEOUT(settings.updateState(), QStringLiteral("available"), 3000);
    QCOMPARE(settings.latestVersion(), QStringLiteral("99.0.0"));

    settings.downloadUpdate();
    QTRY_VERIFY2_WITH_TIMEOUT(settings.updateState() == QStringLiteral("ready"), qPrintable(settings.updateError()), 3000);
    QCOMPARE(settings.updateDownloadProgress(), 100);
    QVERIFY(!settings.downloadedUpdatePath().isEmpty());
    QFile downloaded(settings.downloadedUpdatePath());
    QVERIFY(downloaded.open(QIODevice::ReadOnly));
    QCOMPARE(downloaded.readAll(), installerData);
    downloaded.close();
    QVERIFY(QFile::remove(settings.downloadedUpdatePath()));
}

void QmlWorkspaceControllerTests::otaRejectsInstallerWithWrongChecksum() {
    const QString installerName = testInstallerName();
    if (installerName.isEmpty())
        QSKIP("OTA installers are currently supported on macOS and Windows only");

    UpdateHttpServer server;
    QVERIFY(server.listen());
    const QByteArray installerData("tampered installer fixture\n");
    server.respond("/release", updateReleaseJson(server, installerName, installerData.size(),
                                                  QByteArray(64, '0')));
    server.respond("/installer", installerData);

    qputenv("ISPVIEW_UPDATE_API_URL", server.url("/release").toString().toUtf8());
    const auto resetEnvironment = qScopeGuard([] { qunsetenv("ISPVIEW_UPDATE_API_URL"); });
    auto* application = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
    QVERIFY(application);
    AppSettings settings(application);
    settings.checkForUpdates();
    QTRY_COMPARE_WITH_TIMEOUT(settings.updateState(), QStringLiteral("available"), 3000);
    settings.downloadUpdate();
    QTRY_COMPARE_WITH_TIMEOUT(settings.updateState(), QStringLiteral("error"), 3000);
    QVERIFY(settings.downloadedUpdatePath().isEmpty());
    QVERIFY2(settings.updateError().contains(QStringLiteral("SHA-256")), qPrintable(settings.updateError()));
}

void QmlWorkspaceControllerTests::otaDownloadCanBeCancelled() {
    const QString installerName = testInstallerName();
    if (installerName.isEmpty())
        QSKIP("OTA installers are currently supported on macOS and Windows only");

    UpdateHttpServer server;
    QVERIFY(server.listen());
    const QByteArray installerData("installer fixture\n");
    const QByteArray digest =
        QCryptographicHash::hash(installerData, QCryptographicHash::Sha256).toHex();
    server.respond("/release",
                   updateReleaseJson(server, installerName, installerData.size(), digest));

    qputenv("ISPVIEW_UPDATE_API_URL", server.url("/release").toString().toUtf8());
    const auto resetEnvironment = qScopeGuard([] { qunsetenv("ISPVIEW_UPDATE_API_URL"); });
    auto* application = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
    QVERIFY(application);
    AppSettings settings(application);
    settings.checkForUpdates();
    QTRY_COMPARE_WITH_TIMEOUT(settings.updateState(), QStringLiteral("available"), 3000);
    settings.downloadUpdate();
    QVERIFY2(settings.updateState() == QStringLiteral("downloading"), qPrintable(settings.updateError()));
    settings.cancelUpdateDownload();
    QTRY_COMPARE_WITH_TIMEOUT(settings.updateState(), QStringLiteral("available"), 3000);
    QCOMPARE(settings.updateDownloadProgress(), -1);
    QVERIFY(settings.downloadedUpdatePath().isEmpty());
}

void QmlWorkspaceControllerTests::diagnosticsReceivesPresentationSessionEvents() {
    QVERIFY(diagnosticsService_);
    QVERIFY(diagnosticsService_->flush());
    const QString log = sessionLogText(*diagnosticsService_);
    // Compare and full screen are the highest-risk presentation paths, so their lifecycle has to be
    // visible in the export a user sends to support.
    QVERIFY2(log.contains(QStringLiteral("compare.session_open")), qPrintable(log.left(400)));
    QVERIFY2(log.contains(QStringLiteral("fullscreen.session_open")), qPrintable(log.left(400)));
    QVERIFY(log.contains(QStringLiteral("compare.presentation_mode")));
}

} // namespace ispview

QTEST_MAIN(ispview::QmlWorkspaceControllerTests)
#include "qml_workspace_controller_tests.moc"
