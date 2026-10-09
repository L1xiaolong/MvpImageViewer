#pragma once

#include "core/image_types.h"
#include "core/source_frame_cache.h"

#include <QSize>
#include <QString>

#include <atomic>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

namespace mvpview {

enum class DecodePurpose { Thumbnail, Preview, Full };
enum class DecodeExecutionMode { Parallel, Serialized };

struct DecodeRequest {
    DecodeRequest() = default;
    DecodeRequest(QString path, DecodePurpose purpose, QSize maximumSize = {},
                  std::optional<RawImageParameters> rawParameters = std::nullopt)
        : path(std::move(path)), purpose(purpose), maximumSize(maximumSize),
          rawParameters(std::move(rawParameters)) {}

    // Shared consumer count: decoders stop only after the last consumer leaves.
    std::shared_ptr<std::atomic_int> activeConsumers;
    [[nodiscard]] bool isCancelled() const {
        return activeConsumers && activeConsumers->load(std::memory_order_relaxed) <= 0;
    }
    std::shared_ptr<SourceFrameCache> sourceCache;
    // Called on the decode worker after header discovery, before large pixel allocations.
    // Replacement is valid only at a stage with no previous private pixel allocation alive.
    std::function<bool(qsizetype)> reserveWorkingMemory;
    [[nodiscard]] bool prepareAllocation(qsizetype bytes) const {
        return !isCancelled() && (!reserveWorkingMemory || reserveWorkingMemory(bytes));
    }
    ImageFramePtr metadataSource;
    // QML Image consumes the CPU image; RHI consumers can use native source planes.
    // Keep these representations distinct when Full has a bounded CPU fallback.
    bool requireDisplayImage = false;
    QString path;
    DecodePurpose purpose = DecodePurpose::Preview;
    QSize maximumSize;
    std::optional<RawImageParameters> rawParameters;
};

inline qsizetype estimatedPixelBytes(QSize size, qsizetype bytesPerPixel) {
    if (!size.isValid()) return 0;
    const auto pixels = qint64(size.width()) * size.height();
    const auto limit = std::numeric_limits<qsizetype>::max();
    return pixels > limit / bytesPerPixel ? limit : qsizetype(pixels * bytesPerPixel);
}

inline qsizetype addedAllocationBytes(qsizetype a, qsizetype b) {
    const auto limit = std::numeric_limits<qsizetype>::max();
    return b > limit - a ? limit : a + b;
}

struct DecodeResult {
    ImageFramePtr frame;
    QString error;

    [[nodiscard]] bool succeeded() const { return frame != nullptr; }
};

class IImageDecoder {
  public:
    virtual ~IImageDecoder() = default;
    [[nodiscard]] virtual QString cacheIdentity() const {
        return QStringLiteral("decoder-v1");
    }
    [[nodiscard]] virtual DecodeExecutionMode executionMode(const QString&) const {
        return DecodeExecutionMode::Parallel;
    }
    [[nodiscard]] virtual bool canDecode(const QString& path) const = 0;
    [[nodiscard]] virtual DecodeResult decode(const DecodeRequest& request) const = 0;
};

} // namespace mvpview
