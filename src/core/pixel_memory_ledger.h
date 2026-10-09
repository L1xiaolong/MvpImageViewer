#pragma once

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QMutexLocker>

namespace mvpview {

// Logical pixel storage, identified through immutable backing addresses. Does
// not detach implicitly shared Qt buffers or include allocator/driver overhead.
class PixelStorageFootprint final {
public:
    void add(const QImage& image) { add(image.constBits(), image.sizeInBytes()); }
    void add(const QByteArray& bytes) { add(bytes.constData(), bytes.size()); }
    const QHash<const void*, qsizetype>& allocations() const { return allocations_; }
    qsizetype bytes() const {
        qsizetype total = 0;
        for (auto size : allocations_) total += size;
        return total;
    }
private:
    void add(const void* address, qsizetype bytes) {
        if (address && bytes > 0) allocations_[address] = qMax(allocations_.value(address), bytes);
    }
    QHash<const void*, qsizetype> allocations_;
};

// Incremental ownership accounting. The owner must retain its immutable buffers
// until remove; multiple caches/metadata copies can reference the same allocation.
class PixelMemoryLedger final {
public:
    void adjust(const PixelStorageFootprint& footprint, bool added) {
        QMutexLocker lock(&mutex_);
        for (auto it = footprint.allocations().cbegin(); it != footprint.allocations().cend(); ++it) {
            if (added) {
                auto& allocation = allocations_[it.key()];
                ++allocation.sizes[it.value()];
                if (it.value() > allocation.maximum) {
                    bytes_ += it.value() - allocation.maximum;
                    allocation.maximum = it.value();
                }
            } else {
                auto found = allocations_.find(it.key());
                Q_ASSERT(found != allocations_.end() && found->sizes.value(it.value()) > 0);
                if (found == allocations_.end()) continue;
                auto size = found->sizes.find(it.value());
                if (size == found->sizes.end()) continue;
                if (--size.value() == 0) found->sizes.erase(size);
                qsizetype maximum = 0;
                for (auto value = found->sizes.cbegin(); value != found->sizes.cend(); ++value)
                    maximum = qMax(maximum, value.key());
                bytes_ -= found->maximum - maximum;
                found->maximum = maximum;
                if (found->sizes.isEmpty()) allocations_.erase(found);
            }
        }
    }
    qsizetype bytes() const { QMutexLocker lock(&mutex_); return bytes_; }
private:
    struct Allocation { QHash<qsizetype, int> sizes; qsizetype maximum = 0; };
    mutable QMutex mutex_;
    QHash<const void*, Allocation> allocations_;
    qsizetype bytes_ = 0;
};

} // namespace mvpview
