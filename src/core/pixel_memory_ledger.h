#pragma once

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <memory>

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
class PixelMemoryLedger final : public std::enable_shared_from_this<PixelMemoryLedger> {
public:
    class Lease final {
    public:
        ~Lease() { ledger_->adjust(footprint_, false); }
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
    private:
        friend class PixelMemoryLedger;
        Lease(std::shared_ptr<PixelMemoryLedger> ledger, PixelStorageFootprint footprint)
            : ledger_(std::move(ledger)), footprint_(std::move(footprint)) { ledger_->adjust(footprint_, true); }
        std::shared_ptr<PixelMemoryLedger> ledger_;
        PixelStorageFootprint footprint_;
    };
    std::shared_ptr<Lease> retain(PixelStorageFootprint footprint) {
        return std::shared_ptr<Lease>(new Lease(shared_from_this(), std::move(footprint)));
    }
    void adjust(const PixelStorageFootprint& footprint, bool added) {
        QMutexLocker lock(&mutex_);
        for (auto it = footprint.allocations().cbegin(); it != footprint.allocations().cend(); ++it) {
            if (added) {
                auto& allocation = allocations_[it.key()];
                ++allocation.sizes[it.value()];
                if (it.value() > allocation.maximum) {
                    bytes_ += it.value() - allocation.maximum;
                    allocation.maximum = it.value();
                    peakBytes_ = qMax(peakBytes_, bytes_);
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
    qsizetype peakBytes() const { QMutexLocker lock(&mutex_); return peakBytes_; }
private:
    struct Allocation { QHash<qsizetype, int> sizes; qsizetype maximum = 0; };
    mutable QMutex mutex_;
    QHash<const void*, Allocation> allocations_;
    qsizetype bytes_ = 0;
    qsizetype peakBytes_ = 0;
};

// A copied frame may replace its pixel representation. Register that new frame
// after preparation instead of inheriting a token for potentially old pixels.
class PixelMemoryOwnership final {
public:
    PixelMemoryOwnership() = default;
    PixelMemoryOwnership(const PixelMemoryOwnership&) {}
    PixelMemoryOwnership& operator=(const PixelMemoryOwnership& other) {
        if (this != &other) lease_.reset();
        return *this;
    }
    PixelMemoryOwnership(PixelMemoryOwnership&&) noexcept = default;
    PixelMemoryOwnership& operator=(PixelMemoryOwnership&&) noexcept = default;
    void attach(const std::shared_ptr<PixelMemoryLedger>& ledger, PixelStorageFootprint footprint) {
        lease_ = ledger->retain(std::move(footprint));
    }
private:
    std::shared_ptr<PixelMemoryLedger::Lease> lease_;
};

} // namespace mvpview
