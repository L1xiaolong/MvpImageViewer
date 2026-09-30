#pragma once

#include <QCollator>
#include <QSortFilterProxyModel>

namespace mvpview {

enum class BrowserSortMode { Name, ModifiedTime, Size, Type };

// Sorting/filtering policy for browser data, independent of its QML presentation.
class ThumbnailFilterProxyModel final : public QSortFilterProxyModel {
    Q_OBJECT

  public:
    explicit ThumbnailFilterProxyModel(QObject* parent = nullptr);

    void setSortMode(BrowserSortMode mode);
    [[nodiscard]] BrowserSortMode sortMode() const { return sortMode_; }

  protected:
    [[nodiscard]] bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

  private:
    [[nodiscard]] bool naturalNameLessThan(const QModelIndex& left, const QModelIndex& right) const;

    BrowserSortMode sortMode_ = BrowserSortMode::Name;
    QCollator collator_;
};

} // namespace mvpview
