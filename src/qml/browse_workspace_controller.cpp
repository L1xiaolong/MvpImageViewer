#include "qml/browse_workspace_controller.h"

#include "io/image_loader.h"
#include "qml/browse_controller.h"

#include <QFileSystemModel>
#include <QSet>

#include <algorithm>
#include <utility>

namespace ispview {

BrowseWorkspaceController::BrowseWorkspaceController(
    std::shared_ptr<const IImageDecoder> decoder, const QString& initialDirectory,
    bool deferInitialDirectory, QObject* parent)
    : QObject(parent), loader_(new ImageLoader(std::move(decoder), this)),
      folderTreeModel_(new QFileSystemModel(this)) {
    emptyPane_ = new BrowseController(loader_, folderTreeModel_, {}, true, this);
    panes_.append(createPane(deferInitialDirectory ? QString{} : initialDirectory,
                             deferInitialDirectory));
    if (deferInitialDirectory) {
        deferredInitialDirectory_ = initialDirectory;
        deferredInitialDirectoryPending_ = true;
    }
    activePaneIndex_ = 0;
    recomputeSelection();
}

QVariantList BrowseWorkspaceController::panes() const {
    QVariantList result;
    result.reserve(panes_.size());
    for (BrowseController* pane : panes_) {
        result.append(QVariant::fromValue(static_cast<QObject*>(pane)));
    }
    return result;
}

QObject* BrowseWorkspaceController::activePane() const {
    BrowseController* active = activeBrowsePane();
    return active ? active : emptyPane_;
}

BrowseController* BrowseWorkspaceController::activeBrowsePane() const {
    return activePaneIndex_ >= 0 && activePaneIndex_ < panes_.size()
        ? panes_.at(activePaneIndex_) : nullptr;
}

bool BrowseWorkspaceController::canCompare() const {
    return selectedImagePaths_.size() >= 2 && selectedImagePaths_.size() <= 4;
}

QString BrowseWorkspaceController::statusText() const {
    const BrowseController* active = activeBrowsePane();
    if (!active) return QStringLiteral("Choose a folder for this file manager");
    QString result = active->statusText();
    if (!selectedImagePaths_.isEmpty()) {
        int selectedPanes = 0;
        for (const BrowseController* pane : panes_)
            if (!pane->selectedImagePaths().isEmpty()) ++selectedPanes;
        result += QStringLiteral(" · %1 image(s) across %2 manager(s)")
                      .arg(selectedImagePaths_.size()).arg(selectedPanes);
    }
    return result;
}

BrowseController* BrowseWorkspaceController::createPane(const QString& initialDirectory,
                                                        bool startEmpty) {
    auto* pane = new BrowseController(loader_, folderTreeModel_, initialDirectory, startEmpty, this);
    connectPane(pane);
    return pane;
}

void BrowseWorkspaceController::connectPane(BrowseController* pane) {
    connect(pane, &BrowseController::selectionChanged, this,
            &BrowseWorkspaceController::recomputeSelection);
    connect(pane, &BrowseController::statusTextChanged, this, [this, pane] {
        if (pane == activeBrowsePane()) emit statusTextChanged();
    });
    connect(pane, &BrowseController::currentDirectoryChanged, this, [this, pane] {
        if (pane == activeBrowsePane()) {
            emit activePaneChanged();
            emit statusTextChanged();
        }
    });
    connect(pane, &BrowseController::recentFoldersChanged, this,
            [this, pane] { synchronizeRecentFolders(pane); });
    connect(pane, &BrowseController::transferConfirmationRequested, this,
            [this, pane](bool move, int itemCount, const QString& targetDirectory) {
                if (pendingTransferPane_ && pendingTransferPane_ != pane) {
                    pendingTransferPane_->cancelPendingTransfer();
                }
                pendingTransferPane_ = pane;
                emit transferConfirmationRequested(move, itemCount, targetDirectory);
            });
}

void BrowseWorkspaceController::confirmPendingTransfer() {
    BrowseController* pane = std::exchange(pendingTransferPane_, nullptr);
    if (pane) pane->confirmPendingTransfer();
}

void BrowseWorkspaceController::cancelPendingTransfer() {
    BrowseController* pane = std::exchange(pendingTransferPane_, nullptr);
    if (pane) pane->cancelPendingTransfer();
}

void BrowseWorkspaceController::synchronizeRecentFolders(BrowseController* source) {
    if (synchronizingRecentFolders_) return;
    synchronizingRecentFolders_ = true;
    const QStringList recent = source->recentFolders();
    emptyPane_->setSharedRecentFolders(recent);
    for (BrowseController* pane : panes_)
        if (pane != source) pane->setSharedRecentFolders(recent);
    synchronizingRecentFolders_ = false;
}

void BrowseWorkspaceController::addFileManagerPane() {
    if (!canAddPane()) return;
    panes_.append(createPane({}, true));
    activePaneIndex_ = static_cast<int>(panes_.size()) - 1;
    normalizeDisplayModes();
    emit panesChanged();
    emit activePaneChanged();
    emit statusTextChanged();
}

void BrowseWorkspaceController::activatePane(int index) {
    if (index < 0 || index >= panes_.size() || activePaneIndex_ == index) return;
    activePaneIndex_ = index;
    emit activePaneChanged();
    emit statusTextChanged();
}

void BrowseWorkspaceController::closePane(int index) {
    if (index < 0 || index >= panes_.size()) return;
    if (panes_.size() == 1) {
        BrowseController* removed = panes_.takeFirst();
        if (pendingTransferPane_ == removed) pendingTransferPane_ = nullptr;
        panes_.append(createPane({}, true));
        activePaneIndex_ = 0;
        removed->deleteLater();
        recomputeSelection();
        emit panesChanged();
        emit activePaneChanged();
        emit statusTextChanged();
        return;
    }
    const int previousActiveIndex = activePaneIndex_;
    BrowseController* removed = panes_.takeAt(index);
    if (pendingTransferPane_ == removed) pendingTransferPane_ = nullptr;
    const bool closedActive = activePaneIndex_ == index;
    if (panes_.isEmpty()) {
        activePaneIndex_ = -1;
    } else if (closedActive) {
        activePaneIndex_ = std::min(index, static_cast<int>(panes_.size()) - 1);
    } else if (index < activePaneIndex_) {
        --activePaneIndex_;
    }
    removed->deleteLater();
    normalizeDisplayModes();
    recomputeSelection();
    emit panesChanged();
    if (closedActive || activePaneIndex_ != previousActiveIndex) emit activePaneChanged();
    emit statusTextChanged();
}

void BrowseWorkspaceController::selectPath(int paneIndex, const QString& path, bool extend,
                                           bool toggle) {
    if (paneIndex < 0 || paneIndex >= panes_.size() || path.isEmpty()) return;
    activatePane(paneIndex);
    panes_.at(paneIndex)->selectPath(path, extend, toggle);
}

void BrowseWorkspaceController::setActiveDisplayMode(int mode) {
    BrowseController* active = activeBrowsePane();
    if (!active) return;
    if (panes_.size() >= 3) {
        active->setDisplayMode(1);
        return;
    }
    if (panes_.size() == 2 && mode == 2) return;
    active->setDisplayMode(mode);
}

void BrowseWorkspaceController::normalizeDisplayModes() {
    if (panes_.size() >= 3) {
        for (BrowseController* pane : panes_) pane->setDisplayMode(1);
        return;
    }
    if (panes_.size() == 2) {
        for (BrowseController* pane : panes_)
            if (pane->displayMode() == 2) pane->setDisplayMode(0);
    }
}

void BrowseWorkspaceController::compareSelected() {
    if (!canCompare()) return;
    emit compareRequested(selectedImagePaths_);
}

void BrowseWorkspaceController::refreshAll() {
    for (BrowseController* pane : panes_) pane->refresh();
}

void BrowseWorkspaceController::startDeferredInitialDirectory() {
    if (!deferredInitialDirectoryPending_) return;
    deferredInitialDirectoryPending_ = false;
    const QString initialDirectory = std::exchange(deferredInitialDirectory_, QString{});
    if (!panes_.isEmpty()) {
        panes_.constFirst()->restoreInitialDirectoryAsync(initialDirectory);
    }
}

void BrowseWorkspaceController::recomputeSelection() {
    QStringList allOrdered;
    QStringList imagesOrdered;
    QSet<QString> seenAll;
    QSet<QString> seenImages;
    for (BrowseController* pane : panes_) {
        for (const QString& path : pane->selectedPaths()) {
            if (!seenAll.contains(path)) {
                seenAll.insert(path);
                allOrdered.append(path);
            }
        }
        for (const QString& path : pane->selectedImagePaths()) {
            if (!seenImages.contains(path)) {
                seenImages.insert(path);
                imagesOrdered.append(path);
            }
        }
    }
    for (BrowseController* pane : panes_) pane->setWorkspaceSelectionOrder(allOrdered);
    if (selectedImagePaths_ == imagesOrdered) {
        emit statusTextChanged();
        return;
    }
    selectedImagePaths_ = imagesOrdered;
    emit workspaceSelectionChanged();
    emit statusTextChanged();
}

} // namespace ispview
