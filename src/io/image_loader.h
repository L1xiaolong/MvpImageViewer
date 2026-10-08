#pragma once

#include "core/weighted_lru_cache.h"
#include "io/image_decoder.h"

#include <QObject>
#include <QHash>
#include <QReadWriteLock>
#include <QThreadPool>
#include <QVector>
#include <QVariantMap>
#include <QTimer>
#include <QElapsedTimer>

#include <deque>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>

namespace mvpview {

class ThumbnailDiskCache;

enum class LoadCategory { Interactive, VisibleThumbnail, NearViewport, Metadata, Background };

struct RequestOptions {
    LoadCategory category = LoadCategory::Interactive;
    int priorityAdjustment = 0;
    QString caller;
};

class LoadHandle final {
  public:
    LoadHandle() = default;
    void cancel() const;
    [[nodiscard]] bool isCancelled() const;
    [[nodiscard]] explicit operator bool() const { return state_ != nullptr; }

  private:
    struct State {
        std::atomic_bool cancelled{false};
        std::shared_ptr<std::atomic_int> activeConsumers;
    };
    explicit LoadHandle(std::shared_ptr<State> state) : state_(std::move(state)) {}
    std::shared_ptr<State> state_;
    friend class ImageLoader;
};

class ImageLoader final : public QObject {
    Q_OBJECT

  public:
    using Callback = std::function<void(quint64, const DecodeResult&)>;

    explicit ImageLoader(std::shared_ptr<const IImageDecoder> decoder, QObject* parent = nullptr);

    ~ImageLoader() override;

    LoadHandle request(quint64 requestId, DecodeRequest request, Callback callback,
                       int priority = 0);
    LoadHandle request(quint64 requestId, DecodeRequest request, Callback callback,
                       RequestOptions options);
    void prefetchAdjacentImages(const QStringList& paths, int index, const QSize& previewSize);
    void prefetchAdjacentRawFrames(const QString& path, const RawImageParameters& current,
                                   const QSize& previewSize);
    void setRawParameters(const QString& path, const RawImageParameters& parameters);
    [[nodiscard]] std::optional<RawImageParameters> rawParameters(const QString& path) const;
    [[nodiscard]] bool isCached(DecodeRequest request) const;
    [[nodiscard]] qsizetype cachedBytes() const;
    [[nodiscard]] qsizetype memoryBudget() const { return memoryBudget_; }
    void setMemoryBudget(qsizetype bytes);
    [[nodiscard]] bool
    canAutomaticallyLoadFull(const QVector<ImageFramePtr>& previewFrames) const;
    using AnalysisWork = std::function<QVariantMap(const std::function<bool()>&)>;
    LoadHandle requestAnalysis(AnalysisWork work, std::function<void(QVariantMap)> callback);
    LoadHandle requestMetadata(quint64 requestId, ImageFramePtr frame, Callback callback);
    void updateViewport(const QString& owner, const QHash<QString, int>& priorities, bool fast);
    [[nodiscard]] bool hasInteractiveWork() const;
    [[nodiscard]] bool fastScrolling() const;
    void clearCache();
    // Releases large preview/full-resolution frames while retaining inexpensive thumbnails.
    void clearTransientCaches();

    [[nodiscard]] static QString cacheKey(const DecodeRequest& request,
                                          const QString& decoderIdentity = {});

  signals:
    void rawParametersChanged(const QString& path);
    void thumbnailMetadataReady(const QString& path, const QSize& sourceSize, int validBits);

  private:
    struct PendingRequest {
        quint64 requestId = 0;
        Callback callback;
        std::shared_ptr<LoadHandle::State> state;
    };

    struct InFlightRequest {
        QVector<PendingRequest> pending;
        std::shared_ptr<std::atomic_int> activeConsumers;
        quint64 generation = 0;
        QString path;
        int priority = 0;
        bool serialized = false;
        bool running = false;
        DecodePurpose purpose = DecodePurpose::Preview;
        QElapsedTimer queuedAt;
        std::function<void()> work;
    };

    [[nodiscard]] LoadHandle requestImpl(quint64 requestId, DecodeRequest request,
                                         Callback callback, int priority);
    [[nodiscard]] WeightedLruCache<ImageFrame>& cacheFor(DecodePurpose purpose);
    [[nodiscard]] const WeightedLruCache<ImageFrame>& cacheFor(DecodePurpose purpose) const;
    void enforceMemoryBudget(DecodePurpose insertedPurpose);
    [[nodiscard]] static qsizetype estimatedFullFrameCost(const ImageFrame& preview);

    void dispatch();
    int viewportPriority(const QString& path) const;
    struct Viewport { QHash<QString, int> priorities; bool fast = false; };
    QHash<QString, Viewport> viewports_;
    QHash<QString, quint64> ownerService_;
    quint64 serviceSequence_ = 0;
    QVector<LoadHandle> rawPrefetchHandles_;
    QVector<LoadHandle> imagePrefetchHandles_;
    QTimer dispatchTimer_;
    std::deque<std::function<void()>> completions_;
    QThreadPool writePool_;
    std::shared_ptr<std::atomic_int> pendingWrites_ = std::make_shared<std::atomic_int>(0);
    int parallelRunning_ = 0;
    int serializedRunning_ = 0;
    std::shared_ptr<SourceFrameCache> sourceCache_ = std::make_shared<SourceFrameCache>();
    std::shared_ptr<const IImageDecoder> decoder_;
    std::shared_ptr<ThumbnailDiskCache> diskCache_;
    static constexpr qsizetype kDefaultMemoryBudget = 384LL * 1024 * 1024;
    WeightedLruCache<ImageFrame> thumbnailCache_{kDefaultMemoryBudget};
    WeightedLruCache<ImageFrame> previewCache_{kDefaultMemoryBudget};
    WeightedLruCache<ImageFrame> fullCache_{kDefaultMemoryBudget};
    qsizetype memoryBudget_ = kDefaultMemoryBudget;
    qsizetype automaticFullLoadBudget_ = 256LL * 1024 * 1024;
    QThreadPool pool_;
    QThreadPool serializedPool_;
    mutable QReadWriteLock rawParametersLock_;
    QHash<QString, RawImageParameters> rawParameters_;
    QHash<QString, InFlightRequest> inFlight_;
    quint64 nextInFlightGeneration_ = 0;
};

} // namespace mvpview
