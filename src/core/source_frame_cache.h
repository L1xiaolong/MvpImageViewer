#pragma once
#include "core/image_types.h"
#include "core/weighted_lru_cache.h"
#include <QMutex>
#include <QMutexLocker>
namespace mvpview {
struct SourceFrame {
    QByteArray bytes;
    std::shared_ptr<const PlaneBufferSet> planes;
    std::optional<RawImageParameters> parameters;
    ImageMetadata metadata;
    qsizetype byteSize() const { return planes ? planes->storage.size() : bytes.size(); }
};
// Shared by decoder adapters, bounded independently and included in the loader budget.
class SourceFrameCache {
public:
    std::shared_ptr<const SourceFrame> get(const QString& key) { QMutexLocker lock(&mutex_); return cache_.get(key); }
    void put(const QString& key, std::shared_ptr<const SourceFrame> frame) {
        QMutexLocker lock(&mutex_); cache_.put(key, frame, frame->byteSize());
    }
    qsizetype cost() const { QMutexLocker lock(&mutex_); return cache_.cost(); }
    void clear() { QMutexLocker lock(&mutex_); cache_.clear(); }
    void setBudget(qsizetype bytes) { QMutexLocker lock(&mutex_); cache_.setMaximumCost(bytes); }
private:
    mutable QMutex mutex_;
    WeightedLruCache<SourceFrame> cache_{96LL * 1024 * 1024};
};
}
