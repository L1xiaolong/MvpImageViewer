#pragma once

#include <QMutex>
#include <QMutexLocker>
#include <QWaitCondition>
#include <functional>
#include <memory>

namespace mvpview {

// Bounds decoded results awaiting GUI submission, including queued invokeMethod
// deliveries. Workers wait; the GUI only releases reservations. One oversized
// result can enter an empty budget so explicit full-resolution requests still work.
class ResultBufferBudget final : public std::enable_shared_from_this<ResultBufferBudget> {
public:
    struct Snapshot {
        qsizetype bytes = 0;
        qsizetype peakBytes = 0;
        int waiters = 0;
    };
    class Reservation final {
    public:
        Reservation(const Reservation&) = delete;
        Reservation& operator=(const Reservation&) = delete;
        ~Reservation() { owner_->release(bytes_); }
    private:
        friend class ResultBufferBudget;
        Reservation(std::shared_ptr<ResultBufferBudget> owner, qsizetype bytes)
            : owner_(std::move(owner)), bytes_(bytes) {}
        std::shared_ptr<ResultBufferBudget> owner_;
        qsizetype bytes_;
    };

    explicit ResultBufferBudget(qsizetype maximumBytes) : maximumBytes_(qMax<qsizetype>(1, maximumBytes)) {}
    std::shared_ptr<Reservation> reserve(qsizetype bytes, const std::function<bool()>& cancelled) {
        bytes = qMax<qsizetype>(0, bytes);
        QMutexLocker lock(&mutex_);
        while (!closed_ && !cancelled() && bytes_ > 0 &&
               (bytes_ >= maximumBytes_ || bytes > maximumBytes_ - bytes_)) {
            ++waiters_;
            changed_.wait(&mutex_, 25);
            --waiters_;
        }
        if (closed_ || cancelled()) return {};
        bytes_ += bytes;
        peakBytes_ = qMax(peakBytes_, bytes_);
        return std::shared_ptr<Reservation>(new Reservation(shared_from_this(), bytes));
    }
    Snapshot snapshot() const {
        QMutexLocker lock(&mutex_);
        return {bytes_, peakBytes_, waiters_};
    }
    void close() {
        QMutexLocker lock(&mutex_);
        closed_ = true;
        changed_.wakeAll();
    }
private:
    void release(qsizetype bytes) {
        QMutexLocker lock(&mutex_);
        bytes_ -= bytes;
        changed_.wakeAll();
    }
    const qsizetype maximumBytes_;
    mutable QMutex mutex_;
    QWaitCondition changed_;
    qsizetype bytes_ = 0;
    qsizetype peakBytes_ = 0;
    int waiters_ = 0;
    bool closed_ = false;
};

} // namespace mvpview
