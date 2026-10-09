#pragma once
#include "core/image_types.h"
#include "core/weighted_lru_cache.h"
#include <QMutex>
#include <QMutexLocker>
#include <mutex>
namespace mvpview {
struct SourceFrame {
    QByteArray bytes;
    std::shared_ptr<const PlaneBufferSet> planes;
    std::optional<RawImageParameters> parameters;
    ImageMetadata metadata;
    PixelMemoryOwnership pixelOwnership;
    void appendPixelStorage(PixelStorageFootprint& footprint) const {
        footprint.add(bytes);
        if (planes) { footprint.add(planes->storage); footprint.add(planes->displayImage); }
    }
    qsizetype byteSize() const { PixelStorageFootprint footprint; appendPixelStorage(footprint); return footprint.bytes(); }
};
// Shared by decoder adapters, bounded independently and included in the loader budget.
class SourceFrameCache {
public:
    explicit SourceFrameCache(std::shared_ptr<PixelMemoryLedger> ledger = {},
                              std::shared_ptr<PixelMemoryLedger> resident = {}) : resident_(std::move(resident)) {
        if (ledger) cache_.setObserver([ledger](const auto& frame, bool added) {
            PixelStorageFootprint footprint; frame->appendPixelStorage(footprint);
            ledger->adjust(footprint, added);
        });
    }
    std::shared_ptr<const SourceFrame> get(const QString& key) { QMutexLocker lock(&mutex_); return cache_.get(key); }
    void put(const QString& key, std::shared_ptr<const SourceFrame> frame) {
        if (resident_) {
            auto tracked=std::make_shared<SourceFrame>(*frame);
            PixelStorageFootprint footprint; tracked->appendPixelStorage(footprint);
            tracked->pixelOwnership.attach(resident_,std::move(footprint));
            frame=std::move(tracked);
        }
        QMutexLocker lock(&mutex_); cache_.put(key, frame, frame->byteSize());
    }
    qsizetype cost() const { QMutexLocker lock(&mutex_); return cache_.cost(); }
    void clear() { QMutexLocker lock(&mutex_); cache_.clear(); }
    void setBudget(qsizetype bytes) { QMutexLocker lock(&mutex_); cache_.setMaximumCost(bytes); }
    WeightedLruCache<SourceFrame>::PruneResult pruneUnused(qsizetype maximumExamined, qsizetype targetCost) {
        const std::unique_lock<QMutex> lock(mutex_,std::try_to_lock);
        if (!lock.owns_lock()) return {};
        return cache_.pruneUnused(maximumExamined,targetCost);
    }
private:
    std::shared_ptr<PixelMemoryLedger> resident_;
    mutable QMutex mutex_;
    WeightedLruCache<SourceFrame> cache_{96LL * 1024 * 1024};
};
}
