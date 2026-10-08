#pragma once

#include <QAbstractProxyModel>
#include <QPersistentModelIndex>
#include <QSortFilterProxyModel>
#include <QCollator>

namespace mvpview {

// Expose one filesystem branch as a model with an invisible root. Only forward inserts and
// removals from that branch, so a TreeView never confuses drive rows with folder rows.
class FolderTreeRootModel final : public QAbstractProxyModel {
public:
    FolderTreeRootModel(QAbstractItemModel* source, const QModelIndex& root, QObject* parent)
        : QAbstractProxyModel(parent), root_(root) {
        setSourceModel(source);
        connect(source, &QAbstractItemModel::rowsAboutToBeInserted, this,
                [this](const QModelIndex& parent, int first, int last) {
                    inserting_ = contains(parent);
                    if (inserting_) beginInsertRows(mapFromSource(parent), first, last);
                });
        connect(source, &QAbstractItemModel::rowsInserted, this, [this] {
            if (inserting_) endInsertRows();
            inserting_ = false;
        });
        connect(source, &QAbstractItemModel::rowsAboutToBeRemoved, this,
                [this](const QModelIndex& parent, int first, int last) {
                    removing_ = contains(parent);
                    if (removing_) beginRemoveRows(mapFromSource(parent), first, last);
                    else {
                        QModelIndex ancestor = root_;
                        while (ancestor.isValid() && ancestor.parent() != parent)
                            ancestor = ancestor.parent();
                        resetting_ = ancestor.isValid()
                            && ancestor.row() >= first && ancestor.row() <= last;
                        if (resetting_) beginResetModel();
                    }
                });
        connect(source, &QAbstractItemModel::rowsRemoved, this, [this] {
            if (removing_) endRemoveRows();
            if (resetting_) endResetModel();
            removing_ = false;
            resetting_ = false;
        });
        connect(source, &QAbstractItemModel::dataChanged, this,
                [this](const QModelIndex& first, const QModelIndex& last, const QList<int>& roles) {
                    if (contains(first) && first != root_)
                        emit dataChanged(mapFromSource(first.siblingAtColumn(0)),
                                         mapFromSource(last.siblingAtColumn(0)), roles);
                });
        connect(source, &QAbstractItemModel::layoutAboutToBeChanged, this, [this] {
            emit layoutAboutToBeChanged();
            oldIndexes_ = persistentIndexList();
            sourceIndexes_.clear();
            for (const auto& index : oldIndexes_) sourceIndexes_.append(mapToSource(index));
        });
        connect(source, &QAbstractItemModel::layoutChanged, this, [this] {
            QModelIndexList updated;
            for (const auto& index : sourceIndexes_) updated.append(mapFromSource(index));
            changePersistentIndexList(oldIndexes_, updated);
            oldIndexes_.clear();
            sourceIndexes_.clear();
            emit layoutChanged();
        });
        connect(source, &QAbstractItemModel::modelAboutToBeReset, this,
                [this] { beginResetModel(); });
        connect(source, &QAbstractItemModel::modelReset, this, [this] { endResetModel(); });
    }

    void setRootIndex(const QModelIndex& root) {
        if (root_ == root) return;
        beginResetModel();
        root_ = root;
        endResetModel();
    }
    bool hasValidRoot() const { return root_.isValid(); }

    QModelIndex mapToSource(const QModelIndex& index) const override {
        if (!index.isValid() || index.model() != this) return {};
        return createSourceIndex(index.row(), index.column(), index.internalPointer());
    }
    QModelIndex mapFromSource(const QModelIndex& index) const override {
        if (!index.isValid() || index.column() != 0 || index == root_ || !contains(index)) return {};
        return createIndex(index.row(), index.column(), index.internalPointer());
    }
    QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override {
        if (!root_.isValid() || row < 0 || column != 0) return {};
        return mapFromSource(sourceModel()->index(row, column, sourceParent(parent)));
    }
    QModelIndex parent(const QModelIndex& index) const override {
        return mapFromSource(mapToSource(index).parent());
    }
    int rowCount(const QModelIndex& parent = {}) const override {
        return root_.isValid() ? sourceModel()->rowCount(sourceParent(parent)) : 0;
    }
    int columnCount(const QModelIndex& = {}) const override { return 1; }
    bool hasChildren(const QModelIndex& parent = {}) const override {
        return root_.isValid() && sourceModel()->hasChildren(sourceParent(parent));
    }
    bool canFetchMore(const QModelIndex& parent) const override {
        return root_.isValid() && sourceModel()->canFetchMore(sourceParent(parent));
    }
    void fetchMore(const QModelIndex& parent) override {
        if (root_.isValid()) sourceModel()->fetchMore(sourceParent(parent));
    }
    QHash<int, QByteArray> roleNames() const override { return sourceModel()->roleNames(); }

private:
    QModelIndex sourceParent(const QModelIndex& parent) const {
        return parent.isValid() ? mapToSource(parent) : QModelIndex(root_);
    }
    bool contains(QModelIndex index) const {
        if (!root_.isValid() || index.model() != sourceModel()) return false;
        while (index.isValid()) {
            if (index == root_) return true;
            index = index.parent();
        }
        return false;
    }
    QPersistentModelIndex root_;
    QModelIndexList oldIndexes_;
    QList<QPersistentModelIndex> sourceIndexes_;
    bool inserting_ = false;
    bool removing_ = false;
    bool resetting_ = false;
};

// QFileSystemModel can leave eagerly resolved nodes first on drives outside its rootPath.
// Sort each visible branch independently, using the same natural, locale-aware folder order.
class FolderTreeBranchModel final : public QSortFilterProxyModel {
public:
    FolderTreeBranchModel(QAbstractItemModel* source, const QModelIndex& root, QObject* parent)
        : QSortFilterProxyModel(parent), branch_(new FolderTreeRootModel(source, root, this)) {
        collator_.setNumericMode(true);
        collator_.setCaseSensitivity(Qt::CaseInsensitive);
        setSourceModel(branch_);
        sort(0, Qt::AscendingOrder);
    }
    QModelIndex indexForSource(const QModelIndex& index) const {
        return mapFromSource(branch_->mapFromSource(index));
    }
    void setRootIndex(const QModelIndex& root) { branch_->setRootIndex(root); }
    bool hasValidRoot() const { return branch_->hasValidRoot(); }

protected:
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override {
        return collator_.compare(left.data(Qt::DisplayRole).toString(),
                                 right.data(Qt::DisplayRole).toString()) < 0;
    }

private:
    FolderTreeRootModel* branch_;
    QCollator collator_;
};

} // namespace mvpview
