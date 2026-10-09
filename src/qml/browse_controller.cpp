#include "diagnostics/diagnostics.h"
#include "qml/browse_controller.h"
#include "qml/folder_tree_branch_model.h"

#include "core/comparison_pixel_probe.h"
#include "core/raw_plane_access.h"
#include "io/directory_scanner.h"
#include "io/drop_copy_operation.h"
#include "io/image_loader.h"
#include "io/image_transformer.h"
#include "io/raw_preset_store.h"
#include "io/single_file_rename.h"
#include "io/supported_image_formats.h"
#include "platform/platform_services.h"
#include "browser/file_clipboard.h"
#include "browser/thumbnail_model.h"

#include <QClipboard>
#include <QElapsedTimer>
#include "core/performance_trace.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QImageReader>
#include <QLocale>
#include <QPointer>
#include <QSettings>
#include <QSet>
#include <QStandardPaths>
#include <QThreadPool>
#include <QTimer>

#include <algorithm>
#include <thread>
#include <utility>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace mvpview {
namespace {
#ifdef Q_OS_WIN
QString windowsDriveRoot(const QString& path) {
    const QString native = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    if (native.size() >= 2 && native.at(1) == QLatin1Char(':'))
        return native.left(2) + QStringLiteral("\\");
    if (native.startsWith(QStringLiteral("\\\\"))) {
        const QStringList parts = native.mid(2).split(QLatin1Char('\\'), Qt::SkipEmptyParts);
        if (parts.size() >= 2)
            return QStringLiteral("\\\\%1\\%2\\").arg(parts.at(0), parts.at(1));
    }
    return {};
}

bool isWindowsRemotePath(const QString& path) {
    const QString root = windowsDriveRoot(path);
    if (root.startsWith(QStringLiteral("\\\\"))) return true;
    return !root.isEmpty()
        && GetDriveTypeW(reinterpret_cast<LPCWSTR>(root.utf16())) == DRIVE_REMOTE;
}

QString windowsSystemDriveRoot() {
    wchar_t windowsDirectory[MAX_PATH]{};
    const UINT length = GetWindowsDirectoryW(windowsDirectory, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        const QString root = windowsDriveRoot(QString::fromWCharArray(windowsDirectory, length));
        if (!root.isEmpty()) return root;
    }
    return QStringLiteral("C:\\");
}
#endif

QString startupFallbackDirectory() {
#ifdef Q_OS_WIN
    return windowsSystemDriveRoot();
#else
    return QDir::homePath();
#endif
}
}

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
    const performance::Scope trace(QStringLiteral("browse.initialize"));
    scanner_ = new DirectoryScanner(this);
    scanner_->setBatchBackpressure(true);
    thumbnailModel_ = new ThumbnailModel(loader_, this);
    filterModel_ = new ThumbnailFilterProxyModel(this);
    directoryWatcher_ = new QFileSystemWatcher(this);
    galleryUpgradeTimer_ = new QTimer(this);
    galleryUpgradeTimer_->setSingleShot(true);
    galleryUpgradeTimer_->setInterval(250);
    connect(galleryUpgradeTimer_, &QTimer::timeout, this, [this] {
        if (!galleryFrame_ || galleryFullRequested_ || galleryFullResolution_) return;
        if (loader_->fastScrolling() || loader_->hasInteractiveWork()) galleryUpgradeTimer_->start();
        else if (loader_->canAutomaticallyLoadFull({galleryFrame_})) requestGalleryFull();
    });
    scanBatchTimer_ = new QTimer(this);
    scanBatchTimer_->setSingleShot(true);
    connect(scanBatchTimer_, &QTimer::timeout, this, [this] {
        QElapsedTimer budget; budget.start();
        do {
            const qsizetype count = std::min<qsizetype>(32, pendingScanFiles_.size() - pendingScanOffset_);
            if (count <= 0) break;
            QElapsedTimer chunk; chunk.start();
            thumbnailModel_->appendFiles(pendingScanFiles_.mid(pendingScanOffset_, count));
            performance::mark(QStringLiteral("directory.model_chunk"), {{"elapsedMs", chunk.elapsed()}, {"items", count}});
            pendingScanOffset_ += count;
            while (!pendingScanBatchEnds_.empty() && pendingScanBatchEnds_.front() <= pendingScanOffset_) {
                pendingScanBatchEnds_.pop_front();
                scanner_->acknowledgeBatch(scanGeneration_);
            }
        } while (budget.elapsed() < 4);
        if (pendingScanOffset_ < pendingScanFiles_.size()) {
            if (pendingScanOffset_ >= 192) {
                pendingScanFiles_ = pendingScanFiles_.mid(pendingScanOffset_);
                for (auto& end : pendingScanBatchEnds_) end -= pendingScanOffset_;
                pendingScanOffset_ = 0;
            }
            scanBatchTimer_->start(0);
        }
        else { pendingScanFiles_.clear(); pendingScanOffset_ = 0; pendingScanBatchEnds_.clear(); }
    });
    viewportOwner_ = QString::number(reinterpret_cast<quintptr>(this));
    filterTimer_ = new QTimer(this);
    filterTimer_->setSingleShot(true);
    filterTimer_->setInterval(100);
    connect(filterTimer_, &QTimer::timeout, this, [this] {
        filterModel_->setFilterFixedString(filterText_);
    });
    refreshTimer_ = new QTimer(this);
    refreshDeadlineTimer_ = new QTimer(this);
    recentCandidateTimer_ = new QTimer(this);
    filterModel_->setSourceModel(thumbnailModel_);
    if (!fileSystemModel_->property("_mvpviewNavigationConfigured").toBool()) {
        fileSystemModel_->setProperty("_mvpviewNavigationConfigured", true);
        // The system navigation pane represents folders only, matching Explorer and Finder.
        // Image filtering belongs to ThumbnailModel; putting files into the navigation model both
        // diverges from native sidebars and needlessly expands large directory trees.
        // FolderNavigator draws its own directory/drive artwork. Asking the native icon provider
        // for custom folder icons is wasted work and can stall drive discovery on Windows when
        // removable or network-backed locations are present.
        fileSystemModel_->setOption(QFileSystemModel::DontUseCustomDirectoryIcons, true);
        // The scanner updates the gallery, not this model. Keep navigation watchers enabled so
        // loaded branches reflect folder creation, deletion and renaming as well.
        fileSystemModel_->setFilter(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Drives);
#ifdef Q_OS_WIN
        // Never ask QFileSystemModel to enumerate the Windows "Computer" root: doing so probes
        // every mapped drive, and an offline server can keep the gatherer busy for minutes. Drive
        // entries are supplied separately from GetLogicalDrives(); this model starts locally.
        fileSystemModel_->setRootPath(windowsSystemDriveRoot());
#else
        // On macOS/Linux, hide the synthetic "/" row and expose its native children directly.
        fileSystemModel_->setRootPath(QDir::rootPath());
#endif
    }

#ifdef Q_OS_WIN
    refreshNativeDrivePlaces();
    auto* driveTimer = new QTimer(this);
    driveTimer->setInterval(2000);
    connect(driveTimer, &QTimer::timeout, this, &BrowseController::refreshNativeDrivePlaces);
    driveTimer->start();
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
    recentLocations_ = settings.value(QStringLiteral("browser/recentLocations")).toStringList();
    recentLocations_.removeIf([](const QString& path) { return path.trimmed().isEmpty(); });
    while (recentLocations_.size() > 12) recentLocations_.removeLast();
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
                    // Refresh scans consume only the final snapshot, but still return
                    // credits for ignored intermediate batches.
                    scanner_->acknowledgeBatch(generation);
                    return;
                }
                pendingScanFiles_ += files;
                pendingScanBatchEnds_.push_back(pendingScanFiles_.size());
                if (!scanBatchTimer_->isActive()) scanBatchTimer_->start(0);
                performance::mark(QStringLiteral("directory.batch"), {{"items", files.size()}});
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
                if (incrementalScan_ && pendingScanOffset_ < pendingScanFiles_.size()) {
                    QTimer::singleShot(0, this, [this, directory, files, generation] {
                        emit scanner_->scanFinished(directory, files, generation);
                    });
                    return;
                }
                performance::mark(QStringLiteral("directory.finished"), {{"items", files.size()}});
                diagnostics::event(diagnostics::Level::Info, diagnostics::browse(), QStringLiteral("directory.scan_complete"),
                    {{"directory", diagnostics::fileId(directory)}, {"generation", static_cast<qint64>(generation)}, {"items", files.size()}}, true);
                if (!incrementalScan_) {
                    bool galleryFileChanged = false;
                    if (!galleryPath_.isEmpty()) {
                        const auto& previous = thumbnailModel_->files();
                        const auto old = std::find_if(previous.cbegin(), previous.cend(),
                            [this](const ImageFileRecord& file) { return file.path == galleryPath_; });
                        const auto updated = std::find_if(files.cbegin(), files.cend(),
                            [this](const ImageFileRecord& file) { return file.path == galleryPath_; });
                        galleryFileChanged = old != previous.cend() &&
                            (updated == files.cend() || old->fileSize != updated->fileSize ||
                             old->modifiedAt != updated->modifiedAt);
                    }
                    thumbnailModel_->updateFiles(files);
                    if (galleryFileChanged) {
                        const QString path = galleryPath_;
                        setGalleryPath({});
                        if (QFileInfo::exists(path)) setGalleryPath(path);
                    }
                    if (!galleryPath_.isEmpty() && QFileInfo::exists(galleryPath_) &&
                        !directoryWatcher_->files().contains(galleryPath_)) {
                        directoryWatcher_->addPath(galleryPath_);
                    }
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
    connect(directoryWatcher_, &QFileSystemWatcher::fileChanged, this, [this] {
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
        // Fall back quietly to a safe local directory instead of opening to an error state.
        if (!QFileInfo(startupDirectory).isDir()) {
            startupDirectory = startupFallbackDirectory();
        }
    }
    openDirectoryInternal(startupDirectory, true);
}

BrowseController::~BrowseController() = default;

QAbstractItemModel* BrowseController::thumbnails() const { return filterModel_; }

QAbstractItemModel* BrowseController::folderTree() { return fileSystemModel_; }

QModelIndex BrowseController::folderRootIndex() const {
#ifdef Q_OS_WIN
    const QString path = currentDirectory_.isEmpty() ? fileSystemModel_->rootPath()
                                                     : windowsDriveRoot(currentDirectory_);
    return path.isEmpty() ? QModelIndex{} : fileSystemModel_->index(path);
#else
    return fileSystemModel_->index(QDir::rootPath());
#endif
}

QModelIndex BrowseController::currentFolderTreeIndex() const {
    return currentDirectory_.isEmpty() ? QModelIndex{} : fileSystemModel_->index(currentDirectory_);
}

QModelIndex BrowseController::folderTreeIndex(const QString& path, const QString& branchPath) const {
    const QModelIndex index = path.isEmpty() ? QModelIndex{}
                                           : fileSystemModel_->index(QDir::cleanPath(path));
    if (branchPath.isEmpty()) return index;
    const auto* branch = folderTreeBranches_.value(branchPath);
    return branch ? branch->indexForSource(index) : QModelIndex{};
}

QAbstractItemModel* BrowseController::folderTreeBranch(const QString& path) {
    auto*& branch = folderTreeBranches_[path];
    if (!branch) branch = new FolderTreeBranchModel(fileSystemModel_, folderTreeIndex(path), this);
    else if (!branch->hasValidRoot()) branch->setRootIndex(folderTreeIndex(path));
    return branch;
}

void BrowseController::loadFolderTreeChildren(const QString& path) {
    if (path.trimmed().isEmpty()) return;
    // setRootPath() controls which directory the QFileSystemModel watches and initially
    // populates. Changing it here used to move that anchor away from the Windows drive root, so
    // restoring E:\\some-folder could leave only that cached branch available to the view.
    // The delegate already represents a model node; fetch that node's children without changing
    // the model root.
    const QModelIndex directoryIndex = fileSystemModel_->index(QDir::cleanPath(path));
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

bool BrowseController::canTransform() const {
    return selectedImagePaths().size() == 1;
}

bool BrowseController::canRestoreSelected() const {
    return canTransform() && ImageTransformer::canRestore(selectedImagePaths().first());
}

QSize BrowseController::selectedImageSize() const {
    if (!canTransform()) return {};
    const QString path = selectedImagePaths().first();
    if (galleryPath_ == path && galleryImageSize_.isValid()) return galleryImageSize_;
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QStringLiteral("raw") || suffix == QStringLiteral("yuv")) {
        auto raw = loader_->rawParameters(path);
        if (!raw) raw = RawPresetStore::loadForFile(path);
        if (!raw) {
            const RawImageParameters inferred = RawPresetStore::inferFromFileName(path);
            if (inferred.size.isValid()) raw = inferred;
        }
        return raw ? raw->size : QSize{};
    }
    return QImageReader(path).size();
}

void BrowseController::openDirectory(const QString& path) { openDirectoryInternal(path, true); }

void BrowseController::openDirectoryUrl(const QUrl& url) {
    if (url.isLocalFile()) openDirectoryInternal(url.toLocalFile(), true);
}

QVariantList BrowseController::nativeSidebarPlaces() const {
    const performance::Scope trace(QStringLiteral("navigation.places"));
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

QVariantList BrowseController::nativeDrivePlaces() const {
    QVariantList drives = nativeDrivePlaces_;
#ifdef Q_OS_WIN
    // UNC shares have no drive letter, but still need a parent row for their folder tree.
    const QString root = QDir::fromNativeSeparators(windowsDriveRoot(currentDirectory_));
    if (root.startsWith(QStringLiteral("//"))) {
        drives.append(QVariantMap{{QStringLiteral("label"), root.chopped(1)},
                                  {QStringLiteral("path"), root},
                                  {QStringLiteral("kind"), QStringLiteral("drive")},
                                  {QStringLiteral("remote"), true}});
    }
#endif
    return drives;
}

void BrowseController::refreshNativeDrivePlaces() {
    const performance::Scope trace(QStringLiteral("navigation.drives"));
    QVariantList drives;
#ifdef Q_OS_WIN
    const DWORD mask = GetLogicalDrives();
    if (mask == 0) return;
    for (int index = 0; index < 26; ++index) {
        if ((mask & (DWORD{1} << index)) == 0) continue;
        const QChar letter = QChar::fromLatin1(static_cast<char>('A' + index));
        const QString path = QStringLiteral("%1:/").arg(letter);
        const QString nativeRoot = QDir::toNativeSeparators(path);
        const UINT type = GetDriveTypeW(reinterpret_cast<LPCWSTR>(nativeRoot.utf16()));
        drives.append(QVariantMap{{QStringLiteral("label"), QStringLiteral("%1:").arg(letter)},
                                  {QStringLiteral("path"), path},
                                  {QStringLiteral("kind"), QStringLiteral("drive")},
                                  {QStringLiteral("remote"), type == DRIVE_REMOTE}});
    }
#endif
    if (drives == nativeDrivePlaces_) return;
    nativeDrivePlaces_ = std::move(drives);
    emit nativeDrivePlacesChanged();
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
    auto restore = [self, startupDirectory, requestGeneration] {
        QFileInfo startupInfo(startupDirectory);
        QString resolvedDirectory;
        if (startupInfo.isDir() && DirectoryScanner::isBrowsableEntry(startupInfo)) {
            resolvedDirectory = startupInfo.absoluteFilePath();
        } else {
            resolvedDirectory = startupFallbackDirectory();
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
    };
#ifdef Q_OS_WIN
    // QFileInfo::isDir() may wait for the Windows network redirector timeout when the saved path
    // is on an offline mapped drive. A detached validation thread lets the application remain
    // interactive and cannot delay process shutdown if that OS call is still pending.
    if (isWindowsRemotePath(startupDirectory)) {
        QTimer::singleShot(1500, this, [this, requestGeneration] {
            if (directoryRequestGeneration_ != requestGeneration || !currentDirectory_.isEmpty())
                return;
            ++directoryRequestGeneration_;
            openDirectoryInternal(startupFallbackDirectory(), true, true);
        });
        std::thread(std::move(restore)).detach();
        return;
    }
#endif
    QThreadPool::globalInstance()->start(std::move(restore), 100);
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

QString BrowseController::navigateToTypedPath(const QString& path) {
    QString candidate = path.trimmed();
    if (candidate.size() >= 2 &&
        ((candidate.startsWith(QLatin1Char('"')) && candidate.endsWith(QLatin1Char('"'))) ||
         (candidate.startsWith(QLatin1Char('\'')) && candidate.endsWith(QLatin1Char('\''))))) {
        candidate = candidate.mid(1, candidate.size() - 2).trimmed();
    }
    if (candidate.isEmpty()) return QStringLiteral("Enter a folder path.");

    const QUrl asUrl(candidate);
    if (asUrl.isLocalFile()) candidate = asUrl.toLocalFile();
    if (candidate == QStringLiteral("~")) {
        candidate = QDir::homePath();
    } else if (candidate.startsWith(QStringLiteral("~/")) ||
               candidate.startsWith(QStringLiteral("~\\"))) {
        candidate = QDir(QDir::homePath()).filePath(candidate.sliced(2));
    }
    if (QDir::isRelativePath(candidate)) {
        const QString base = currentDirectory_.isEmpty() ? QDir::homePath() : currentDirectory_;
        candidate = QDir(base).absoluteFilePath(candidate);
    }

#ifdef Q_OS_WIN
    if (isWindowsRemotePath(candidate)) {
        openDirectoryInternal(QDir::cleanPath(candidate), true);
        return {};
    }
#endif
    const QFileInfo info(QDir::cleanPath(candidate));
    if (!info.exists())
        return QStringLiteral("Folder does not exist: %1").arg(QDir::toNativeSeparators(candidate));
    if (!info.isDir())
        return QStringLiteral("Path is not a folder: %1").arg(QDir::toNativeSeparators(candidate));
    if (!DirectoryScanner::isBrowsableEntry(info))
        return QStringLiteral("Folder is hidden or unreadable: %1")
            .arg(QDir::toNativeSeparators(candidate));

    const QString resolved = info.absoluteFilePath();
    if (resolved != currentDirectory_) openDirectoryInternal(resolved, true, true);
    return {};
}

void BrowseController::clearRecentLocations() {
    if (recentLocations_.isEmpty()) return;
    recentLocations_.clear();
    QSettings().remove(QStringLiteral("browser/recentLocations"));
    emit recentLocationsChanged();
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

QString BrowseController::registerThumbnailViewport() {
    const QString owner = viewportOwner_ + QLatin1Char('/') + QString::number(viewportOwners_.size());
    viewportOwners_.append(owner);
    connect(this, &QObject::destroyed, loader_, [loader = loader_, owner] {
        loader->updateViewport(owner, {}, false);
    });
    return owner;
}

int BrowseController::thumbnailIndexForPath(const QString& path) const {
    const int row = thumbnailModel_->rowForPath(path);
    return row < 0 ? -1 : filterModel_->mapFromSource(thumbnailModel_->index(row)).row();
}

bool BrowseController::performanceTracing() const { return performance::enabled(); }

void BrowseController::reportThumbnailPresentation(const QString& owner, int generation,
                                                    int count, qint64 elapsedMs, bool first, qint64 firstElapsedMs) {
    performance::mark(QStringLiteral("viewport.presented"),
        {{"owner", owner}, {"generation", generation}, {"visible", count}, {"elapsedMs", elapsedMs}, {"first", first}, {"firstElapsedMs", firstElapsedMs}});
}

void BrowseController::reportThumbnailObservation(const QString& state, const QString& owner,
                                                   int generation, int count, qint64 elapsedMs) {
    if (state != QStringLiteral("demand") && state != QStringLiteral("stopped") &&
        state != QStringLiteral("settled") && state != QStringLiteral("retargeted") &&
        state != QStringLiteral("report") && state != QStringLiteral("anchor_layout")) return;
    QJsonObject fields{{"owner", owner}, {"visible", count}, {"elapsedMs", elapsedMs}};
    fields.insert(state == QStringLiteral("demand") ? QStringLiteral("generation") :
                  (state == QStringLiteral("report") || state == QStringLiteral("anchor_layout"))
                    ? QStringLiteral("frame") : QStringLiteral("stopId"), generation);
    performance::mark(QStringLiteral("viewport.") + state, fields);
}

void BrowseController::setThumbnailViewport(const QString& owner, const QVariantList& entries, bool fast) {
    QHash<QString, int> priorities;
    for (const auto& entry : entries) {
        const auto value = entry.toMap();
        const auto path = value.value(QStringLiteral("path")).toString();
        if (!path.isEmpty()) priorities.insert(path, value.value(QStringLiteral("priority")).toInt());
    }
    loader_->updateViewport(owner, priorities, fast);
}

void BrowseController::setFilterText(const QString& text) {
    const QString normalized = text.trimmed();
    if (filterText_ == normalized) {
        return;
    }
    filterText_ = normalized;
    filterTimer_->start();
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
    requestTransferPaths(clipboard.paths, clipboard.cut);
}

void BrowseController::pasteItemsInto(const QString& directory) {
    const FileClipboardContents clipboard = FileClipboard::contents();
    if (clipboard.paths.isEmpty()) {
        setStatusText(QStringLiteral("The clipboard does not contain files"));
        return;
    }
    requestTransferPaths(clipboard.paths, clipboard.cut, directory);
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
    requestTransferPaths(paths, false, directory);
}

void BrowseController::confirmPendingTransfer() {
    if (pendingTransferPaths_.isEmpty()) return;
    const QStringList paths = std::exchange(pendingTransferPaths_, {});
    const QString target = std::exchange(pendingTransferTarget_, {});
    const bool move = std::exchange(pendingTransferMove_, false);
    transferPaths(paths, move, target);
}

void BrowseController::cancelPendingTransfer() {
    pendingTransferPaths_.clear();
    pendingTransferTarget_.clear();
    pendingTransferMove_ = false;
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
    const QString operation = diagnostics::operationId();
    const QJsonObject context{{"operation", operation}, {"file", diagnostics::fileId(source.absoluteFilePath())}};
    diagnostics::event(diagnostics::Level::Info, diagnostics::files(), QStringLiteral("rename.begin"), context, true);
    if (!SingleFileRename::execute(source.absoluteFilePath(), destination, &error)) {
        auto failure = context; failure.insert(QStringLiteral("reason"), error);
        diagnostics::event(diagnostics::Level::Error, diagnostics::files(), QStringLiteral("rename.failed"), failure, true);
        return error;
    }
    diagnostics::event(diagnostics::Level::Info, diagnostics::files(), QStringLiteral("rename.complete"), context, true);
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
    const QString operation = diagnostics::operationId();
    diagnostics::event(diagnostics::Level::Info, diagnostics::files(), QStringLiteral("trash.begin"), {{"operation", operation}, {"count", requestedCount}}, true);
    QStringList failures;
    for (const QString& path : std::as_const(selectedPaths_)) {
        if (!QFile::moveToTrash(path)) {
            diagnostics::event(diagnostics::Level::Error, diagnostics::files(), QStringLiteral("trash.failed"),
                {{"operation", operation}, {"file", diagnostics::fileId(path)}}, true);
            failures.append(QFileInfo(path).fileName());
            continue;
        }
        const QStringList companions{
            RawPresetStore::sidecarPath(path), ImageTransformer::backupPath(path),
            ImageTransformer::backupManifestPath(path),
            RawPresetStore::sidecarPath(ImageTransformer::backupPath(path))};
        for (const QString& companion : companions)
            if (QFileInfo::exists(companion)) QFile::moveToTrash(companion);
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
    diagnostics::event(diagnostics::Level::Info, diagnostics::files(), QStringLiteral("trash.complete"), {{"operation", operation}, {"count", requestedCount}}, true);
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

QString BrowseController::transformSelected(bool clockwise) {
    if (!canTransform()) return QStringLiteral("Select one image first.");
    const QString path = selectedImagePaths().first();
    std::optional<RawImageParameters> raw = loader_->rawParameters(path);
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (!raw && (suffix == QStringLiteral("raw") || suffix == QStringLiteral("yuv")))
        raw = RawPresetStore::loadForFile(path);
    if (!raw && (suffix == QStringLiteral("raw") || suffix == QStringLiteral("yuv"))) {
        const RawImageParameters inferred = RawPresetStore::inferFromFileName(path);
        if (inferred.size.isValid()) raw = inferred;
    }
    if ((suffix == QStringLiteral("raw") || suffix == QStringLiteral("yuv")) && !raw) {
        const QString error =
            QStringLiteral("Configure the RAW/YUV dimensions and pixel format first.");
        setStatusText(error);
        return error;
    }
    const QString error = ImageTransformer::rotate(
        path, clockwise ? QuarterTurn::Clockwise : QuarterTurn::CounterClockwise, raw);
    if (error.isEmpty()) refreshTransformedPath(path);
    else setStatusText(error);
    return error;
}

QString BrowseController::rotateSelectedClockwise() { return transformSelected(true); }

QString BrowseController::rotateSelectedCounterClockwise() { return transformSelected(false); }

QString BrowseController::resizeSelected(int width, int height) {
    if (!canTransform()) return QStringLiteral("Select one image first.");
    if (width <= 0 || height <= 0 || width > 100000 || height > 100000)
        return QStringLiteral("Enter dimensions between 1 and 100,000 pixels.");
    const QString path = selectedImagePaths().first();
    std::optional<RawImageParameters> raw = loader_->rawParameters(path);
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (!raw && (suffix == QStringLiteral("raw") || suffix == QStringLiteral("yuv")))
        raw = RawPresetStore::loadForFile(path);
    if (!raw && (suffix == QStringLiteral("raw") || suffix == QStringLiteral("yuv"))) {
        const RawImageParameters inferred = RawPresetStore::inferFromFileName(path);
        if (inferred.size.isValid()) raw = inferred;
    }
    if ((suffix == QStringLiteral("raw") || suffix == QStringLiteral("yuv")) && !raw)
        return QStringLiteral("Configure the RAW/YUV dimensions and pixel format first.");
    if (raw && raw->isYuv() && ((width & 1) != 0 || (height & 1) != 0))
        return QStringLiteral("YUV 4:2:0 output dimensions must be even.");
    QJsonObject context{{"operation", diagnostics::operationId()}, {"file", diagnostics::fileId(path)}, {"width", width}, {"height", height}};
    diagnostics::event(diagnostics::Level::Info, diagnostics::files(), QStringLiteral("resize.begin"), context, true);
    const QString error = ImageTransformer::resize(path, {width, height}, raw);
    if (!error.isEmpty()) context.insert(QStringLiteral("reason"), error);
    diagnostics::event(error.isEmpty() ? diagnostics::Level::Info : diagnostics::Level::Error, diagnostics::files(),
        error.isEmpty() ? QStringLiteral("resize.complete") : QStringLiteral("resize.failed"), context, true);
    if (error.isEmpty()) refreshTransformedPath(path);
    else setStatusText(error);
    return error;
}

QString BrowseController::restoreSelected() {
    if (!canRestoreSelected()) return QStringLiteral("No original image backup is available.");
    const QString path = selectedImagePaths().first();
    const QString error = ImageTransformer::restore(path);
    if (error.isEmpty()) refreshTransformedPath(path);
    else setStatusText(error);
    return error;
}

void BrowseController::refreshTransformedPath(const QString& path) {
    loader_->clearCache();
    if (const auto raw = RawPresetStore::loadForFile(path)) loader_->setRawParameters(path, *raw);
    thumbnailModel_->invalidateThumbnail(path);
    if (galleryPath_ == path) {
        galleryPath_.clear();
        galleryFrame_.reset();
        setGalleryPath(path);
    }
    emit selectionChanged();
    emit imageTransformed(path);
    rescanCurrentDirectory();
    setStatusText(QStringLiteral("Saved changes to %1").arg(QFileInfo(path).fileName()));
}

void BrowseController::setGalleryPath(const QString& path) {
    const QString normalized = path.isEmpty() ? QString() : QFileInfo(path).absoluteFilePath();
    if (galleryPath_ == normalized && (galleryFrame_ || normalized.isEmpty())) {
        return;
    }
    if (!galleryPath_.isEmpty() && directoryWatcher_->files().contains(galleryPath_)) {
        directoryWatcher_->removePath(galleryPath_);
    }
    galleryPath_ = normalized;
    if (!normalized.isEmpty() && QFileInfo(normalized).isFile()) {
        directoryWatcher_->addPath(normalized);
    }
    galleryUpgradeTimer_->stop();
    galleryPreviewHandle_.cancel();
    galleryFullHandle_.cancel();
    galleryFullRequested_ = false;
    galleryFullResolution_ = false;
    galleryFrame_.reset();
    galleryImageSize_ = {};
    galleryInfoText_.clear();
    emit galleryImageChanged();

    if (normalized.isEmpty() || QFileInfo(normalized).isDir()) {
        return;
    }
    const quint64 requestId = ++galleryRequestId_;
    const QPointer<BrowseController> self(this);
    galleryPreviewHandle_ = loader_->request(
        requestId, {normalized, DecodePurpose::Preview, QSize(2048, 2048)},
        [self, normalized](quint64 completedId, const DecodeResult& result) {
            if (!self || completedId != self->galleryRequestId_ ||
                normalized != self->galleryPath_) {
                return;
            }
            self->applyGalleryFrame(result.frame, false);
        }, RequestOptions{LoadCategory::Interactive, 0, QStringLiteral("gallery")});
}

void BrowseController::requestGalleryFull() {
    if (galleryFullRequested_ || galleryFullResolution_ || galleryPath_.isEmpty()) {
        return;
    }
    galleryFullRequested_ = true;
    const QString requestedPath = galleryPath_;
    const quint64 requestId = galleryRequestId_;
    const QPointer<BrowseController> self(this);
    galleryFullHandle_ = loader_->request(
        requestId, {requestedPath, DecodePurpose::Full, {}},
        [self, requestedPath](quint64 completedId, const DecodeResult& result) {
            if (!self || completedId != self->galleryRequestId_ ||
                requestedPath != self->galleryPath_) {
                return;
            }
            self->galleryFullRequested_ = false;
            if (result.frame) {
                self->applyGalleryFrame(result.frame, true);
            }
        },
        RequestOptions{LoadCategory::Interactive, 10, QStringLiteral("gallery-pixel-probe")});
}

void BrowseController::applyGalleryFrame(const ImageFramePtr& frame, bool fullResolution) {
    galleryFrame_ = frame;
    galleryFullResolution_ = fullResolution && frame;
    if (frame && !fullResolution) galleryUpgradeTimer_->start();
    else galleryUpgradeTimer_->stop();
    if (frame) {
        const RawPlaneAccessor raw(*frame);
        if (raw.isValid()) {
            galleryImageSize_ = raw.displaySize();
        } else if (fullResolution) {
            if (const QImage* image = frame->qImage()) {
                // Full decoded pixels already include EXIF orientation.
                galleryImageSize_ = image->size();
            }
        } else {
            galleryImageSize_ = frame->metadata.sourceSize.isValid()
                                    ? frame->metadata.sourceSize
                                    : frame->descriptor.size;
            if (const QImage* image = frame->qImage();
                image && galleryImageSize_.isValid() &&
                (image->width() > image->height()) !=
                    (galleryImageSize_.width() > galleryImageSize_.height())) {
                galleryImageSize_.transpose();
            }
        }
        if (!galleryImageSize_.isValid()) {
            galleryImageSize_ = frame->descriptor.size;
        }
        const int bits = std::max(1, frame->descriptor.validBits);
        galleryInfoText_ =
            QStringLiteral("%1 × %2  %3-bit  %4  %5")
                .arg(galleryImageSize_.width())
                .arg(galleryImageSize_.height())
                .arg(bits)
                .arg(frame->metadata.format.toUpper())
                .arg(QLocale().formattedDataSize(frame->metadata.fileSize));
    }
    emit galleryImageChanged();
}

QString BrowseController::probeGalleryPixel(int x, int y) {
    if (!galleryFrame_ || !galleryImageSize_.isValid() || x < 0 || y < 0 ||
        x >= galleryImageSize_.width() || y >= galleryImageSize_.height()) {
        return {};
    }
    if (!galleryFullResolution_) {
        requestGalleryFull();
        return QStringLiteral("Loading pixel data…");
    }

    const ComparisonPixelSample sample =
        ComparisonPixelProbe::sampleAtDisplayPixel(*galleryFrame_, QPoint(x, y));
    if (!sample.valid) {
        return {};
    }
    return QStringLiteral("x %1 · y %2 · %3").arg(x).arg(y).arg(sample.sourceValueText());
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
    const performance::Scope trace(QStringLiteral("directory.open_gui"), {{"validated", pathAlreadyValidated}});
    QElapsedTimer phase;
    if (performance::enabled()) phase.start();
    const auto markPhase = [&phase](const char* name) {
        if (!phase.isValid()) return;
        performance::mark(QStringLiteral("directory.open_phase"),
            {{"phase", QString::fromLatin1(name)}, {"elapsedUs", phase.nsecsElapsed() / 1000}});
        phase.restart();
    };
#ifdef Q_OS_WIN
    if (!pathAlreadyValidated && isWindowsRemotePath(path)) {
        const quint64 requestGeneration = ++directoryRequestGeneration_;
        setStatusText(tr("Connecting to network folder…"));
        QTimer::singleShot(5000, this, [this, path, requestGeneration] {
            if (directoryRequestGeneration_ != requestGeneration) return;
            ++directoryRequestGeneration_;
            setStatusText(tr("Network folder is unavailable: %1").arg(path));
        });
        const QPointer<BrowseController> self(this);
        std::thread([self, path, addToHistory, requestGeneration] {
            const QFileInfo info(path);
            const bool browsable = info.exists() && info.isDir()
                && DirectoryScanner::isBrowsableEntry(info);
            if (!self) return;
            QMetaObject::invokeMethod(
                self,
                [self, path, addToHistory, requestGeneration, browsable] {
                    if (!self || self->directoryRequestGeneration_ != requestGeneration) return;
                    if (!browsable) {
                        self->setStatusText(
                            QCoreApplication::translate(
                                "mvpview::BrowseController",
                                "Network folder is unavailable: %1")
                                .arg(path));
                        return;
                    }
                    self->openDirectoryInternal(path, addToHistory, true);
                },
                Qt::QueuedConnection);
        }).detach();
        return;
    }
#endif
    ++directoryRequestGeneration_;
    const QFileInfo info(path);
    if (!pathAlreadyValidated &&
        (!info.exists() || !info.isDir() || !DirectoryScanner::isBrowsableEntry(info))) {
        setStatusText(QStringLiteral("Folder is hidden, unreadable, or unavailable: %1").arg(path));
        return;
    }
    markPhase("validation");
    // The gallery owns an asynchronous decode independently of the thumbnail model. Clear it
    // before publishing the directory change so neither the old frame nor a late completion from
    // the previous folder can remain visible while the new folder is scanned.
    for (const auto& owner : viewportOwners_) loader_->updateViewport(owner, {}, false);
    setGalleryPath({});
    const QString previousDirectory = currentDirectory_;
    currentDirectory_ = info.absoluteFilePath();
#ifdef Q_OS_WIN
    if (windowsDriveRoot(previousDirectory) != windowsDriveRoot(currentDirectory_)
        && (previousDirectory.startsWith(QStringLiteral("//"))
            || currentDirectory_.startsWith(QStringLiteral("//")))) {
        emit nativeDrivePlacesChanged();
    }
#else
    Q_UNUSED(previousDirectory);
#endif
    markPhase("directory-state");
    // Persist at navigation time so an ordinary force-quit or crash still restores the last
    // meaningful workspace on the next start.
    diagnostics::event(diagnostics::Level::Info, diagnostics::browse(), QStringLiteral("directory.open"),
        {{"directory", diagnostics::fileId(currentDirectory_)}}, true);
    recentLocations_.removeAll(currentDirectory_);
    recentLocations_.prepend(currentDirectory_);
    while (recentLocations_.size() > 12) recentLocations_.removeLast();
    {
        QSettings settings;
        settings.setValue(QStringLiteral("browser/lastDirectory"), currentDirectory_);
        settings.setValue(QStringLiteral("browser/recentLocations"), recentLocations_);
    }
    markPhase("persistence");
    emit recentLocationsChanged();
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
    markPhase("notifications");
    if (!directoryWatcher_->directories().isEmpty()) {
        directoryWatcher_->removePaths(directoryWatcher_->directories());
    }
#ifdef Q_OS_WIN
    if (!isWindowsRemotePath(currentDirectory_)) directoryWatcher_->addPath(currentDirectory_);
#else
    directoryWatcher_->addPath(currentDirectory_);
#endif
    markPhase("watcher");
    scanBatchTimer_->stop();
    pendingScanFiles_.clear(); pendingScanOffset_ = 0; pendingScanBatchEnds_.clear();
    thumbnailModel_->setFiles({});
    incrementalScan_ = true;
    setStatusText(QStringLiteral("Scanning %1…").arg(QDir::toNativeSeparators(currentDirectory_)));
    markPhase("model-reset");
    scanGeneration_ = scanner_->scanAsync(currentDirectory_);
    markPhase("scan-start");
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

void BrowseController::setSharedRecentLocations(const QStringList& paths) {
    if (recentLocations_ == paths) return;
    recentLocations_ = paths;
    // The originating navigation/clear already persisted this shared state.
    // Receiving panes must not synchronously rewrite the same settings file.
    emit recentLocationsChanged();
}

void BrowseController::requestTransferPaths(const QStringList& paths, bool move,
                                            const QString& targetDirectory) {
    const QString target = targetDirectory.isEmpty() ? currentDirectory_
                                                      : QFileInfo(targetDirectory).absoluteFilePath();
    if (paths.isEmpty() || target.isEmpty() || !QFileInfo(target).isDir()) {
        return;
    }
    pendingTransferPaths_ = paths;
    pendingTransferMove_ = move;
    pendingTransferTarget_ = target;
    emit transferConfirmationRequested(move, static_cast<int>(paths.size()), target);
}

void BrowseController::transferPaths(const QStringList& paths, bool move,
                                     const QString& targetDirectory) {
    const QString target = targetDirectory.isEmpty() ? currentDirectory_
                                                      : QFileInfo(targetDirectory).absoluteFilePath();
    if (paths.isEmpty() || target.isEmpty() || !QFileInfo(target).isDir()) return;
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

} // namespace mvpview
