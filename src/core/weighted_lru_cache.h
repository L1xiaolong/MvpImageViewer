#pragma once

#include <QHash>
#include <QString>
#include <QVector>

#include <algorithm>
#include <list>
#include <memory>
#include <functional>
#include <limits>
#include <iterator>

namespace mvpview {

template <typename T> class WeightedLruCache final {
  public:
    explicit WeightedLruCache(qsizetype maximumCost) : maximumCost_(maximumCost) {}
    ~WeightedLruCache() { clear(); }
    using Observer = std::function<void(const std::shared_ptr<const T>&, bool)>;
    void setObserver(Observer observer) { Q_ASSERT(entries_.isEmpty()); observer_ = std::move(observer); }
    using Retirer = std::function<void(std::shared_ptr<const T>, qsizetype)>;
    void setRetirer(Retirer retirer) { retirer_ = std::move(retirer); }

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
        if (observer_) observer_(entries_.value(key).value, true);
        currentCost_ += cost;
        trim();
    }

    void erase(const QString& key) {
        auto it = entries_.find(key);
        if (it == entries_.end()) {
            return;
        }
        currentCost_ -= it->cost;
        if (observer_) observer_(it->value, false);
        const auto cost = it->cost;
        auto owner = std::move(it->value);
        order_.erase(it->orderIterator);
        entries_.erase(it);
        retire(std::move(owner), cost);
    }

    void setMaximumCost(qsizetype bytes) { maximumCost_ = std::max<qsizetype>(0, bytes); trim(); }

    void clear() {
        for (auto& entry : entries_) {
            if (observer_) observer_(entry.value, false);
            retire(std::move(entry.value), entry.cost);
        }
        entries_.clear();
        order_.clear();
        currentCost_ = 0;
        pruneNextKey_.clear();
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
    struct PruneResult {
        QVector<std::shared_ptr<const T>> retired;
        qsizetype cost = 0;
        qsizetype examined = 0;
    };
    // Incremental LRU walk, including past externally held entries. Return owners
    // so the caller can release large allocations away from the GUI thread.
    [[nodiscard]] PruneResult pruneUnused(qsizetype maximumExamined = 32,
                                          qsizetype targetCost = std::numeric_limits<qsizetype>::max()) {
        PruneResult result;
        while (!order_.empty() && result.examined < maximumExamined && result.cost < targetCost) {
            auto found = entries_.find(pruneNextKey_);
            if (found == entries_.end()) found = entries_.find(order_.back());
            const QString key = found.key();
            const auto position = found->orderIterator;
            const bool finished = position == order_.begin();
            pruneNextKey_ = finished ? QString{} : *std::prev(position);
            ++result.examined;
            if (found->value.use_count() == 1) {
                result.cost += found->cost;
                result.retired.append(found->value);
                erase(key);
            }
            if (finished) break;
        }
        return result;
    }
    [[nodiscard]] qsizetype size() const { return entries_.size(); }
    [[nodiscard]] qsizetype maximumCost() const { return maximumCost_; }
    [[nodiscard]] bool contains(const QString& key) const { return entries_.contains(key); }

  private:
    struct Entry {
        std::shared_ptr<const T> value;
        qsizetype cost;
        typename std::list<QString>::iterator orderIterator;
    };

    void retire(std::shared_ptr<const T> owner, qsizetype cost) {
        // Transfer the last owner after unlinking, so a fast worker cannot leave
        // the cache's reference as the final (GUI-thread) pixel destructor.
        if (retirer_ && owner.use_count() == 1) retirer_(std::move(owner), cost);
    }

    void trim() {
        while (currentCost_ > maximumCost_ && !order_.empty()) {
            erase(order_.back());
        }
    }

    qsizetype maximumCost_;
    qsizetype currentCost_ = 0;
    std::list<QString> order_;
    QHash<QString, Entry> entries_;
    Observer observer_;
    Retirer retirer_;
    QString pruneNextKey_;
};

} // namespace mvpview
