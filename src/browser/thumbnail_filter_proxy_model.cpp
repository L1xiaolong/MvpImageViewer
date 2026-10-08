#include "browser/thumbnail_filter_proxy_model.h"

#include "browser/thumbnail_model.h"

namespace mvpview {

ThumbnailFilterProxyModel::ThumbnailFilterProxyModel(QObject* parent)
    : QSortFilterProxyModel(parent) {
    setDynamicSortFilter(true);
    setFilterCaseSensitivity(Qt::CaseInsensitive);
    setFilterRole(Qt::DisplayRole);
    collator_.setNumericMode(true);
    collator_.setCaseSensitivity(Qt::CaseInsensitive);
    sort(0, Qt::AscendingOrder);
}

void ThumbnailFilterProxyModel::setSortMode(BrowserSortMode mode) {
    if (sortMode_ == mode) {
        return;
    }
    sortMode_ = mode;
    invalidate();
    sort(0, Qt::AscendingOrder);
}

bool ThumbnailFilterProxyModel::naturalNameLessThan(const QModelIndex& left,
                                                    const QModelIndex& right) const {
    if (const auto* model = qobject_cast<const ThumbnailModel*>(sourceModel())) {
        const auto& a = model->files().at(left.row()).nameSortKey;
        const auto& b = model->files().at(right.row()).nameSortKey;
        if (a && b) return a->compare(*b) < 0;
    }
    return collator_.compare(left.data(Qt::DisplayRole).toString(),
                             right.data(Qt::DisplayRole).toString()) < 0;
}

bool ThumbnailFilterProxyModel::lessThan(const QModelIndex& left, const QModelIndex& right) const {
    const bool leftDirectory = left.data(ThumbnailModel::DirectoryRole).toBool();
    const bool rightDirectory = right.data(ThumbnailModel::DirectoryRole).toBool();
    if (leftDirectory != rightDirectory) {
        return leftDirectory;
    }
    switch (sortMode_) {
    case BrowserSortMode::ModifiedTime: {
        const QDateTime leftTime = left.data(ThumbnailModel::ModifiedRole).toDateTime();
        const QDateTime rightTime = right.data(ThumbnailModel::ModifiedRole).toDateTime();
        if (leftTime != rightTime) {
            return leftTime < rightTime;
        }
        break;
    }
    case BrowserSortMode::Size: {
        const qint64 leftSize = left.data(ThumbnailModel::SizeRole).toLongLong();
        const qint64 rightSize = right.data(ThumbnailModel::SizeRole).toLongLong();
        if (leftSize != rightSize) {
            return leftSize < rightSize;
        }
        break;
    }
    case BrowserSortMode::Type: {
        if (const auto* model = qobject_cast<const ThumbnailModel*>(sourceModel())) {
            const auto& a = model->files().at(left.row()).typeSortKey;
            const auto& b = model->files().at(right.row()).typeSortKey;
            if (a && b) { const int comparison = a->compare(*b); if (comparison) return comparison < 0; }
        }
        const int typeComparison =
            collator_.compare(left.data(ThumbnailModel::TypeRole).toString(),
                              right.data(ThumbnailModel::TypeRole).toString());
        if (typeComparison != 0) {
            return typeComparison < 0;
        }
        break;
    }
    case BrowserSortMode::Name:
        break;
    }
    return naturalNameLessThan(left, right);
}

} // namespace mvpview
