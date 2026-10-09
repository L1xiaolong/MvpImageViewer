#include "io/image_loader.h"

#include "io/directory_scanner.h"
#include "diagnostics/diagnostics.h"
#include "core/performance_trace.h"
#include "io/metadata_reader.h"
#include "io/thumbnail_disk_cache.h"

#include <QFileInfo>
#include <QImageReader>
#include <QMetaObject>
#include <QPointer>
#include <QThread>
#include <QThreadPool>

#include <limits>

namespace mvpview {
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

QString cacheKeyPrefix(const DecodeRequest& request, const QFileInfo& info) {
    return info.absoluteFilePath() + QLatin1Char('|') + QString::number(info.size()) +
        QLatin1Char('|') + QString::number(info.lastModified().toMSecsSinceEpoch()) +
        QLatin1Char('|') + QString::number(request.maximumSize.width()) + QLatin1Char('x') +
        QString::number(request.maximumSize.height()) + QLatin1Char('|') +
        QString::number(static_cast<int>(request.purpose) + (request.metadataSource ? 100 : 0));
}

} // namespace

void LoadHandle::cancel() const {
    if (!state_) return;
    bool expected = false;
    if (state_->cancelled.compare_exchange_strong(expected, true, std::memory_order_relaxed) &&
        state_->activeConsumers) {
        int consumers = state_->activeConsumers->load(std::memory_order_relaxed);
        while (consumers > 0 &&
               !state_->activeConsumers->compare_exchange_weak(
                   consumers, consumers == 1 ? -1 : consumers - 1, std::memory_order_relaxed)) {
        }
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
    serializedPool_.setMaxThreadCount(1);
    serializedPool_.setExpiryTimeout(10'000);
    writePool_.setMaxThreadCount(1);
    dispatchTimer_.setInterval(16);
    connect(&dispatchTimer_, &QTimer::timeout, this, &ImageLoader::dispatch);
    dispatchWake_.setSingleShot(true);
    dispatchWake_.setInterval(0);
    connect(&dispatchWake_, &QTimer::timeout, this, &ImageLoader::dispatch);
}

ImageLoader::~ImageLoader() {
    dispatchTimer_.stop();
    dispatchWake_.stop();
    for (auto& job : inFlight_) job.activeConsumers->store(-1);
    pool_.waitForDone();
    serializedPool_.waitForDone();
    writePool_.waitForDone();
}

LoadHandle ImageLoader::request(quint64 requestId, DecodeRequest request, Callback callback,
                                int priority) {
    return requestImpl(requestId, std::move(request), std::move(callback), priority);
}

LoadHandle ImageLoader::request(quint64 requestId, DecodeRequest request, Callback callback,
                                RequestOptions options) {
    return requestImpl(requestId, std::move(request), std::move(callback), priorityFor(options));
}

LoadHandle ImageLoader::requestAnalysis(AnalysisWork work, std::function<void(QVariantMap)> callback) {
    if (inFlight_.size() >= 512) { callback({}); return {}; }
    auto state = std::make_shared<LoadHandle::State>();
    state->activeConsumers = std::make_shared<std::atomic_int>(1);
    const auto consumers = state->activeConsumers;
    const QString key = QStringLiteral("analysis:%1").arg(++nextInFlightGeneration_);
    InFlightRequest job; job.priority = -20; job.generation = nextInFlightGeneration_; job.queuedAt.start();
    job.activeConsumers = consumers;
    const QPointer<ImageLoader> self(this);
    job.work = [self, consumers, key, work = std::move(work), callback = std::move(callback)] {
        const auto cancelled = [consumers] { return consumers->load() <= 0; };
        QVariantMap result = cancelled() ? QVariantMap{} : work(cancelled);
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, consumers, key, result = std::move(result), callback] {
            if (!self) return;
            --self->parallelRunning_;
            self->completions_.push_back([self, consumers, key, result, callback] {
                if (!self) return;
                self->inFlight_.remove(key);
                if (consumers->load() > 0) callback(result);
            });
            self->scheduleDispatch();
        }, Qt::QueuedConnection);
    };
    inFlight_.insert(key, std::move(job));
    scheduleDispatch();
    return LoadHandle(state);
}

LoadHandle ImageLoader::requestMetadata(quint64 id, ImageFramePtr frame, Callback callback) {
    if (!frame) return {};
    DecodeRequest request{frame->metadata.path, DecodePurpose::Full};
    request.metadataSource = std::move(frame);
    return this->request(id, std::move(request), std::move(callback),
                         RequestOptions{LoadCategory::Metadata, 0, QStringLiteral("metadata")});
}

LoadHandle ImageLoader::requestImpl(quint64 requestId, DecodeRequest request, Callback callback,
                                    int priority) {
    Q_ASSERT(thread() == QThread::currentThread());
    if (request.purpose == DecodePurpose::Preview && request.maximumSize.isValid()) {
        const int edge = std::max(request.maximumSize.width(), request.maximumSize.height());
        for (const int bucket : {512, 1024, 1536, 2048, 2560}) {
            if (edge <= bucket) { request.maximumSize = QSize(bucket, bucket); break; }
        }
    }
    if (request.purpose == DecodePurpose::Thumbnail) {
        const int demand = viewportPriority(request.path);
        if (demand > -1000) priority = demand;
    }
    const QFileInfo sourceInfo(request.path);
    if (!DirectoryScanner::isBrowsableEntry(sourceInfo) || !sourceInfo.isFile()) {
        callback(requestId, {{}, QStringLiteral("File is hidden, unreadable, or unavailable")});
        return {};
    }
    if (!request.rawParameters) {
        request.rawParameters = rawParameters(request.path);
    }
    const QString keyPrefix = cacheKeyPrefix(request, sourceInfo);
    const QString decoderIdentity = decoder_->cacheIdentity();
    const QString key = keyPrefix + QLatin1Char('|') +
        (request.rawParameters ? request.rawParameters->cacheKey() : QStringLiteral("encoded")) +
        QLatin1Char('|') + decoderIdentity;
    ImageFramePtr reusable;
    if (request.purpose == DecodePurpose::Preview) {
        DecodeRequest candidate = request; candidate.purpose = DecodePurpose::Full; candidate.maximumSize = {};
        reusable = fullCache_.get(cacheKey(candidate, decoder_->cacheIdentity()));
        if (!reusable) for (const int bucket : {512, 1024, 1536, 2048, 2560}) {
            if (bucket < request.maximumSize.width()) continue;
            candidate = request; candidate.maximumSize = QSize(bucket, bucket);
            if ((reusable = previewCache_.get(cacheKey(candidate, decoder_->cacheIdentity())))) break;
        }
    }
    if (!reusable && request.purpose == DecodePurpose::Thumbnail) {
        for (const int bucket : {128, 256, 384, 512}) {
            if (bucket < std::max(request.maximumSize.width(), request.maximumSize.height())) continue;
            auto candidate = request; candidate.maximumSize = QSize(bucket, bucket);
            if ((reusable = thumbnailCache_.get(cacheKey(candidate, decoder_->cacheIdentity())))) break;
        }
    }
    if (auto cached = reusable ? reusable : cacheFor(request.purpose).get(key)) {
        performance::mark(QStringLiteral("loader.memory_hit"), {{"purpose", int(request.purpose)}});
        if (request.purpose == DecodePurpose::Thumbnail) {
            const QSize sourceSize = cached->metadata.sourceSize.isValid()
                                         ? cached->metadata.sourceSize
                                         : cached->descriptor.size;
            const int validBits = cached->rawParameters
                                      ? cached->rawParameters->validBits()
                                      : cached->descriptor.validBits;
            emit thumbnailMetadataReady(request.path, sourceSize, validBits);
        }
        callback(requestId, {std::move(cached), {}});
        return {};
    }
    if (inFlight_.size() >= 512 && priority < 60) {
        callback(requestId, {{}, QStringLiteral("Background queue is full")});
        return {};
    }
    auto state = std::make_shared<LoadHandle::State>();
    if (auto found = inFlight_.find(key); found != inFlight_.end()) {
        int consumers = found->activeConsumers->load(std::memory_order_relaxed);
        while (consumers >= 0) {
            if (found->activeConsumers->compare_exchange_weak(
                    consumers, consumers + 1, std::memory_order_relaxed)) {
                state->activeConsumers = found->activeConsumers;
                found->pending.push_back({requestId, std::move(callback), state});
                found->priority = std::max(found->priority, priority);
                scheduleDispatch();
                return LoadHandle(state);
            }
        }
        // The worker already committed to skipping an unobserved request. Replace that
        // generation instead of attaching a new consumer to a result that will be empty.
        inFlight_.erase(found);
    }
    auto activeConsumers = std::make_shared<std::atomic_int>(1);
    state->activeConsumers = activeConsumers;
    InFlightRequest inFlight;
    inFlight.activeConsumers = activeConsumers;
    inFlight.generation = ++nextInFlightGeneration_;
    const quint64 generation = inFlight.generation;
    inFlight.path = request.path;
    inFlight.priority = priority;
    inFlight.purpose = request.purpose;
    inFlight.serialized = !request.metadataSource && decoder_->executionMode(request.path) == DecodeExecutionMode::Serialized;
    inFlight.queuedAt.start();
    request.activeConsumers = activeConsumers;
    request.sourceCache = sourceCache_;
    inFlight.pending.push_back({requestId, std::move(callback), state});
    inFlight_.insert(key, std::move(inFlight));

    const QPointer<ImageLoader> self(this);
    const auto decoder = decoder_;
    const auto diskCache = diskCache_;
    const auto pendingWrites = pendingWrites_;
    inFlight_[key].work =
        [self, decoder, diskCache, request = std::move(request), key, keyPrefix, decoderIdentity, generation,
         activeConsumers, pendingWrites] {
            DecodeResult result;
            const bool abandoned = request.isCancelled();
            if (!abandoned && request.purpose == DecodePurpose::Thumbnail) {
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
                    frame->descriptor.validBits = request.rawParameters
                                                      ? request.rawParameters->validBits()
                                                      : cachedImage
                                                            .text(QStringLiteral(
                                                                "mvpview.validBits"))
                                                            .toInt();
                    if (frame->descriptor.validBits <= 0) {
                        frame->descriptor.validBits = 8;
                    }
                    if (request.rawParameters && request.rawParameters->size.isValid()) {
                        frame->metadata.sourceSize = request.rawParameters->size;
                    } else {
                        const QSize cachedSourceSize(
                            cachedImage.text(QStringLiteral("mvpview.sourceWidth")).toInt(),
                            cachedImage.text(QStringLiteral("mvpview.sourceHeight")).toInt());
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
            QElapsedTimer decodeTimer;
            decodeTimer.start();
            const bool diskHit = bool(result.frame);
            if (!abandoned && !request.isCancelled() && !result.frame) {
                if (request.metadataSource) {
                    auto frame = std::make_shared<ImageFrame>(*request.metadataSource);
                    MetadataReader::enrich(request.path, frame->metadata);
                    result.frame = std::move(frame);
                } else result = decoder->decode(request);
                if (result.frame && request.purpose == DecodePurpose::Thumbnail &&
                    activeConsumers->load(std::memory_order_relaxed) > 0) {
                    if (const QImage* image = result.frame->qImage()) {
                        const QSize sourceSize = result.frame->metadata.sourceSize.isValid()
                                                     ? result.frame->metadata.sourceSize
                                                     : result.frame->descriptor.size;
                        const int validBits = result.frame->rawParameters
                                                  ? result.frame->rawParameters->validBits()
                                                  : result.frame->descriptor.validBits;
                        const QImage cacheImage = *image;
                        QMetaObject::invokeMethod(self, [self, diskCache, key, cacheImage,
                                                         sourceSize, validBits, pendingWrites] {
                            if (!self || pendingWrites->load() >= 64) return;
                            ++*pendingWrites;
                            self->writePool_.start([diskCache, key, cacheImage, sourceSize,
                                                    validBits, pendingWrites] {
                                (void)diskCache->store(key, cacheImage, sourceSize, validBits);
                                --*pendingWrites;
                            });
                        }, Qt::QueuedConnection);
                    }
                }
            }
            if (result.frame && !request.isCancelled() && request.purpose != DecodePurpose::Thumbnail && !request.metadataSource) {
                auto prepared = std::make_shared<ImageFrame>(*result.frame);
                if (const auto* image = prepared->qImage(); image && image->format() == QImage::Format_RGBA64)
                    prepared->uploadImage = image->convertedTo(QImage::Format_RGBA16FPx4);
                if (prepared->rawParameters && prepared->rawParameters->format == RawPixelFormat::P010 &&
                    !prepared->rawParameters->littleEndian) {
                    if (const auto* planes = std::get_if<std::shared_ptr<const PlaneBufferSet>>(&prepared->storage)) {
                        auto normalized = std::make_shared<PlaneBufferSet>(**planes);
                        normalized->displayImage = {};
                        normalized->storage.detach();
                        for (qsizetype i = 0; i + 1 < normalized->storage.size(); i += 2) {
                            if ((i % (1024 * 1024)) == 0 && request.isCancelled()) break;
                            std::swap(normalized->storage[i], normalized->storage[i + 1]);
                        }
                        prepared->uploadPlanes = std::move(normalized);
                    }
                }
                result.frame = std::move(prepared);
            }
            if (!self) return;
            QMetaObject::invokeMethod(
                self,
                [self, completion = [self, result = std::move(result), key, keyPrefix, decoderIdentity, purpose = request.purpose,
                 sourcePath = request.path, generation, activeConsumers, diskHit, metadataOnly = bool(request.metadataSource),
                 elapsedMs = decodeTimer.elapsed()]() {
                    if (!self) {
                        return;
                    }
                    performance::mark(QStringLiteral("loader.completed"),
                        {{"elapsedMs", elapsedMs}, {"diskHit", diskHit}, {"purpose", int(purpose)},
                         {"cancelled", activeConsumers->load() <= 0}, {"failed", !result.frame && activeConsumers->load() > 0},
                         {"cachedBytes", qint64(self->cachedBytes())}, {"sourceBytes", qint64(self->sourceCache_->cost())}});
                    diagnostics::event(diagnostics::Level::Debug, diagnostics::decode(),
                        QStringLiteral("loader.completed"),
                        {{"elapsedMs", elapsedMs}, {"diskHit", diskHit},
                         {"cancelled", activeConsumers->load() <= 0},
                         {"cachedBytes", qint64(self->cachedBytes())}}, false);
                    const auto currentGeneration = self->inFlight_.constFind(key);
                    if (currentGeneration == self->inFlight_.cend() || currentGeneration->generation != generation) return;
                    if (result.frame && activeConsumers->load(std::memory_order_relaxed) > 0) {
                        QString frameKey = key;
                        if (result.frame->rawParameters &&
                            self->adoptRawParameters(sourcePath, *result.frame->rawParameters)) {
                            // Subsequent requests include the discovered defaults. Store once
                            // under that identity, retaining the original file-version prefix.
                            frameKey = keyPrefix + QLatin1Char('|') + result.frame->rawParameters->cacheKey() +
                                QLatin1Char('|') + decoderIdentity;
                        }
                        if (!metadataOnly) self->cacheFor(purpose).put(frameKey, result.frame, result.frame->byteSize());
                        self->enforceMemoryBudget(purpose);
                        performance::mark(QStringLiteral("loader.cache_state"),
                            {{"cachedBytes", qint64(self->cachedBytes())}, {"sourceBytes", qint64(self->sourceCache_->cost())}});
                        if (purpose == DecodePurpose::Thumbnail) {
                            const QSize sourceSize = result.frame->metadata.sourceSize.isValid()
                                                         ? result.frame->metadata.sourceSize
                                                         : result.frame->descriptor.size;
                            const int validBits = result.frame->rawParameters
                                                      ? result.frame->rawParameters->validBits()
                                                      : result.frame->descriptor.validBits;
                            emit self->thumbnailMetadataReady(sourcePath, sourceSize, validBits);
                        }
                    }
                    const auto current = self->inFlight_.constFind(key);
                    if (current == self->inFlight_.cend() ||
                        current->generation != generation) {
                        return;
                    }
                    const InFlightRequest inFlight = self->inFlight_.take(key);
                    for (const PendingRequest& request : inFlight.pending) {
                        if (request.state->cancelled.load(std::memory_order_relaxed)) continue;
                        request.callback(request.requestId, result);
                        if (!self) {
                            break;
                        }
                    }
                }, serialized = !request.metadataSource && decoder->executionMode(request.path) == DecodeExecutionMode::Serialized]() mutable {
                    if (!self) return;
                    if (serialized) --self->serializedRunning_;
                    else --self->parallelRunning_;
                    self->completions_.push_back(std::move(completion));
                    self->scheduleDispatch();
                },
                Qt::QueuedConnection);
        };
    scheduleDispatch();
    return LoadHandle(state);
}

int ImageLoader::viewportPriority(const QString& path) const {
    int priority = -1000;
    for (const auto& viewport : viewports_)
        priority = std::max(priority, viewport.priorities.value(path, -1000));
    return priority;
}

void ImageLoader::updateViewport(const QString& owner, const QHash<QString, int>& priorities,
                                 bool fast) {
    const auto previous = viewports_.constFind(owner);
    if (previous == viewports_.cend() || previous->priorities != priorities || previous->fast != fast) {
        int visible = 0;
        for (int priority : priorities) if (priority >= 60) ++visible;
        performance::mark(QStringLiteral("viewport.updated"),
            {{"owner", owner}, {"visible", visible}, {"candidates", priorities.size()}, {"fast", fast}});
    }
    if (priorities.isEmpty()) { viewports_.remove(owner); ownerService_.remove(owner); }
    else viewports_.insert(owner, {priorities, fast});
    if (fast) {
        for (const auto& handle : imagePrefetchHandles_) handle.cancel();
        for (const auto& handle : rawPrefetchHandles_) handle.cancel();
    }
    for (auto it = inFlight_.begin(); it != inFlight_.end(); ++it)
        if (it->purpose == DecodePurpose::Thumbnail)
            it->priority = viewportPriority(it->path);
    scheduleDispatch();
}

bool ImageLoader::fastScrolling() const {
    for (const auto& viewport : viewports_) if (viewport.fast) return true;
    return false;
}

bool ImageLoader::hasInteractiveWork() const {
    for (const auto& job : inFlight_)
        if (job.priority >= 60 && job.activeConsumers->load() > 0) return true;
    return false;
}

void ImageLoader::scheduleDispatch() {
    if (!dispatchTimer_.isActive()) dispatchTimer_.start();
    // New demand preempts a pending completion deadline without waiting a frame.
    if (!dispatchWake_.isActive() || dispatchWake_.remainingTime() > 0) dispatchWake_.start(0);
}

void ImageLoader::dispatch() {
    const QPointer<ImageLoader> alive(this);
    const bool commitNow = !completionClock_.isValid() || completionClock_.elapsed() >= 16;
    if (commitNow && !completions_.empty()) completionClock_.start();
    QElapsedTimer completionBudget; completionBudget.start();
    while (commitNow && !completions_.empty() && completionBudget.elapsed() < 4) {
        auto completion = std::move(completions_.front()); completions_.pop_front();
        completion();
        if (!alive) return;
    }
    // Keep QThreadPool's own queue empty: queued jobs remain mutable and cancellable here.
    for (auto it = inFlight_.begin(); it != inFlight_.end();) {
        if (!it->running && it->activeConsumers->load() <= 0) it = inFlight_.erase(it);
        else ++it;
    }
    for (;;) {
        // Reserve a result slot before starting work. A fast decoder must not grow
        // pixel buffers without bound while GUI completion commits are time-sliced.
        constexpr qsizetype resultCapacity = 64;
        if (qsizetype(completions_.size()) + parallelRunning_ + serializedRunning_ >= resultCapacity) break;
        auto best = inFlight_.end();
        auto ownerFor = [this](const QString& path) {
            QString owner;
            for (auto viewport = viewports_.cbegin(); viewport != viewports_.cend(); ++viewport)
                if (viewport->priorities.contains(path) &&
                    (owner.isEmpty() || ownerService_.value(viewport.key()) < ownerService_.value(owner)))
                    owner = viewport.key();
            return owner;
        };
        for (auto it = inFlight_.begin(); it != inFlight_.end(); ++it) {
            if (it->purpose == DecodePurpose::Thumbnail && it->priority <= -1000) continue;
            if (it->running || !it->work || it->activeConsumers->load() <= 0) continue;
            if (it->serialized ? serializedRunning_ >= 1
                               : parallelRunning_ >= pool_.maxThreadCount()) continue;
            if (fastScrolling() && it->priority < 60) continue;
            if (!it->serialized && it->priority < 60 &&
                parallelRunning_ >= pool_.maxThreadCount() - 1) continue;
            const int category = it->priority >= 100 ? 3 : it->priority >= 60 ? 2 : it->priority >= 20 ? 1 : 0;
            const int bestCategory = best == inFlight_.end() ? -1 :
                best->priority >= 100 ? 3 : best->priority >= 60 ? 2 : best->priority >= 20 ? 1 : 0;
            const quint64 served = ownerService_.value(ownerFor(it->path));
            const quint64 bestServed = best == inFlight_.end() ? 0 : ownerService_.value(ownerFor(best->path));
            if (category > bestCategory || (category == bestCategory &&
                (served < bestServed || (served == bestServed &&
                 (it->priority > best->priority || (it->priority == best->priority && it->generation < best->generation)))))) best = it;
        }
        if (best == inFlight_.end()) break;
        ownerService_[ownerFor(best->path)] = ++serviceSequence_;
        best->running = true;
        performance::mark(QStringLiteral("loader.dispatched"),
            {{"queueMs", best->queuedAt.elapsed()}, {"priority", best->priority},
             {"pending", inFlight_.size()},
             {"pendingResults", qint64(completions_.size()) + parallelRunning_ + serializedRunning_ + 1}});
        diagnostics::event(diagnostics::Level::Debug, diagnostics::decode(),
            QStringLiteral("loader.dispatched"),
            {{"queueMs", best->queuedAt.elapsed()}, {"priority", best->priority},
             {"pending", inFlight_.size()}}, false);
        if (best->serialized) { ++serializedRunning_; serializedPool_.start(std::move(best->work)); }
        else { ++parallelRunning_; pool_.start(std::move(best->work)); }
    }
    if (!completions_.empty() && !dispatchWake_.isActive())
        dispatchWake_.start(std::max(1, 16 - int(completionClock_.elapsed())));
    if (inFlight_.isEmpty() && completions_.empty()) dispatchTimer_.stop();
}

void ImageLoader::prefetchAdjacentImages(const QStringList& paths, int index, const QSize& size) {
    for (const auto& handle : imagePrefetchHandles_) handle.cancel();
    imagePrefetchHandles_.clear();
    if (fastScrolling() || hasInteractiveWork() || cachedBytes() > memoryBudget_ * 3 / 4) return;
    for (const int delta : {1, -1}) {
        if (index + delta < 0 || index + delta >= paths.size()) continue;
        imagePrefetchHandles_.append(request(0, {paths.at(index + delta), DecodePurpose::Preview, size},
            [](quint64, const DecodeResult&) {}, RequestOptions{LoadCategory::NearViewport, delta > 0 ? 5 : 0,
                                                                            QStringLiteral("image-prefetch")}));
    }
}

void ImageLoader::prefetchAdjacentRawFrames(const QString& path, const RawImageParameters& current,
                                            const QSize& previewSize) {
    for (const auto& handle : rawPrefetchHandles_) handle.cancel();
    rawPrefetchHandles_.clear();
    if (fastScrolling() || hasInteractiveWork()) return;
    const int frameCount = availableFrameCount(QFileInfo(path).size(), current);
    const int adjacentCount =
        (current.frameIndex > 0 ? 1 : 0) + (current.frameIndex + 1 < frameCount ? 1 : 0);
    const qsizetype estimatedFrameCost = estimatedFullFrameBytes(current);
    // Full frames have their own budget. Prefetch only when current and adjacent frames can
    // coexist without displacing the active frame immediately.
    const qsizetype fullPrefetchBudget = std::min(fullCache_.maximumCost(), memoryBudget_);
    const bool prefetchFull = adjacentCount > 0 && estimatedFrameCost > 0 &&
                              estimatedFrameCost <= fullPrefetchBudget / (adjacentCount + 1);
    for (const int delta : {-1, 1}) {
        RawImageParameters adjacent = current;
        adjacent.frameIndex += delta;
        if (adjacent.frameIndex < 0 || adjacent.frameIndex >= frameCount) {
            continue;
        }
        rawPrefetchHandles_.append(request(
            0, {path, DecodePurpose::Preview, previewSize, adjacent},
            [](quint64, const DecodeResult&) {},
            RequestOptions{LoadCategory::NearViewport, 0, QStringLiteral("raw-prefetch")}));
        if (prefetchFull) {
            rawPrefetchHandles_.append(request(
                0, {path, DecodePurpose::Full, {}, adjacent}, [](quint64, const DecodeResult&) {},
                RequestOptions{LoadCategory::Background, 0,
                               QStringLiteral("raw-full-prefetch")}));
        }
    }
}

void ImageLoader::clearCache() {
    sourceCache_->clear();
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
        rawParameterRevisions_.insert(normalized, parameters.cacheKey());
    }
    emit rawParametersChanged(normalized);
}

bool ImageLoader::adoptRawParameters(const QString& path, const RawImageParameters& parameters) {
    const QString normalized = QFileInfo(path).absoluteFilePath();
    const QWriteLocker lock(&rawParametersLock_);
    if (rawParameters_.contains(normalized)) return false;
    rawParameters_.insert(normalized, parameters);
    return true;
}

QString ImageLoader::rawParametersRevision(const QString& path) const {
    const QReadLocker lock(&rawParametersLock_);
    return rawParameterRevisions_.value(QFileInfo(path).absoluteFilePath());
}

std::optional<RawImageParameters> ImageLoader::rawParameters(const QString& path) const {
    const QReadLocker lock(&rawParametersLock_);
    const auto found = rawParameters_.constFind(QFileInfo(path).absoluteFilePath());
    return found == rawParameters_.cend() ? std::nullopt
                                          : std::optional<RawImageParameters>(*found);
}

bool ImageLoader::isCached(DecodeRequest request) const {
    Q_ASSERT(thread() == QThread::currentThread());
    if (!request.rawParameters) request.rawParameters = rawParameters(request.path);
    if (request.purpose == DecodePurpose::Preview) {
        DecodeRequest candidate = request;
        candidate.purpose = DecodePurpose::Full;
        candidate.maximumSize = {};
        if (fullCache_.contains(cacheKey(candidate, decoder_->cacheIdentity()))) return true;
        const int edge = std::max(request.maximumSize.width(), request.maximumSize.height());
        for (const int bucket : {512, 1024, 1536, 2048, 2560}) {
            if (bucket < edge) continue;
            candidate = request;
            candidate.maximumSize = QSize(bucket, bucket);
            if (previewCache_.contains(cacheKey(candidate, decoder_->cacheIdentity()))) return true;
        }
    }
    return cacheFor(request.purpose).contains(cacheKey(request, decoder_->cacheIdentity()));
}

qsizetype ImageLoader::cachedBytes() const {
    Q_ASSERT(thread() == QThread::currentThread());
    return thumbnailCache_.cost() + previewCache_.cost() + fullCache_.cost() + sourceCache_->cost();
}

void ImageLoader::setMemoryBudget(qsizetype bytes) {
    Q_ASSERT(thread() == QThread::currentThread());
    memoryBudget_ = std::max<qsizetype>(bytes, 1);
    sourceCache_->setBudget(std::min<qsizetype>(96LL * 1024 * 1024, memoryBudget_ / 4));
    automaticFullLoadBudget_ =
        std::min<qsizetype>(256LL * 1024LL * 1024LL, memoryBudget_);
    enforceMemoryBudget(DecodePurpose::Full);
}

qsizetype ImageLoader::estimatedFullFrameCost(const ImageFrame& preview) {
    if (preview.rawParameters) {
        return estimatedFullFrameBytes(*preview.rawParameters);
    }
    const QSize sourceSize = preview.metadata.sourceSize.isValid()
                                 ? preview.metadata.sourceSize
                                 : preview.descriptor.size;
    if (sourceSize.isEmpty()) {
        return 0;
    }
    // RGBA64 retains native samples plus its prepared half-float upload buffer.
    const qsizetype bytesPerPixel = preview.descriptor.storageBits > 8 ? 16 : 4;
    const qint64 pixels = static_cast<qint64>(sourceSize.width()) * sourceSize.height();
    if (pixels <= 0 || pixels > std::numeric_limits<qsizetype>::max() / bytesPerPixel) {
        return std::numeric_limits<qsizetype>::max();
    }
    return static_cast<qsizetype>(pixels) * bytesPerPixel;
}

bool ImageLoader::canAutomaticallyLoadFull(
    const QVector<ImageFramePtr>& previewFrames) const {
    if (fastScrolling() || hasInteractiveWork()) return false;
    qsizetype total = 0;
    for (const ImageFramePtr& frame : previewFrames) {
        if (!frame) {
            return false;
        }
        const qsizetype cost = estimatedFullFrameCost(*frame);
        if (cost <= 0 || cost > automaticFullLoadBudget_ - total) {
            return false;
        }
        total += cost;
    }
    return total <= automaticFullLoadBudget_;
}

void ImageLoader::enforceMemoryBudget(DecodePurpose insertedPurpose) {
    constexpr qsizetype thumbnailReserve = 64LL * 1024 * 1024;
    constexpr qsizetype previewReserve = 64LL * 1024 * 1024;
    auto evictOne = [](WeightedLruCache<ImageFrame>& cache) {
        return cache.evictLeastRecentlyUsed() > 0;
    };
    while (cachedBytes() > memoryBudget_) {
        bool evicted = false;
        if (fullCache_.cost() > 0 &&
            (insertedPurpose != DecodePurpose::Full ||
             fullCache_.cost() > memoryBudget_ / 2)) {
            evicted = evictOne(fullCache_);
        }
        if (!evicted && previewCache_.cost() > previewReserve) {
            evicted = evictOne(previewCache_);
        }
        if (!evicted && thumbnailCache_.cost() > thumbnailReserve) {
            evicted = evictOne(thumbnailCache_);
        }
        if (!evicted && fullCache_.cost() > 0) {
            evicted = evictOne(fullCache_);
        }
        if (!evicted && previewCache_.cost() > 0) {
            evicted = evictOne(previewCache_);
        }
        if (!evicted && thumbnailCache_.cost() > 0) {
            evicted = evictOne(thumbnailCache_);
        }
        if (!evicted) {
            break;
        }
    }
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
    return cacheKeyPrefix(request, QFileInfo(request.path)) + QLatin1Char('|') +
        (request.rawParameters ? request.rawParameters->cacheKey() : QStringLiteral("encoded")) +
        QLatin1Char('|') + decoderIdentity;
}

} // namespace mvpview
