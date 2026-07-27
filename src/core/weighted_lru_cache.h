#pragma once

#include <QHash>
#include <QString>

#include <list>
#include <memory>

namespace ispview {

template <typename T> class WeightedLruCache final {
  public:
    explicit WeightedLruCache(qsizetype maximumCost) : maximumCost_(maximumCost) {}

    [[nodiscard]] std::shared_ptr<const T> get(const QString& key) {
        auto it = entries_.find(key);
        if (it == entries_.end()) {
            return {};
        }
        order_.splice(order_.begin(), order_, it->orderIterator);
        return it->value;
    }

    void put(QString key, std::shared_ptr<const T> value, qsizetype cost) {
        if (!value || cost <= 0 || cost > maximumCost_) {
            return;
        }
        erase(key);
        order_.push_front(key);
        entries_.insert(key, Entry{std::move(value), cost, order_.begin()});
        currentCost_ += cost;
        trim();
    }

    void erase(const QString& key) {
        auto it = entries_.find(key);
        if (it == entries_.end()) {
            return;
        }
        currentCost_ -= it->cost;
        order_.erase(it->orderIterator);
        entries_.erase(it);
    }

    void clear() {
        entries_.clear();
        order_.clear();
        currentCost_ = 0;
    }

    [[nodiscard]] qsizetype evictLeastRecentlyUsed() {
        if (order_.empty()) {
            return 0;
        }
        const QString key = order_.back();
        const auto it = entries_.constFind(key);
        const qsizetype removedCost = it == entries_.cend() ? 0 : it->cost;
        erase(key);
        return removedCost;
    }

    [[nodiscard]] qsizetype cost() const { return currentCost_; }
    [[nodiscard]] qsizetype size() const { return entries_.size(); }
    [[nodiscard]] qsizetype maximumCost() const { return maximumCost_; }
    [[nodiscard]] bool contains(const QString& key) const { return entries_.contains(key); }

  private:
    struct Entry {
        std::shared_ptr<const T> value;
        qsizetype cost;
        typename std::list<QString>::iterator orderIterator;
    };

    void trim() {
        while (currentCost_ > maximumCost_ && !order_.empty()) {
            erase(order_.back());
        }
    }

    qsizetype maximumCost_;
    qsizetype currentCost_ = 0;
    std::list<QString> order_;
    QHash<QString, Entry> entries_;
};

} // namespace ispview
