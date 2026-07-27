#include "io/image_loader.h"

#include "io/directory_scanner.h"
#include "io/thumbnail_disk_cache.h"

#include <QFileInfo>
#include <QImageReader>
#include <QMetaObject>
#include <QPointer>
#include <QThread>
#include <QThreadPool>

namespace ispview {
namespace {

int priorityFor(const RequestOptions& options) {
    int base = 0;
    switch (options.category) {
    case LoadCategory::Interactive: base = 100; break;
    case LoadCategory::VisibleThumbnail: base = 60; break;
    case LoadCategory::NearViewport: base = 20; break;
    case LoadCategory::Metadata: base = -20; break;
    case LoadCategory::Background: base = -60; break;
    }
    return base + options.priorityAdjustment;
}

} // namespace

void LoadHandle::cancel() const {
    if (!state_) return;
    bool expected = false;
    if (state_->cancelled.compare_exchange_strong(expected, true, std::memory_order_relaxed) &&
        state_->activeConsumers) {
        --(*state_->activeConsumers);
    }
}

bool LoadHandle::isCancelled() const {
    return state_ && state_->cancelled.load(std::memory_order_relaxed);
}

ImageLoader::ImageLoader(std::shared_ptr<const IImageDecoder> decoder, QObject* parent)
    : QObject(parent), decoder_(std::move(decoder)),
      diskCache_(std::make_shared<ThumbnailDiskCache>()) {
    Q_ASSERT(decoder_);
    pool_.setMaxThreadCount(qBound(2, QThread::idealThreadCount() - 1, 6));
    pool_.setExpiryTimeout(10'000);
}

LoadHandle ImageLoader::request(quint64 requestId, DecodeRequest request, Callback callback,
                                int priority) {
    return requestImpl(requestId, std::move(request), std::move(callback), priority);
}

LoadHandle ImageLoader::request(quint64 requestId, DecodeRequest request, Callback callback,
                                RequestOptions options) {
    return requestImpl(requestId, std::move(request), std::move(callback), priorityFor(options));
}

LoadHandle ImageLoader::requestImpl(quint64 requestId, DecodeRequest request, Callback callback,
                                    int priority) {
    Q_ASSERT(thread() == QThread::currentThread());
    const QFileInfo sourceInfo(request.path);
    if (!DirectoryScanner::isBrowsableEntry(sourceInfo) || !sourceInfo.isFile()) {
        callback(requestId, {{}, QStringLiteral("File is hidden, unreadable, or unavailable")});
        return {};
    }
    if (!request.rawParameters) {
        request.rawParameters = rawParameters(request.path);
    }
    const QString key = cacheKey(request, decoder_->cacheIdentity());
    if (auto cached = cacheFor(request.purpose).get(key)) {
        callback(requestId, {std::move(cached), {}});
        return {};
    }
    auto state = std::make_shared<LoadHandle::State>();
    if (auto found = inFlight_.find(key); found != inFlight_.end()) {
        state->activeConsumers = found->activeConsumers;
        ++(*found->activeConsumers);
        found->pending.push_back({requestId, std::move(callback), state});
        return LoadHandle(state);
    }
    auto activeConsumers = std::make_shared<std::atomic_int>(1);
    state->activeConsumers = activeConsumers;
    InFlightRequest inFlight;
    inFlight.activeConsumers = activeConsumers;
    inFlight.pending.push_back({requestId, std::move(callback), state});
    inFlight_.insert(key, std::move(inFlight));

    const QPointer<ImageLoader> self(this);
    const auto decoder = decoder_;
    const auto diskCache = diskCache_;
    pool_.start(
        [self, decoder, diskCache, request = std::move(request), key, activeConsumers] {
            DecodeResult result;
            if (activeConsumers->load(std::memory_order_relaxed) > 0 &&
                request.purpose == DecodePurpose::Thumbnail) {
                QImage cachedImage = diskCache->load(key);
                if (!cachedImage.isNull()) {
                    const QFileInfo info(request.path);
                    auto frame = std::make_shared<ImageFrame>();
                    frame->descriptor.size = cachedImage.size();
                    frame->metadata.path = info.absoluteFilePath();
                    frame->metadata.fileName = info.fileName();
                    frame->metadata.format = info.suffix().toUpper();
                    frame->metadata.fileSize = info.size();
                    frame->metadata.modifiedAt = info.lastModified();
                    if (request.rawParameters && request.rawParameters->size.isValid()) {
                        frame->metadata.sourceSize = request.rawParameters->size;
                    } else {
                        const QSize cachedSourceSize(
                            cachedImage.text(QStringLiteral("ispview.sourceWidth")).toInt(),
                            cachedImage.text(QStringLiteral("ispview.sourceHeight")).toInt());
                        if (cachedSourceSize.isValid()) {
                            frame->metadata.sourceSize = cachedSourceSize;
                        }
                        // The disk cache stores only preview pixels. Probe the original header on
                        // this worker thread so legacy cache entries still report source dimensions
                        // instead of the cached 160×120 bitmap size.
                        if (!frame->metadata.sourceSize.isValid()) {
                            QImageReader sourceReader(request.path);
                            frame->metadata.sourceSize = sourceReader.size();
                        }
                    }
                    if (frame->metadata.sourceSize.isValid()) {
                        frame->storage = std::move(cachedImage);
                        result.frame = std::move(frame);
                    }
                    // Formats such as DNG are not understood by QImageReader. When the source
                    // size is unavailable, leave the cache result empty so the decoder rebuilds
                    // the frame with its source metadata and RAW parameters.
                }
            }
            if (!result.frame && activeConsumers->load(std::memory_order_relaxed) > 0) {
                result = decoder->decode(request);
                if (result.frame && request.purpose == DecodePurpose::Thumbnail &&
                    activeConsumers->load(std::memory_order_relaxed) > 0) {
                    if (const QImage* image = result.frame->qImage()) {
                        const QSize sourceSize = result.frame->metadata.sourceSize.isValid()
                                                     ? result.frame->metadata.sourceSize
                                                     : result.frame->descriptor.size;
                        const QImage cacheImage = *image;
                        QThreadPool::globalInstance()->start(
                            [diskCache, key, cacheImage, sourceSize] {
                                (void)diskCache->store(key, cacheImage, sourceSize);
                            },
                            -80);
                    }
                }
            }
            if (!self) {
                return;
            }
            QMetaObject::invokeMethod(
                self,
                [self, result = std::move(result), key, purpose = request.purpose,
                 sourcePath = request.path, activeConsumers]() {
                    if (!self) {
                        return;
                    }
                    if (result.frame && activeConsumers->load(std::memory_order_relaxed) > 0) {
                        self->cacheFor(purpose).put(key, result.frame, result.frame->byteSize());
                        if (purpose == DecodePurpose::Thumbnail) {
                            const QSize sourceSize = result.frame->metadata.sourceSize.isValid()
                                                         ? result.frame->metadata.sourceSize
                                                         : result.frame->descriptor.size;
                            emit self->thumbnailMetadataReady(sourcePath, sourceSize);
                        }
                    }
                    const InFlightRequest inFlight = self->inFlight_.take(key);
                    for (const PendingRequest& request : inFlight.pending) {
                        if (request.state->cancelled.load(std::memory_order_relaxed)) continue;
                        request.callback(request.requestId, result);
                        if (!self) {
                            break;
                        }
                    }
                },
                Qt::QueuedConnection);
        },
        priority);
    return LoadHandle(state);
}

void ImageLoader::prefetchAdjacentRawFrames(const QString& path, const RawImageParameters& current,
                                            const QSize& previewSize) {
    const int frameCount = availableFrameCount(QFileInfo(path).size(), current);
    const int adjacentCount =
        (current.frameIndex > 0 ? 1 : 0) + (current.frameIndex + 1 < frameCount ? 1 : 0);
    const qsizetype estimatedFrameCost = estimatedFullFrameBytes(current);
    // Full frames have their own budget. Prefetch only when current and adjacent frames can
    // coexist without displacing the active frame immediately.
    const qsizetype fullPrefetchBudget = fullCache_.maximumCost();
    const bool prefetchFull = adjacentCount > 0 && estimatedFrameCost > 0 &&
                              estimatedFrameCost <= fullPrefetchBudget / (adjacentCount + 1);
    for (const int delta : {-1, 1}) {
        RawImageParameters adjacent = current;
        adjacent.frameIndex += delta;
        if (adjacent.frameIndex < 0 || adjacent.frameIndex >= frameCount) {
            continue;
        }
        request(
            0, {path, DecodePurpose::Preview, previewSize, adjacent},
            [](quint64, const DecodeResult&) {},
            RequestOptions{LoadCategory::NearViewport, 0, QStringLiteral("raw-prefetch")});
        if (prefetchFull) {
            request(
                0, {path, DecodePurpose::Full, {}, adjacent}, [](quint64, const DecodeResult&) {},
                RequestOptions{LoadCategory::Background, 0,
                               QStringLiteral("raw-full-prefetch")});
        }
    }
}

void ImageLoader::clearCache() {
    thumbnailCache_.clear();
    clearTransientCaches();
}

void ImageLoader::clearTransientCaches() {
    Q_ASSERT(thread() == QThread::currentThread());
    previewCache_.clear();
    fullCache_.clear();
}

void ImageLoader::setRawParameters(const QString& path, const RawImageParameters& parameters) {
    const QString normalized = QFileInfo(path).absoluteFilePath();
    {
        QWriteLocker lock(&rawParametersLock_);
        const auto existing = rawParameters_.constFind(normalized);
        if (existing != rawParameters_.cend() &&
            existing->cacheKey() == parameters.cacheKey()) {
            return;
        }
        rawParameters_.insert(normalized, parameters);
    }
    emit rawParametersChanged(normalized);
}

std::optional<RawImageParameters> ImageLoader::rawParameters(const QString& path) const {
    const QReadLocker lock(&rawParametersLock_);
    const auto found = rawParameters_.constFind(QFileInfo(path).absoluteFilePath());
    return found == rawParameters_.cend() ? std::nullopt
                                          : std::optional<RawImageParameters>(*found);
}

bool ImageLoader::isCached(DecodeRequest request) const {
    Q_ASSERT(thread() == QThread::currentThread());
    if (!request.rawParameters) {
        request.rawParameters = rawParameters(request.path);
    }
    return cacheFor(request.purpose).contains(cacheKey(request, decoder_->cacheIdentity()));
}

WeightedLruCache<ImageFrame>& ImageLoader::cacheFor(DecodePurpose purpose) {
    switch (purpose) {
    case DecodePurpose::Thumbnail: return thumbnailCache_;
    case DecodePurpose::Preview: return previewCache_;
    case DecodePurpose::Full: return fullCache_;
    }
    return previewCache_;
}

const WeightedLruCache<ImageFrame>& ImageLoader::cacheFor(DecodePurpose purpose) const {
    switch (purpose) {
    case DecodePurpose::Thumbnail: return thumbnailCache_;
    case DecodePurpose::Preview: return previewCache_;
    case DecodePurpose::Full: return fullCache_;
    }
    return previewCache_;
}

QString ImageLoader::cacheKey(const DecodeRequest& request, const QString& decoderIdentity) {
    const QFileInfo info(request.path);
    return QStringLiteral("%1|%2|%3|%4x%5|%6|%7|%8")
        .arg(info.absoluteFilePath())
        .arg(info.size())
        .arg(info.lastModified().toMSecsSinceEpoch())
        .arg(request.maximumSize.width())
        .arg(request.maximumSize.height())
        .arg(static_cast<int>(request.purpose))
        .arg(request.rawParameters ? request.rawParameters->cacheKey() : QStringLiteral("encoded"))
        .arg(decoderIdentity);
}

} // namespace ispview
