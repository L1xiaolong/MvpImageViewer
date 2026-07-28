#include "io/qt_image_decoder.h"
#include "io/encoded_color_management.h"
#include "io/image_loader.h"
#include "qml/browse_controller.h"
#include "qml/app_settings.h"
#include "qml/browse_workspace_controller.h"
#include "qml/compare_controller.h"
#include "qml/image_properties_controller.h"
#include "qml/full_screen_controller.h"
#include "qml/raw_parameters_controller.h"
#include "qml/qml_image_canvas.h"
#include "qml/thumbnail_image_provider.h"
#include "browser/thumbnail_model.h"

#include <QImage>
#include <QGuiApplication>
#include <QHash>
#include <QDir>
#include <QFile>
#include <QUrl>
#include <QWheelEvent>
#include <QSettings>
#include <QSignalSpy>
#include <QMutex>
#include <QMutexLocker>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>
#include <atomic>
#include <memory>

namespace ispview {
namespace {

class RawParameterColorDecoder final : public IImageDecoder {
  public:
    [[nodiscard]] bool canDecode(const QString&) const override { return true; }

    [[nodiscard]] DecodeResult decode(const DecodeRequest& request) const override {
        calls.fetch_add(1, std::memory_order_relaxed);
        QImage image(8, 8, QImage::Format_RGBA8888);
        image.fill(request.rawParameters && request.rawParameters->size.width() == 4
                       ? QColor(Qt::red) : QColor(Qt::green));
        auto frame = std::make_shared<ImageFrame>();
        frame->descriptor.size = image.size();
        frame->metadata.path = request.path;
        frame->metadata.sourceSize = request.rawParameters
                                         ? request.rawParameters->size : image.size();
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

BrowseController* paneAt(const BrowseWorkspaceController& workspace, int index) {
    const QVariantList panes = workspace.panes();
    return qobject_cast<BrowseController*>(panes.at(index).value<QObject*>());
}

QString createImage(QTemporaryDir& directory, const QString& name) {
    const QString path = directory.filePath(name);
    QImage image(4, 4, QImage::Format_RGBA8888);
    image.fill(Qt::black);
    if (!image.save(path)) return {};
    return path;
}

} // namespace

class QmlWorkspaceControllerTests final : public QObject {
    Q_OBJECT

  private:
    std::unique_ptr<QTemporaryDir> settingsDirectory_;

  private slots:
    void initTestCase();
    void addsActivatesAndClosesOneToFourPanes();
    void defersStartupDirectoryUntilExplicitlyStarted();
    void exposesNativeFolderNavigationStructure();
    void loadsFolderTreeChildrenWithoutNavigating();
    void usesPlatformFolderIconForThumbnailDirectories();
    void keepsPaneStateIndependent();
    void aggregatesUniqueSelectionsInStableOrder();
    void copiesDropsIntoSubfoldersAndAcrossPanes();
    void emptyPaneOpensDroppedFoldersAndImageLocations();
    void rawParametersRefreshEveryPaneAndQmlProvider();
    void galleryUsesPreviewUntilPixelProbeRequestsFullResolution();
    void browseFileDialogsAreRequestedByQmlAndActionsStayInBackend();
    void imagePropertiesAreExposedWithoutWidgetUi();
    void fullScreenSessionKeepsNavigationAndFileOperationsOutOfQml();
    void fullScreenExactPixelsPromotePreviewWithoutLosingFullResolution();
    void fullScreenAutomaticallyPromotesBudgetedImageAfterNavigationSettles();
    void rawParameterEditorAppliesValuesAndManagesPresetsWithoutWidgets();
    void comparePreferencesPersistAndHorizontalModeIsUnavailable();
    void compareUsesCompactRgbaPixelTextAndLumaOnlyHistogram();
    void compareViewSyncTemporarilyBypassesWithControl();
    void compareDefersOversizedAutomaticFullLoadsButExactToolsStillPromote();
    void compareAutomaticallyPromotesBudgetedImages();
    void applicationSettingsPersistAndRestoreDefaults();
};

void QmlWorkspaceControllerTests::initTestCase() {
    settingsDirectory_ = std::make_unique<QTemporaryDir>();
    QVERIFY(settingsDirectory_->isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("ISPViewTests"));
    QCoreApplication::setApplicationName(QStringLiteral("QmlWorkspaceControllerTests"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory_->path());
    QSettings().clear();
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
    QVERIFY(settings.honorExifOrientation());
    QCOMPARE(settings.canvasBackground(), QStringLiteral("neutral"));
    QCOMPARE(settings.shortcutFor(QStringLiteral("compare")), QStringLiteral("C"));
    QCOMPARE(settings.shortcutEntries().size(), 7);

    QSignalSpy languageSpy(&settings, &AppSettings::languageChanged);
    QSignalSpy themeSpy(&settings, &AppSettings::themeChanged);
    QSignalSpy shortcutsSpy(&settings, &AppSettings::shortcutsChanged);
    QSignalSpy colorDisplaySpy(&settings, &AppSettings::colorDisplayChanged);
    settings.setLanguage(QStringLiteral("en"));
    settings.setTheme(QStringLiteral("dark"));
    settings.setRestoreLastDirectory(false);
    settings.setConfirmTrash(false);
    settings.setAutomaticUpdateChecks(false);
    settings.setApplyEmbeddedColorProfiles(false);
    settings.setPreserveHighBitDepth(false);
    settings.setHonorExifOrientation(false);
    settings.setCanvasBackground(QStringLiteral("black"));
    QCOMPARE(settings.setShortcut(QStringLiteral("compare"),
                                  QStringLiteral("Ctrl+Shift+C")), QString{});
    QCOMPARE(settings.setShortcut(QStringLiteral("rename"),
                                  QStringLiteral("Ctrl+Shift+C")),
             QStringLiteral("compare"));
    QCOMPARE(settings.setShortcut(QStringLiteral("rename"),
                                  QStringLiteral("not a shortcut")),
             QStringLiteral("invalid"));

    QCOMPARE(languageSpy.count(), 1);
    QCOMPARE(themeSpy.count(), 1);
    QCOMPARE(shortcutsSpy.count(), 1);
    QCOMPARE(colorDisplaySpy.count(), 4);
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
    QVERIFY(!settings.honorExifOrientation());
    QCOMPARE(settings.canvasBackground(), QStringLiteral("black"));
    QVERIFY(!EncodedColorManagement::isEnabled());
    QVERIFY(!QtImageDecoder::preserveHighBitDepth());
    QVERIFY(!QtImageDecoder::autoOrientationEnabled());
    QCOMPARE(settings.shortcutFor(QStringLiteral("compare")),
             QStringLiteral("Ctrl+Shift+C"));
    QCOMPARE(QSettings().value(QStringLiteral("shortcuts/compare")).toString(),
             QStringLiteral("Ctrl+Shift+C"));

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
        QVERIFY(place.value(QStringLiteral("icon")).toString().startsWith(
            QStringLiteral("image://system-folder/")));
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
    if (!nativeIcon.isNull()) QCOMPARE(iconSize, nativeIcon.size());
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

    BrowseWorkspaceController workspace(std::make_shared<QtImageDecoder>(),
                                        sourceDirectory.path());
    BrowseController* first = paneAt(workspace, 0);
    first->copyDroppedUrlsInto({QUrl::fromLocalFile(source)}, child);
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(QDir(child).filePath(QStringLiteral("drag.png"))),
                             3000);

    workspace.addFileManagerPane();
    BrowseController* second = paneAt(workspace, 1);
    second->openDirectory(targetDirectory.path());
    second->copyDroppedUrls({QUrl::fromLocalFile(source)});
    QTRY_VERIFY_WITH_TIMEOUT(
        QFileInfo::exists(targetDirectory.filePath(QStringLiteral("drag.png"))), 3000);
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
            if (index.data(ThumbnailModel::PathRole).toString() == rawPath) return index;
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
    QImage firstImage = provider.requestImage(encodedPath + QStringLiteral("?v=first"), nullptr,
                                              QSize(16, 16));
    QCOMPARE(firstImage.pixelColor(0, 0), QColor(Qt::red));
    QCOMPARE(decoder->calls.load(std::memory_order_relaxed), 1);

    parameters.size = {8, 4};
    parameters.rowStride = 16;
    workspace.loader()->setRawParameters(rawPath, parameters);
    const QString refreshedUrl =
        indexForPath(paneAt(workspace, 1)).data(ThumbnailModel::ThumbnailUrlRole).toString();
    QVERIFY(refreshedUrl != secondUrl);
    QImage refreshedImage = provider.requestImage(encodedPath + QStringLiteral("?v=second"),
                                                  nullptr, QSize(16, 16));
    QCOMPARE(refreshedImage.pixelColor(0, 0), QColor(Qt::green));
    QCOMPARE(decoder->calls.load(std::memory_order_relaxed), 2);
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
    QVERIFY(!controller.probeGalleryPixel(0, 0).isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(decoder->count(DecodePurpose::Full), 1, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(
        controller.probeGalleryPixel(0, 0).contains(QStringLiteral("RGBA(0, 255, 0, 255)")),
        2000);
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
    QVERIFY(properties.exifFields().size() >= 16);

    QSignalSpy histogramSpy(&properties, &ImagePropertiesController::histogramChanged);
    properties.requestHistogram(0);
    QTRY_VERIFY_WITH_TIMEOUT(!histogramSpy.isEmpty(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(properties.histogram(0).value(QStringLiteral("valid")).toBool(), 5000);
    QCOMPARE(properties.histogram(0).value(QStringLiteral("channels")).toList().size(), 4);

    properties.loadPath(directory.path());
    QCOMPARE(properties.directory(), true);
    QCOMPARE(properties.basicFields().size(), 3);
    QVERIFY(properties.exifFields().isEmpty());
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

void QmlWorkspaceControllerTests::
    fullScreenExactPixelsPromotePreviewWithoutLosingFullResolution() {
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
    QTRY_COMPARE_WITH_TIMEOUT(controller.frame(0)->qImage()->pixelColor(0, 0),
                              QColor(Qt::green), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(controller.frame(1)->qImage()->pixelColor(0, 0),
                              QColor(Qt::green), 2000);
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
    QCOMPARE(controller.frame(0)->qImage()->pixelColor(0, 0), QColor(Qt::green));
    QCOMPARE(controller.frame(1)->qImage()->pixelColor(0, 0), QColor(Qt::green));
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

    first.setFileInformationVisible(false);
    first.setExifVisible(true);
    first.setHistogramVisible(true);
    first.setPixelValueVisible(true);
    first.setPresentationMode(2);
    QCOMPARE(first.presentationMode(), 1);

    CompareController restored(nullptr);
    QVERIFY(!restored.fileInformationVisible());
    QVERIFY(restored.exifVisible());
    QVERIFY(restored.histogramVisible());
    QVERIFY(restored.pixelValueVisible());
}

void QmlWorkspaceControllerTests::compareUsesCompactRgbaPixelTextAndLumaOnlyHistogram() {
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

    const QVariantList values = compare.pixelTexts(0, 0, 0);
    QCOMPARE(values.size(), 2);
    QCOMPARE(values.at(0).toString(), QStringLiteral("(0,0) RGBA(10,20,30,40)"));
    QVERIFY(!values.at(0).toString().contains(QStringLiteral("RAW")));
    QVERIFY(!values.at(0).toString().contains(QStringLiteral("YUV")));
    QVERIFY(!values.at(0).toString().contains(QChar(0x2022)));

    QSignalSpy histogramSpy(&compare, &CompareController::histogramChanged);
    compare.requestHistogram(0);
    QTRY_VERIFY_WITH_TIMEOUT(!histogramSpy.isEmpty(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(
        compare.histogram(0).value(QStringLiteral("valid")).toBool(), 5000);
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
    const double independentlyAdjustedScale =
        canvas.effectiveViewState(0).pixelsPerImagePixel;
    const double unchangedScale = canvas.effectiveViewState(1).pixelsPerImagePixel;
    QVERIFY(independentlyAdjustedScale > initialScale);
    QCOMPARE(unchangedScale, initialScale);
    const double independentRatio = independentlyAdjustedScale / unchangedScale;

    // Releasing Ctrl does not reconcile either view. The next ordinary input
    // applies the same scale delta to both independent baselines.
    QCOMPARE(canvas.effectiveViewState(0).pixelsPerImagePixel,
             independentlyAdjustedScale);
    QCOMPARE(canvas.effectiveViewState(1).pixelsPerImagePixel, unchangedScale);

    QWheelEvent resumedWheel(QPointF(250, 100), QPointF(250, 100), {}, QPoint(0, 120),
                             Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(&canvas, &resumedWheel);
    QVERIFY(canvas.effectiveViewState(0).pixelsPerImagePixel > independentlyAdjustedScale);
    QVERIFY(canvas.effectiveViewState(1).pixelsPerImagePixel > unchangedScale);
    const double resumedRatio = canvas.effectiveViewState(0).pixelsPerImagePixel
                                / canvas.effectiveViewState(1).pixelsPerImagePixel;
    QVERIFY(std::abs(resumedRatio - independentRatio) < 0.000001);
    const QVariantMap navigation = canvas.navigationState(0);
    QVERIFY(navigation.value(QStringLiteral("visible")).toBool());
    QVERIFY(navigation.value(QStringLiteral("width")).toInt() <= 96);
    QVERIFY(navigation.value(QStringLiteral("height")).toInt() <= 71);
}

} // namespace ispview

QTEST_MAIN(ispview::QmlWorkspaceControllerTests)
#include "qml_workspace_controller_tests.moc"
