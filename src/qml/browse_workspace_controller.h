#pragma once

#include <QObject>
#include <QFileSystemModel>
#include <QStringList>
#include <QVariantList>
#include <QVector>

#include <memory>

namespace ispview {

class BrowseController;
class IImageDecoder;
class ImageLoader;

// Owns the embedded 1-4 file-manager workspace. Each pane keeps an independent
// browsing session while decoding and thumbnail caches remain shared.
class BrowseWorkspaceController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList panes READ panes NOTIFY panesChanged)
    Q_PROPERTY(int paneCount READ paneCount NOTIFY panesChanged)
    Q_PROPERTY(QObject* activePane READ activePane NOTIFY activePaneChanged)
    Q_PROPERTY(bool hasActivePane READ hasActivePane NOTIFY activePaneChanged)
    Q_PROPERTY(int activePaneIndex READ activePaneIndex NOTIFY activePaneChanged)
    Q_PROPERTY(bool canAddPane READ canAddPane NOTIFY panesChanged)
    Q_PROPERTY(QStringList workspaceSelectedPaths READ workspaceSelectedPaths
                   NOTIFY workspaceSelectionChanged)
    Q_PROPERTY(int workspaceSelectionCount READ workspaceSelectionCount
                   NOTIFY workspaceSelectionChanged)
    Q_PROPERTY(bool canCompare READ canCompare NOTIFY workspaceSelectionChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)

  public:
    BrowseWorkspaceController(std::shared_ptr<const IImageDecoder> decoder,
                              const QString& initialDirectory = {},
                              bool deferInitialDirectory = false, QObject* parent = nullptr);

    [[nodiscard]] QVariantList panes() const;
    [[nodiscard]] int paneCount() const { return static_cast<int>(panes_.size()); }
    [[nodiscard]] QObject* activePane() const;
    [[nodiscard]] bool hasActivePane() const { return activeBrowsePane() != nullptr; }
    [[nodiscard]] BrowseController* activeBrowsePane() const;
    [[nodiscard]] int activePaneIndex() const { return activePaneIndex_; }
    [[nodiscard]] bool canAddPane() const { return panes_.size() < 4; }
    [[nodiscard]] QStringList workspaceSelectedPaths() const { return selectedImagePaths_; }
    [[nodiscard]] int workspaceSelectionCount() const {
        return static_cast<int>(selectedImagePaths_.size());
    }
    [[nodiscard]] bool canCompare() const;
    [[nodiscard]] QString statusText() const;
    [[nodiscard]] ImageLoader* loader() const { return loader_; }

    Q_INVOKABLE void addFileManagerPane();
    Q_INVOKABLE void activatePane(int index);
    Q_INVOKABLE void closePane(int index);
    Q_INVOKABLE void selectPath(int paneIndex, const QString& path, bool extend = false,
                                bool toggle = false);
    Q_INVOKABLE void setActiveDisplayMode(int mode);
    Q_INVOKABLE void compareSelected();
    Q_INVOKABLE void refreshAll();
    Q_INVOKABLE void startDeferredInitialDirectory();
    Q_INVOKABLE void confirmPendingTransfer();
    Q_INVOKABLE void cancelPendingTransfer();

  signals:
    void panesChanged();
    void activePaneChanged();
    void workspaceSelectionChanged();
    void statusTextChanged();
    void compareRequested(const QStringList& paths);
    void transferConfirmationRequested(bool move, int itemCount,
                                       const QString& targetDirectory);

  private:
    BrowseController* createPane(const QString& initialDirectory, bool startEmpty);
    void recomputeSelection();
    void connectPane(BrowseController* pane);
    void synchronizeRecentFolders(BrowseController* source);
    void normalizeDisplayModes();

    ImageLoader* loader_ = nullptr;
    QFileSystemModel* folderTreeModel_ = nullptr;
    BrowseController* emptyPane_ = nullptr;
    QVector<BrowseController*> panes_;
    QStringList selectedImagePaths_;
    QString deferredInitialDirectory_;
    bool deferredInitialDirectoryPending_ = false;
    int activePaneIndex_ = -1;
    bool synchronizingRecentFolders_ = false;
    BrowseController* pendingTransferPane_ = nullptr;
};

} // namespace ispview
