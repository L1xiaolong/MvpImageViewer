#pragma once

#include "core/image_types.h"
#include "browser/thumbnail_filter_proxy_model.h"
#include "io/image_loader.h"

#include <QFileSystemModel>
#include <QModelIndex>
#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

#include <memory>

QT_BEGIN_NAMESPACE
class QFileSystemWatcher;
class QTimer;
QT_END_NAMESPACE

namespace ispview {

class DirectoryScanner;
class FullScreenWindow;
class IImageDecoder;
class ThumbnailModel;

// QML-facing application service for the Browse design. Filesystem work remains in C++ so the
// visual layer never reimplements scanning, RAW inference, clipboard operations, or Trash logic.
class BrowseController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* thumbnails READ thumbnails CONSTANT)
    Q_PROPERTY(QAbstractItemModel* folderTree READ folderTree CONSTANT)
    Q_PROPERTY(QModelIndex folderRootIndex READ folderRootIndex CONSTANT)
    Q_PROPERTY(QModelIndex currentFolderTreeIndex READ currentFolderTreeIndex NOTIFY currentDirectoryChanged)
    Q_PROPERTY(QString currentDirectory READ currentDirectory NOTIFY currentDirectoryChanged)
    Q_PROPERTY(QString currentFolderName READ currentFolderName NOTIFY currentDirectoryChanged)
    Q_PROPERTY(QStringList recentFolders READ recentFolders NOTIFY recentFoldersChanged)
    Q_PROPERTY(QVariantList nativeSidebarPlaces READ nativeSidebarPlaces CONSTANT)
    Q_PROPERTY(QStringList selectedPaths READ selectedPaths NOTIFY selectionChanged)
    Q_PROPERTY(QList<QUrl> selectedFileUrls READ selectedFileUrls NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedUriList READ selectedUriList NOTIFY selectionChanged)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY navigationStateChanged)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY navigationStateChanged)
    Q_PROPERTY(bool canGoUp READ canGoUp NOTIFY navigationStateChanged)
    Q_PROPERTY(bool canPaste READ canPaste NOTIFY clipboardStateChanged)
    Q_PROPERTY(bool canCompare READ canCompare NOTIFY selectionChanged)
    Q_PROPERTY(QString filterText READ filterText NOTIFY filterTextChanged)
    Q_PROPERTY(int sortMode READ sortMode NOTIFY sortModeChanged)
    Q_PROPERTY(int displayMode READ displayMode WRITE setDisplayMode NOTIFY displayModeChanged)
    Q_PROPERTY(int gridCellWidth READ gridCellWidth WRITE setGridCellWidth NOTIFY gridCellWidthChanged)
    Q_PROPERTY(QSize galleryImageSize READ galleryImageSize NOTIFY galleryImageChanged)
    Q_PROPERTY(bool galleryImageReady READ galleryImageReady NOTIFY galleryImageChanged)
    Q_PROPERTY(QString galleryInfoText READ galleryInfoText NOTIFY galleryImageChanged)
    Q_PROPERTY(bool canEditRaw READ canEditRaw NOTIFY selectionChanged)

  public:
    BrowseController(std::shared_ptr<const IImageDecoder> decoder,
                     const QString& initialDirectory = {}, QObject* parent = nullptr);
    BrowseController(ImageLoader* sharedLoader, QFileSystemModel* sharedFileSystemModel,
                     const QString& initialDirectory, bool startEmpty, QObject* parent = nullptr);
    ~BrowseController() override;

    [[nodiscard]] QAbstractItemModel* thumbnails() const;
    [[nodiscard]] QAbstractItemModel* folderTree();
    [[nodiscard]] QModelIndex folderRootIndex() const;
    [[nodiscard]] QModelIndex currentFolderTreeIndex() const;
    [[nodiscard]] QString currentDirectory() const { return currentDirectory_; }
    [[nodiscard]] QString currentFolderName() const;
    [[nodiscard]] QStringList recentFolders() const { return recentFolders_; }
    [[nodiscard]] QVariantList nativeSidebarPlaces() const;
    [[nodiscard]] QStringList selectedPaths() const { return selectedPaths_; }
    [[nodiscard]] QList<QUrl> selectedFileUrls() const;
    [[nodiscard]] QString selectedUriList() const;
    [[nodiscard]] int selectionCount() const { return static_cast<int>(selectedPaths_.size()); }
    [[nodiscard]] QString statusText() const { return statusText_; }
    [[nodiscard]] bool canGoBack() const { return navigationHistoryIndex_ > 0; }
    [[nodiscard]] bool canGoForward() const;
    [[nodiscard]] bool canGoUp() const;
    [[nodiscard]] bool canPaste() const;
    [[nodiscard]] bool canCompare() const;
    [[nodiscard]] QString filterText() const { return filterText_; }
    [[nodiscard]] int sortMode() const;
    [[nodiscard]] int displayMode() const { return displayMode_; }
    [[nodiscard]] int gridCellWidth() const { return gridCellWidth_; }
    [[nodiscard]] QSize galleryImageSize() const { return galleryImageSize_; }
    [[nodiscard]] bool galleryImageReady() const { return galleryFrame_ != nullptr; }
    [[nodiscard]] QString galleryInfoText() const { return galleryInfoText_; }
    [[nodiscard]] bool canEditRaw() const;
    [[nodiscard]] ImageLoader* loader() const { return loader_; }
    [[nodiscard]] QStringList selectedImagePaths() const;
    void setWorkspaceSelectionOrder(const QStringList& paths);
    void setSharedRecentFolders(const QStringList& paths);

    Q_INVOKABLE void openDirectory(const QString& path);
    Q_INVOKABLE void openDirectoryUrl(const QUrl& url);
    Q_INVOKABLE void loadFolderTreeChildren(const QString& path);
    void restoreInitialDirectoryAsync(const QString& initialDirectory = {});
    Q_INVOKABLE void chooseDirectory();
    Q_INVOKABLE void navigateBack();
    Q_INVOKABLE void navigateForward();
    Q_INVOKABLE void navigateUp();
    Q_INVOKABLE void activatePath(const QString& path);
    Q_INVOKABLE void activateTreeItem(const QString& path);
    Q_INVOKABLE void selectPath(const QString& path, bool extend = false, bool toggle = false);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void setFilterText(const QString& text);
    Q_INVOKABLE void setSortMode(int mode);
    Q_INVOKABLE void setDisplayMode(int mode);
    Q_INVOKABLE QString createFolder(const QString& name);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void openCurrentDirectoryInFileManager();
    Q_INVOKABLE void copySelected(bool cut = false);
    Q_INVOKABLE void pasteItems();
    Q_INVOKABLE void pasteItemsInto(const QString& directory);
    Q_INVOKABLE void copyDroppedUrls(const QList<QUrl>& urls);
    Q_INVOKABLE void copyDroppedUrlsInto(const QList<QUrl>& urls, const QString& directory);
    Q_INVOKABLE void openDroppedUrls(const QList<QUrl>& urls);
    Q_INVOKABLE void renameSelected();
    Q_INVOKABLE QString renameSelectedTo(const QString& newName);
    Q_INVOKABLE void moveSelectedToTrash();
    Q_INVOKABLE QString moveSelectedToTrashConfirmed();
    Q_INVOKABLE void revealSelected();
    Q_INVOKABLE void showSelectedProperties();
    Q_INVOKABLE void openSelected();
    Q_INVOKABLE void compareSelected();
    Q_INVOKABLE void editSelectedRawParameters();
    Q_INVOKABLE void setGalleryPath(const QString& path);
    Q_INVOKABLE QString probeGalleryPixel(int x, int y);

  public slots:
    void setGridCellWidth(int width);

  signals:
    void currentDirectoryChanged();
    void recentFoldersChanged();
    void selectionChanged();
    void statusTextChanged();
    void navigationStateChanged();
    void clipboardStateChanged();
    void sortModeChanged();
    void filterTextChanged();
    void displayModeChanged();
    void gridCellWidthChanged();
    void galleryImageChanged();
    void compareRequested(const QStringList& paths);
    void fullScreenRequested(const QStringList& paths, int initialIndex);
    void propertiesRequested(const QString& path);
    void rawParametersRequested(const QString& path);
    void directorySelectionRequested(const QUrl& initialFolder);
    void renameRequested(const QString& currentName);
    void trashConfirmationRequested(int itemCount);

  private:
    void openDirectoryInternal(const QString& path, bool addToHistory,
                               bool pathAlreadyValidated = false);
    void rescanCurrentDirectory();
    void setStatusText(const QString& text);
    void requestGalleryFull();
    void applyGalleryFrame(const ImageFramePtr& frame, bool fullResolution);
    void updateSelection(const QStringList& paths);
    void transferPaths(const QStringList& paths, bool move, const QString& targetDirectory = {});
    [[nodiscard]] QStringList allImagePaths() const;
    void initialize(const QString& initialDirectory, bool startEmpty);

    ImageLoader* loader_ = nullptr;
    DirectoryScanner* scanner_ = nullptr;
    ThumbnailModel* thumbnailModel_ = nullptr;
    ThumbnailFilterProxyModel* filterModel_ = nullptr;
    QFileSystemModel* fileSystemModel_ = nullptr;
    QFileSystemWatcher* directoryWatcher_ = nullptr;
    QTimer* refreshTimer_ = nullptr;
    QTimer* refreshDeadlineTimer_ = nullptr;
    QTimer* recentCandidateTimer_ = nullptr;
    QString currentDirectory_;
    QString recentCandidateDirectory_;
    QStringList recentFolders_;
    QStringList selectedPaths_;
    QStringList navigationHistory_;
    QString statusText_;
    QString filterText_;
    int navigationHistoryIndex_ = -1;
    quint64 scanGeneration_ = 0;
    quint64 directoryRequestGeneration_ = 0;
    bool incrementalScan_ = false;
    quint64 galleryRequestId_ = 0;
    int gridCellWidth_ = 196;
    QString pendingActivationPath_;
    int displayMode_ = 0;
    QString galleryPath_;
    QString selectionAnchorPath_;
    QString galleryInfoText_;
    QSize galleryImageSize_;
    ImageFramePtr galleryFrame_;
    LoadHandle galleryPreviewHandle_;
    LoadHandle galleryFullHandle_;
    bool galleryFullRequested_ = false;
    bool galleryFullResolution_ = false;
};

} // namespace ispview
