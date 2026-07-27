#pragma once

#include "core/weighted_lru_cache.h"
#include "io/image_decoder.h"

#include <QObject>
#include <QHash>
#include <QReadWriteLock>
#include <QThreadPool>
#include <QVector>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

namespace ispview {

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

    LoadHandle request(quint64 requestId, DecodeRequest request, Callback callback,
                       int priority = 0);
    LoadHandle request(quint64 requestId, DecodeRequest request, Callback callback,
                       RequestOptions options);
    void prefetchAdjacentRawFrames(const QString& path, const RawImageParameters& current,
                                   const QSize& previewSize);
    void setRawParameters(const QString& path, const RawImageParameters& parameters);
    [[nodiscard]] std::optional<RawImageParameters> rawParameters(const QString& path) const;
    [[nodiscard]] bool isCached(DecodeRequest request) const;
    void clearCache();
    // Releases large preview/full-resolution frames while retaining inexpensive thumbnails.
    void clearTransientCaches();

    [[nodiscard]] static QString cacheKey(const DecodeRequest& request,
                                          const QString& decoderIdentity = {});

  signals:
    void rawParametersChanged(const QString& path);
    void thumbnailMetadataReady(const QString& path, const QSize& sourceSize);

  private:
    struct PendingRequest {
        quint64 requestId = 0;
        Callback callback;
        std::shared_ptr<LoadHandle::State> state;
    };

    struct InFlightRequest {
        QVector<PendingRequest> pending;
        std::shared_ptr<std::atomic_int> activeConsumers;
    };

    [[nodiscard]] LoadHandle requestImpl(quint64 requestId, DecodeRequest request,
                                         Callback callback, int priority);
    [[nodiscard]] WeightedLruCache<ImageFrame>& cacheFor(DecodePurpose purpose);
    [[nodiscard]] const WeightedLruCache<ImageFrame>& cacheFor(DecodePurpose purpose) const;

    std::shared_ptr<const IImageDecoder> decoder_;
    std::shared_ptr<ThumbnailDiskCache> diskCache_;
    WeightedLruCache<ImageFrame> thumbnailCache_{96LL * 1024 * 1024};
    WeightedLruCache<ImageFrame> previewCache_{128LL * 1024 * 1024};
    WeightedLruCache<ImageFrame> fullCache_{288LL * 1024 * 1024};
    QThreadPool pool_;
    mutable QReadWriteLock rawParametersLock_;
    QHash<QString, RawImageParameters> rawParameters_;
    QHash<QString, InFlightRequest> inFlight_;
};

} // namespace ispview
