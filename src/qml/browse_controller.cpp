#include "qml/browse_controller.h"

#include "core/raw_plane_access.h"
#include "io/directory_scanner.h"
#include "io/drop_copy_operation.h"
#include "io/image_loader.h"
#include "io/raw_preset_store.h"
#include "io/single_file_rename.h"
#include "io/supported_image_formats.h"
#include "platform/platform_services.h"
#include "browser/file_clipboard.h"
#include "browser/thumbnail_model.h"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QLocale>
#include <QPointer>
#include <QSettings>
#include <QSet>
#include <QStandardPaths>
#include <QThreadPool>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace ispview {
BrowseController::BrowseController(std::shared_ptr<const IImageDecoder> decoder,
                                   const QString& initialDirectory, QObject* parent)
    : QObject(parent), loader_(new ImageLoader(std::move(decoder), this)),
      fileSystemModel_(new QFileSystemModel(this)) {
    initialize(initialDirectory, false);
}

BrowseController::BrowseController(ImageLoader* sharedLoader,
                                   QFileSystemModel* sharedFileSystemModel,
                                   const QString& initialDirectory, bool startEmpty,
                                   QObject* parent)
    : QObject(parent), loader_(sharedLoader), fileSystemModel_(sharedFileSystemModel) {
    Q_ASSERT(loader_);
    Q_ASSERT(fileSystemModel_);
    initialize(initialDirectory, startEmpty);
}

void BrowseController::initialize(const QString& initialDirectory, bool startEmpty) {
    scanner_ = new DirectoryScanner(this);
    thumbnailModel_ = new ThumbnailModel(loader_, this);
    filterModel_ = new ThumbnailFilterProxyModel(this);
    directoryWatcher_ = new QFileSystemWatcher(this);
    refreshTimer_ = new QTimer(this);
    refreshDeadlineTimer_ = new QTimer(this);
    recentCandidateTimer_ = new QTimer(this);
    filterModel_->setSourceModel(thumbnailModel_);
    // The system navigation pane represents folders only, matching Explorer and Finder.
    // Image filtering belongs to ThumbnailModel; putting files into the navigation model both
    // diverges from native sidebars and needlessly expands large directory trees.
    fileSystemModel_->setFilter(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Drives);
#ifdef Q_OS_WIN
    // Invalid root index exposes all native drive roots on Windows.
    fileSystemModel_->setRootPath(QString{});
#else
    // On macOS/Linux, hide the synthetic "/" row and expose its native children directly.
    fileSystemModel_->setRootPath(QDir::rootPath());
#endif

    QSettings settings;
    settings.remove(QStringLiteral("browser/favoriteFolders"));
    constexpr int recentPolicyVersion = 2;
    if (settings.value(QStringLiteral("browser/recentPolicyVersion"), 0).toInt() !=
        recentPolicyVersion) {
        settings.remove(QStringLiteral("browser/recentFolders"));
        settings.setValue(QStringLiteral("browser/recentPolicyVersion"), recentPolicyVersion);
    }
    recentFolders_ = settings.value(QStringLiteral("browser/recentFolders")).toStringList();
    // Do not probe every recent path during construction. A disconnected network share or
    // sleeping removable drive can block QFileInfo long enough to make the app look hung before
    // its first frame. Invalid entries are handled normally if the user chooses one.
    recentFolders_.removeIf([](const QString& path) { return path.trimmed().isEmpty(); });
    gridCellWidth_ = std::clamp(settings.value(QStringLiteral("browser/qmlGridCellWidth"), 196).toInt(),
                                168, 260);
    settings.remove(QStringLiteral("browser/qmlInspectorVisible"));
    const int savedSortMode = settings.value(QStringLiteral("browser/sortMode"), 0).toInt();
    if (savedSortMode >= static_cast<int>(BrowserSortMode::Name) &&
        savedSortMode <= static_cast<int>(BrowserSortMode::Type)) {
        filterModel_->setSortMode(static_cast<BrowserSortMode>(savedSortMode));
    }

    connect(scanner_, &DirectoryScanner::scanBatchReady, this,
            [this](const QString& directory, const QVector<ImageFileRecord>& files,
                   quint64 generation) {
                if (generation != scanGeneration_ || directory != currentDirectory_ ||
                    !incrementalScan_) {
                    return;
                }
                thumbnailModel_->appendFiles(files);
                setStatusText(QStringLiteral("Scanning %1… %2 items")
                                  .arg(QDir::toNativeSeparators(currentDirectory_))
                                  .arg(thumbnailModel_->rowCount()));
            });
    connect(scanner_, &DirectoryScanner::scanFinished, this,
            [this](const QString& directory, const QVector<ImageFileRecord>& files,
                   quint64 generation) {
                if (generation != scanGeneration_ || directory != currentDirectory_) {
                    return;
                }
                if (!incrementalScan_) {
                    thumbnailModel_->updateFiles(files);
                }
                QStringList existing;
                for (const QString& path : std::as_const(selectedPaths_)) {
                    if (QFileInfo::exists(path)) {
                        existing.append(path);
                    }
                }
                updateSelection(existing);
                setStatusText(QStringLiteral("%1 items · %2 selected")
                                  .arg(files.size())
                                  .arg(selectedPaths_.size()));

                if (!pendingActivationPath_.isEmpty()) {
                    const QString pending = pendingActivationPath_;
                    pendingActivationPath_.clear();
                    if (QFileInfo(pending).absolutePath() == currentDirectory_) {
                        updateSelection({pending});
                        openSelected();
                    }
                }

                const bool directlyContainsImages =
                    std::any_of(files.cbegin(), files.cend(),
                                [](const ImageFileRecord& file) { return !file.isDirectory; });
                if (directory == recentCandidateDirectory_ && directlyContainsImages) {
                    if (!recentCandidateTimer_->isActive()) {
                        recentCandidateTimer_->start();
                    }
                } else if (directory == recentCandidateDirectory_) {
                    recentCandidateTimer_->stop();
                    recentCandidateDirectory_.clear();
                }
            });
    refreshTimer_->setSingleShot(true);
    refreshTimer_->setInterval(250);
    refreshDeadlineTimer_->setSingleShot(true);
    refreshDeadlineTimer_->setInterval(1'000);
    connect(directoryWatcher_, &QFileSystemWatcher::directoryChanged, this, [this] {
        refreshTimer_->start();
        if (!refreshDeadlineTimer_->isActive()) refreshDeadlineTimer_->start();
    });
    connect(refreshTimer_, &QTimer::timeout, this, [this] {
        refreshDeadlineTimer_->stop();
        rescanCurrentDirectory();
    });
    connect(refreshDeadlineTimer_, &QTimer::timeout, this, [this] {
        refreshTimer_->stop();
        rescanCurrentDirectory();
    });
    recentCandidateTimer_->setSingleShot(true);
    recentCandidateTimer_->setInterval(10'000);
    connect(recentCandidateTimer_, &QTimer::timeout, this, [this] {
        if (recentCandidateDirectory_.isEmpty() ||
            recentCandidateDirectory_ != currentDirectory_) {
            return;
        }
        const auto& files = thumbnailModel_->files();
        const bool directlyContainsImages =
            std::any_of(files.cbegin(), files.cend(),
                        [](const ImageFileRecord& file) { return !file.isDirectory; });
        if (!directlyContainsImages) {
            return;
        }
        recentFolders_.removeAll(currentDirectory_);
        recentFolders_.prepend(currentDirectory_);
        while (recentFolders_.size() > 8) {
            recentFolders_.removeLast();
        }
        QSettings().setValue(QStringLiteral("browser/recentFolders"), recentFolders_);
        emit recentFoldersChanged();
    });
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this,
            &BrowseController::clipboardStateChanged);
    connect(loader_, &ImageLoader::rawParametersChanged, this, [this](const QString& path) {
        if (galleryPath_ != path) return;
        galleryPath_.clear();
        galleryFrame_.reset();
        setGalleryPath(path);
    });

    if (startEmpty) {
        statusText_ = QStringLiteral("Choose a folder for this file manager");
        return;
    }

    QString startupDirectory = initialDirectory;
    if (startupDirectory.isEmpty()) {
        startupDirectory =
            settings.value(QStringLiteral("browser/lastDirectory"), QDir::homePath()).toString();
        // A removable drive or project folder may have disappeared since the previous run.
        // Fall back quietly to the home directory instead of opening to an error state.
        if (!QFileInfo(startupDirectory).isDir()) {
            startupDirectory = QDir::homePath();
        }
    }
    openDirectoryInternal(startupDirectory, true);
}

BrowseController::~BrowseController() = default;

QAbstractItemModel* BrowseController::thumbnails() const { return filterModel_; }

QAbstractItemModel* BrowseController::folderTree() { return fileSystemModel_; }

QModelIndex BrowseController::folderRootIndex() const {
#ifdef Q_OS_WIN
    return {};
#else
    return fileSystemModel_->index(QDir::rootPath());
#endif
}

QModelIndex BrowseController::currentFolderTreeIndex() const {
    return currentDirectory_.isEmpty() ? QModelIndex{} : fileSystemModel_->index(currentDirectory_);
}

void BrowseController::loadFolderTreeChildren(const QString& path) {
    if (path.trimmed().isEmpty()) return;
    // QFileSystemModel does not begin asynchronously populating an uncached branch merely
    // because TreeView marks its row expanded. setRootPath() starts the gatherer for this path;
    // per Qt's contract it does not change the model structure or the root shown by the view.
    const QModelIndex directoryIndex = fileSystemModel_->setRootPath(QDir::cleanPath(path));
    if (!directoryIndex.isValid()) return;
    if (fileSystemModel_->canFetchMore(directoryIndex)) {
        fileSystemModel_->fetchMore(directoryIndex);
    }
}

QString BrowseController::currentFolderName() const {
    const QString name = QFileInfo(currentDirectory_).fileName();
    return name.isEmpty() ? QDir::toNativeSeparators(currentDirectory_) : name;
}

QList<QUrl> BrowseController::selectedFileUrls() const {
    QList<QUrl> urls;
    urls.reserve(selectedPaths_.size());
    for (const QString& path : selectedPaths_) {
        urls.append(QUrl::fromLocalFile(path));
    }
    return urls;
}

QString BrowseController::selectedUriList() const {
    QStringList encoded;
    encoded.reserve(selectedPaths_.size());
    for (const QString& path : selectedPaths_) {
        encoded.append(QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded));
    }
    return encoded.join(QStringLiteral("\r\n"));
}

bool BrowseController::canGoForward() const {
    return navigationHistoryIndex_ >= 0 &&
           navigationHistoryIndex_ + 1 < navigationHistory_.size();
}

bool BrowseController::canGoUp() const {
    QDir directory(currentDirectory_);
    return !currentDirectory_.isEmpty() && directory.cdUp();
}

bool BrowseController::canPaste() const { return FileClipboard::hasFiles(); }

int BrowseController::sortMode() const { return static_cast<int>(filterModel_->sortMode()); }

bool BrowseController::canCompare() const {
    const qsizetype count = selectedImagePaths().size();
    return count >= 2 && count <= 4;
}

bool BrowseController::canEditRaw() const {
    if (selectedPaths_.size() != 1) {
        return false;
    }
    const QString suffix = QFileInfo(selectedPaths_.first()).suffix().toLower();
    return suffix == QStringLiteral("raw") || suffix == QStringLiteral("yuv");
}

void BrowseController::openDirectory(const QString& path) { openDirectoryInternal(path, true); }

void BrowseController::openDirectoryUrl(const QUrl& url) {
    if (url.isLocalFile()) openDirectoryInternal(url.toLocalFile(), true);
}

QVariantList BrowseController::nativeSidebarPlaces() const {
    QVariantList places;
    QSet<QString> seenPaths;
    const auto appendPlace = [&places, &seenPaths](const QString& label, const QString& path,
                                                   const QString& kind) {
        if (path.isEmpty()) return;
        const QString cleanPath = QDir::cleanPath(path);
#ifdef Q_OS_WIN
        const QString key = cleanPath.toLower();
#else
        const QString key = cleanPath;
#endif
        if (seenPaths.contains(key)) return;
        seenPaths.insert(key);
        QVariantMap place{{QStringLiteral("label"), label},
                          {QStringLiteral("path"), cleanPath},
                          {QStringLiteral("kind"), kind}};
#ifdef Q_OS_WIN
        place.insert(QStringLiteral("icon"),
                     QStringLiteral("image://system-folder/%1")
                         .arg(QString::fromLatin1(QUrl::toPercentEncoding(cleanPath))));
#endif
        places.append(std::move(place));
    };

#ifdef Q_OS_MACOS
    const QString homeLabel = QFileInfo(QDir::homePath()).fileName();
    appendPlace(homeLabel.isEmpty() ? QStringLiteral("Home") : homeLabel, QDir::homePath(),
                QStringLiteral("home"));
#else
    appendPlace(QStringLiteral("Home"), QDir::homePath(), QStringLiteral("home"));
#endif
    appendPlace(QStringLiteral("Desktop"),
                QStandardPaths::writableLocation(QStandardPaths::DesktopLocation),
                QStringLiteral("desktop"));
    appendPlace(QStringLiteral("Documents"),
                QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
                QStringLiteral("documents"));
    appendPlace(QStringLiteral("Downloads"),
                QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
                QStringLiteral("downloads"));
    appendPlace(QStringLiteral("Pictures"),
                QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
                QStringLiteral("pictures"));
    return places;
}

void BrowseController::restoreInitialDirectoryAsync(const QString& initialDirectory) {
    QString startupDirectory = initialDirectory;
    if (startupDirectory.isEmpty()) {
        startupDirectory =
            QSettings().value(QStringLiteral("browser/lastDirectory"), QDir::homePath()).toString();
    }
    const quint64 requestGeneration = ++directoryRequestGeneration_;
    setStatusText(QStringLiteral("Restoring the previous folder…"));
    const QPointer<BrowseController> self(this);
    QThreadPool::globalInstance()->start(
        [self, startupDirectory, requestGeneration] {
            QFileInfo startupInfo(startupDirectory);
            QString resolvedDirectory;
            if (startupInfo.isDir() && DirectoryScanner::isBrowsableEntry(startupInfo)) {
                resolvedDirectory = startupInfo.absoluteFilePath();
            } else {
                resolvedDirectory = QDir::homePath();
            }
            if (!self) return;
            QMetaObject::invokeMethod(
                self,
                [self, resolvedDirectory, requestGeneration] {
                    if (!self || self->directoryRequestGeneration_ != requestGeneration ||
                        !self->currentDirectory_.isEmpty()) {
                        return;
                    }
                    self->openDirectoryInternal(resolvedDirectory, true, true);
                },
                Qt::QueuedConnection);
        },
        100);
}

void BrowseController::chooseDirectory() {
    const QString start = currentDirectory_.isEmpty() ? QDir::homePath() : currentDirectory_;
    emit directorySelectionRequested(QUrl::fromLocalFile(start));
}

void BrowseController::navigateBack() {
    if (!canGoBack()) {
        return;
    }
    --navigationHistoryIndex_;
    openDirectoryInternal(navigationHistory_.at(navigationHistoryIndex_), false);
}

void BrowseController::navigateForward() {
    if (!canGoForward()) {
        return;
    }
    ++navigationHistoryIndex_;
    openDirectoryInternal(navigationHistory_.at(navigationHistoryIndex_), false);
}

void BrowseController::navigateUp() {
    QDir directory(currentDirectory_);
    if (directory.cdUp()) {
        openDirectoryInternal(directory.absolutePath(), true);
    }
}

void BrowseController::activatePath(const QString& path) {
    if (QFileInfo(path).isDir()) {
        openDirectoryInternal(path, true);
        return;
    }
    updateSelection({path});
    openSelected();
}

void BrowseController::activateTreeItem(const QString& path) {
    if (path.isEmpty()) return;
    if (QFileInfo(path).isDir()) {
        openDirectoryInternal(path, true);
        return;
    }
    // If the file's parent directory differs from the current directory,
    // navigate there first, then the scan-finished handler will activate the file.
    const QString parentDir = QFileInfo(path).absolutePath();
    if (parentDir != currentDirectory_) {
        pendingActivationPath_ = path;
        openDirectoryInternal(parentDir, true);
        return;
    }
    updateSelection({path});
    openSelected();
}

void BrowseController::selectPath(const QString& path, bool extend, bool toggle) {
    if (path.isEmpty()) {
        return;
    }
    QStringList selection = selectedPaths_;
    if (extend && !selectionAnchorPath_.isEmpty()) {
        int anchorRow = -1;
        int targetRow = -1;
        for (int row = 0; row < filterModel_->rowCount(); ++row) {
            const QString candidate =
                filterModel_->index(row, 0).data(ThumbnailModel::PathRole).toString();
            if (candidate == selectionAnchorPath_) {
                anchorRow = row;
            }
            if (candidate == path) {
                targetRow = row;
            }
        }
        if (anchorRow >= 0 && targetRow >= 0) {
            selection.clear();
            const int first = std::min(anchorRow, targetRow);
            const int last = std::max(anchorRow, targetRow);
            for (int row = first; row <= last; ++row) {
                selection.append(
                    filterModel_->index(row, 0).data(ThumbnailModel::PathRole).toString());
            }
        } else {
            selection = {path};
            selectionAnchorPath_ = path;
        }
    } else if (toggle) {
        if (selection.contains(path)) {
            selection.removeAll(path);
        } else {
            selection.append(path);
        }
        selectionAnchorPath_ = path;
    } else if (extend) {
        if (!selection.contains(path)) {
            selection.append(path);
        }
        selectionAnchorPath_ = path;
    } else {
        selection = {path};
        selectionAnchorPath_ = path;
    }
    updateSelection(selection);
}

void BrowseController::clearSelection() {
    selectionAnchorPath_.clear();
    updateSelection({});
}

void BrowseController::setFilterText(const QString& text) {
    const QString normalized = text.trimmed();
    if (filterText_ == normalized) {
        return;
    }
    filterText_ = normalized;
    filterModel_->setFilterFixedString(filterText_);
    emit filterTextChanged();
    setStatusText(QStringLiteral("%1 visible · %2 selected")
                      .arg(filterModel_->rowCount())
                      .arg(selectedPaths_.size()));
}

void BrowseController::setDisplayMode(int mode) {
    const int bounded = std::clamp(mode, 0, 2);
    if (displayMode_ == bounded) {
        return;
    }
    displayMode_ = bounded;
    emit displayModeChanged();
}

void BrowseController::setSortMode(int mode) {
    if (mode < static_cast<int>(BrowserSortMode::Name) ||
        mode > static_cast<int>(BrowserSortMode::Type)) {
        return;
    }
    if (mode == sortMode()) {
        return;
    }
    filterModel_->setSortMode(static_cast<BrowserSortMode>(mode));
    QSettings().setValue(QStringLiteral("browser/sortMode"), mode);
    emit sortModeChanged();
}

QString BrowseController::createFolder(const QString& requestedName) {
    const QString name = requestedName.trimmed();
    if (name.isEmpty()) {
        return QStringLiteral("Enter a folder name.");
    }
    if (name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\')) ||
        name == QStringLiteral(".") || name == QStringLiteral("..")) {
        return QStringLiteral("The folder name contains unsupported characters.");
    }
    const QString path = QDir(currentDirectory_).filePath(name);
    if (QFileInfo::exists(path)) {
        return QStringLiteral("An item named “%1” already exists.").arg(name);
    }
    if (!QDir().mkdir(path)) {
        return QStringLiteral("The folder could not be created here.");
    }
    rescanCurrentDirectory();
    updateSelection({path});
    return {};
}

void BrowseController::refresh() {
    if (currentDirectory_.isEmpty()) {
        return;
    }
    setStatusText(QStringLiteral("Refreshing %1…")
                      .arg(QDir::toNativeSeparators(currentDirectory_)));
    rescanCurrentDirectory();
}

void BrowseController::selectAll() {
    QStringList paths;
    paths.reserve(filterModel_->rowCount());
    for (int row = 0; row < filterModel_->rowCount(); ++row) {
        paths.append(filterModel_->index(row, 0).data(ThumbnailModel::PathRole).toString());
    }
    updateSelection(paths);
}

void BrowseController::openCurrentDirectoryInFileManager() {
    if (currentDirectory_.isEmpty()) {
        return;
    }
    if (!PlatformServices::openDirectoryInFileManager(currentDirectory_)) {
        setStatusText(QStringLiteral("Could not open the system file manager"));
    }
}

void BrowseController::copySelected(bool cut) {
    if (selectedPaths_.isEmpty()) {
        return;
    }
    FileClipboard::setPaths(selectedPaths_, cut);
    setStatusText(QStringLiteral("%1 %2 item(s) to the clipboard")
                      .arg(cut ? QStringLiteral("Cut") : QStringLiteral("Copied"))
                      .arg(selectedPaths_.size()));
}

void BrowseController::pasteItems() {
    const FileClipboardContents clipboard = FileClipboard::contents();
    if (clipboard.paths.isEmpty()) {
        setStatusText(QStringLiteral("The clipboard does not contain files"));
        return;
    }
    transferPaths(clipboard.paths, clipboard.cut);
}

void BrowseController::pasteItemsInto(const QString& directory) {
    const FileClipboardContents clipboard = FileClipboard::contents();
    if (clipboard.paths.isEmpty()) {
        setStatusText(QStringLiteral("The clipboard does not contain files"));
        return;
    }
    transferPaths(clipboard.paths, clipboard.cut, directory);
}

void BrowseController::copyDroppedUrls(const QList<QUrl>& urls) {
    copyDroppedUrlsInto(urls, currentDirectory_);
}

void BrowseController::copyDroppedUrlsInto(const QList<QUrl>& urls, const QString& directory) {
    QStringList paths;
    for (const QUrl& url : urls) {
        if (url.isLocalFile()) {
            paths.append(url.toLocalFile());
        }
    }
    transferPaths(paths, false, directory);
}

void BrowseController::openDroppedUrls(const QList<QUrl>& urls) {
    QStringList localPaths;
    for (const QUrl& url : urls) {
        if (url.isLocalFile()) localPaths.append(QFileInfo(url.toLocalFile()).absoluteFilePath());
    }
    for (const QString& path : std::as_const(localPaths)) {
        if (QFileInfo(path).isDir()) {
            openDirectoryInternal(path, true);
            return;
        }
    }

    QString targetDirectory;
    QStringList images;
    for (const QString& path : std::as_const(localPaths)) {
        const QFileInfo info(path);
        if (!info.isFile() || !DirectoryScanner::isSupportedImageFile(path)) continue;
        if (targetDirectory.isEmpty()) targetDirectory = info.absolutePath();
        if (info.absolutePath() == targetDirectory) images.append(info.absoluteFilePath());
    }
    if (targetDirectory.isEmpty()) return;
    openDirectoryInternal(targetDirectory, true);
    updateSelection(images);
}

void BrowseController::renameSelected() {
    if (selectedPaths_.size() != 1) return;
    emit renameRequested(QFileInfo(selectedPaths_.first()).fileName());
}

QString BrowseController::renameSelectedTo(const QString& requestedName) {
    if (selectedPaths_.size() != 1) return QStringLiteral("Select one item to rename.");
    const QFileInfo source(selectedPaths_.first());
    const QString newName = requestedName.trimmed();
    if (newName.isEmpty()) return QStringLiteral("Enter a name.");
    if (newName.contains(QLatin1Char('/')) || newName.contains(QLatin1Char('\\')) ||
        newName == QStringLiteral(".") || newName == QStringLiteral("..")) {
        return QStringLiteral("The name contains unsupported characters.");
    }
    if (newName == source.fileName()) return {};
    const QString destination = source.dir().filePath(newName);
    QString error;
    if (!SingleFileRename::execute(source.absoluteFilePath(), destination, &error)) return error;
    updateSelection({destination});
    rescanCurrentDirectory();
    setStatusText(QStringLiteral("Renamed to %1").arg(newName));
    return {};
}

void BrowseController::moveSelectedToTrash() {
    if (!selectedPaths_.isEmpty())
        emit trashConfirmationRequested(static_cast<int>(selectedPaths_.size()));
}

QString BrowseController::moveSelectedToTrashConfirmed() {
    if (selectedPaths_.isEmpty()) return {};
    const int requestedCount = static_cast<int>(selectedPaths_.size());
    QStringList failures;
    for (const QString& path : std::as_const(selectedPaths_)) {
        if (!QFile::moveToTrash(path)) {
            failures.append(QFileInfo(path).fileName());
        }
    }
    clearSelection();
    rescanCurrentDirectory();
    if (!failures.isEmpty()) {
        const QString message = QStringLiteral("Could not move to Trash:\n%1")
                                    .arg(failures.join(QLatin1Char('\n')));
        setStatusText(QStringLiteral("%1 of %2 item(s) could not be moved to Trash")
                          .arg(failures.size())
                          .arg(requestedCount));
        return message;
    }
    setStatusText(QStringLiteral("Moved %1 item(s) to Trash").arg(requestedCount));
    return {};
}

void BrowseController::revealSelected() {
    if (!selectedPaths_.isEmpty() &&
        !PlatformServices::revealInFileManager(selectedPaths_.first())) {
        setStatusText(QStringLiteral("Could not open the system file manager"));
    }
}

void BrowseController::showSelectedProperties() {
    if (selectedPaths_.size() == 1) emit propertiesRequested(selectedPaths_.first());
}

void BrowseController::openSelected() {
    const QStringList selectedImages = selectedImagePaths();
    if (selectedImages.isEmpty()) {
        return;
    }
    const QStringList paths = allImagePaths();
    const int index = std::max(0, static_cast<int>(paths.indexOf(selectedImages.first())));
    emit fullScreenRequested(paths, index);
}

void BrowseController::compareSelected() {
    const QStringList paths = selectedImagePaths();
    if (paths.size() < 2 || paths.size() > 4) {
        setStatusText(QStringLiteral("Select 2–4 images to compare"));
        return;
    }
    emit compareRequested(paths);
}

void BrowseController::editSelectedRawParameters() {
    if (canEditRaw()) emit rawParametersRequested(selectedPaths_.first());
}

void BrowseController::setGalleryPath(const QString& path) {
    const QString normalized = QFileInfo(path).absoluteFilePath();
    if (galleryPath_ == normalized && (galleryFrame_ || normalized.isEmpty())) {
        return;
    }
    galleryPath_ = normalized;
    galleryLoadHandle_.cancel();
    galleryFrame_.reset();
    galleryImageSize_ = {};
    galleryInfoText_.clear();
    emit galleryImageChanged();

    if (normalized.isEmpty() || QFileInfo(normalized).isDir()) {
        return;
    }
    const quint64 requestId = ++galleryRequestId_;
    const QPointer<BrowseController> self(this);
    galleryLoadHandle_ = loader_->request(
        requestId, {normalized, DecodePurpose::Full, {}},
        [self, normalized](quint64 completedId, const DecodeResult& result) {
            if (!self || completedId != self->galleryRequestId_ ||
                normalized != self->galleryPath_) {
                return;
            }
            self->galleryFrame_ = result.frame;
            if (result.frame) {
                const RawPlaneAccessor raw(*result.frame);
                if (raw.isValid()) {
                    self->galleryImageSize_ = raw.displaySize();
                } else if (const QImage* image = result.frame->qImage()) {
                    // Full decoded pixels already include EXIF orientation. Their logical size
                    // must drive both 1:1 display and pixel coordinates; metadata.sourceSize is
                    // the pre-orientation sensor/header size for rotated JPEGs.
                    self->galleryImageSize_ = image->size();
                } else if (result.frame->metadata.sourceSize.isValid()) {
                    self->galleryImageSize_ = result.frame->metadata.sourceSize;
                } else {
                    self->galleryImageSize_ = result.frame->descriptor.size;
                }
                const int bits = std::max(1, result.frame->descriptor.validBits);
                self->galleryInfoText_ =
                    QStringLiteral("%1 × %2  %3-bit  %4  %5")
                        .arg(result.frame->descriptor.size.width())
                        .arg(result.frame->descriptor.size.height())
                        .arg(bits)
                        .arg(result.frame->metadata.format.toUpper())
                        .arg(QLocale().formattedDataSize(result.frame->metadata.fileSize));
            }
            emit self->galleryImageChanged();
        }, RequestOptions{LoadCategory::Interactive, 0, QStringLiteral("gallery")});
}

QString BrowseController::probeGalleryPixel(int x, int y) const {
    if (!galleryFrame_ || !galleryImageSize_.isValid() || x < 0 || y < 0 ||
        x >= galleryImageSize_.width() || y >= galleryImageSize_.height()) {
        return {};
    }

    QString value;
    if (const QImage* image = galleryFrame_->qImage(); image && !image->isNull()) {
        const int sampleX = std::clamp(
            static_cast<int>((static_cast<double>(x) + 0.5) * image->width() /
                             galleryImageSize_.width()),
            0, image->width() - 1);
        const int sampleY = std::clamp(
            static_cast<int>((static_cast<double>(y) + 0.5) * image->height() /
                             galleryImageSize_.height()),
            0, image->height() - 1);
        const QColor color = image->pixelColor(sampleX, sampleY);
        value = QStringLiteral("RGBA(%1, %2, %3, %4)")
                    .arg(color.red())
                    .arg(color.green())
                    .arg(color.blue())
                    .arg(color.alpha());
    }
    const RawPlaneAccessor raw(*galleryFrame_);
    if (raw.isValid()) {
        const QString engineering = raw.pixelDescriptionAtDisplayPixel({x, y});
        if (!engineering.isEmpty()) {
            value += value.isEmpty() ? engineering : QStringLiteral(" · ") + engineering;
        }
    }
    return QStringLiteral("x %1 · y %2 · %3").arg(x).arg(y).arg(value);
}

void BrowseController::setGridCellWidth(int width) {
    width = std::clamp(width, 168, 260);
    if (gridCellWidth_ == width) {
        return;
    }
    gridCellWidth_ = width;
    QSettings().setValue(QStringLiteral("browser/qmlGridCellWidth"), gridCellWidth_);
    emit gridCellWidthChanged();
}

void BrowseController::openDirectoryInternal(const QString& path, bool addToHistory,
                                             bool pathAlreadyValidated) {
    ++directoryRequestGeneration_;
    const QFileInfo info(path);
    if (!pathAlreadyValidated &&
        (!info.exists() || !info.isDir() || !DirectoryScanner::isBrowsableEntry(info))) {
        setStatusText(QStringLiteral("Folder is hidden, unreadable, or unavailable: %1").arg(path));
        return;
    }
    currentDirectory_ = info.absoluteFilePath();
    // Persist at navigation time so an ordinary force-quit or crash still restores the last
    // meaningful workspace on the next start.
    QSettings().setValue(QStringLiteral("browser/lastDirectory"), currentDirectory_);
    recentCandidateTimer_->stop();
    recentCandidateDirectory_ = currentDirectory_;
    if (addToHistory &&
        (navigationHistoryIndex_ < 0 ||
         navigationHistory_.value(navigationHistoryIndex_) != currentDirectory_)) {
        while (navigationHistory_.size() > navigationHistoryIndex_ + 1) {
            navigationHistory_.removeLast();
        }
        navigationHistory_.append(currentDirectory_);
        navigationHistoryIndex_ = static_cast<int>(navigationHistory_.size()) - 1;
    }
    emit currentDirectoryChanged();
    emit navigationStateChanged();
    clearSelection();
    if (!directoryWatcher_->directories().isEmpty()) {
        directoryWatcher_->removePaths(directoryWatcher_->directories());
    }
    directoryWatcher_->addPath(currentDirectory_);
    thumbnailModel_->setFiles({});
    incrementalScan_ = true;
    setStatusText(QStringLiteral("Scanning %1…").arg(QDir::toNativeSeparators(currentDirectory_)));
    scanGeneration_ = scanner_->scanAsync(currentDirectory_);
}

void BrowseController::rescanCurrentDirectory() {
    if (!currentDirectory_.isEmpty()) {
        incrementalScan_ = false;
        scanGeneration_ = scanner_->scanAsync(currentDirectory_);
    }
}

void BrowseController::setStatusText(const QString& text) {
    if (statusText_ == text) {
        return;
    }
    statusText_ = text;
    emit statusTextChanged();
}

void BrowseController::updateSelection(const QStringList& paths) {
    QStringList normalized;
    normalized.reserve(paths.size());
    QSet<QString> seen;
    seen.reserve(paths.size());
    for (const QString& path : paths) {
        if (!path.isEmpty() && !seen.contains(path)) {
            seen.insert(path);
            normalized.append(path);
        }
    }
    if (selectedPaths_ == normalized) {
        return;
    }
    selectedPaths_ = normalized;
    thumbnailModel_->setSelectedPaths(selectedPaths_);
    emit selectionChanged();
    setStatusText(QStringLiteral("%1 visible · %2 selected")
                      .arg(filterModel_->rowCount())
                      .arg(selectedPaths_.size()));
}

void BrowseController::setWorkspaceSelectionOrder(const QStringList& paths) {
    thumbnailModel_->setSelectedPaths(paths);
}

void BrowseController::setSharedRecentFolders(const QStringList& paths) {
    if (recentFolders_ == paths) return;
    recentFolders_ = paths;
    emit recentFoldersChanged();
}

void BrowseController::transferPaths(const QStringList& paths, bool move,
                                     const QString& targetDirectory) {
    const QString target = targetDirectory.isEmpty() ? currentDirectory_
                                                      : QFileInfo(targetDirectory).absoluteFilePath();
    if (paths.isEmpty() || target.isEmpty() || !QFileInfo(target).isDir()) {
        return;
    }
    setStatusText(QStringLiteral("%1 %2 item(s)…")
                      .arg(move ? QStringLiteral("Moving") : QStringLiteral("Copying"))
                      .arg(paths.size()));
    const QPointer<BrowseController> self(this);
    QThreadPool::globalInstance()->start([self, paths, target, move] {
        FileTransferResult result = FileTransferOperation::execute(
            paths, target, move ? FileTransferMode::Move : FileTransferMode::Copy);
        if (!self) {
            return;
        }
        QMetaObject::invokeMethod(
            self,
            [self, paths, target, move, result = std::move(result)] {
                if (!self) {
                    return;
                }
                if (move && result.errors.isEmpty()) {
                    FileClipboard::clear();
                }
                const bool destinationIsCurrent = self->currentDirectory_ == target;
                const bool sourceWasCurrent =
                    std::any_of(paths.cbegin(), paths.cend(), [self](const QString& path) {
                        return QFileInfo(path).absolutePath() == self->currentDirectory_;
                    });
                if (destinationIsCurrent) {
                    self->updateSelection(result.destinationPaths);
                }
                if (destinationIsCurrent || sourceWasCurrent) {
                    self->rescanCurrentDirectory();
                }
                self->setStatusText(
                    result.errors.isEmpty()
                        ? QStringLiteral("%1 %2 item(s)")
                              .arg(move ? QStringLiteral("Moved") : QStringLiteral("Copied"))
                              .arg(result.destinationPaths.size())
                        : QStringLiteral("%1 error(s): %2")
                              .arg(result.errors.size())
                              .arg(result.errors.first()));
            },
            Qt::QueuedConnection);
    });
}

QStringList BrowseController::selectedImagePaths() const {
    QStringList result;
    for (const QString& path : selectedPaths_) {
        if (DirectoryScanner::isSupportedImageFile(path)) {
            result.append(path);
        }
    }
    return result;
}

QStringList BrowseController::allImagePaths() const {
    QStringList result;
    for (int row = 0; row < filterModel_->rowCount(); ++row) {
        const QModelIndex index = filterModel_->index(row, 0);
        if (!index.data(ThumbnailModel::DirectoryRole).toBool()) {
            result.append(index.data(ThumbnailModel::PathRole).toString());
        }
    }
    return result;
}

} // namespace ispview
