#pragma once

#include "core/image_types.h"

#include <QSize>
#include <QString>

#include <optional>
#include <utility>

namespace ispview {

enum class DecodePurpose { Thumbnail, Preview, Full };
enum class DecodeExecutionMode { Parallel, Serialized };

struct DecodeRequest {
    DecodeRequest() = default;
    DecodeRequest(QString path, DecodePurpose purpose, QSize maximumSize = {},
                  std::optional<RawImageParameters> rawParameters = std::nullopt)
        : path(std::move(path)), purpose(purpose), maximumSize(maximumSize),
          rawParameters(std::move(rawParameters)) {}

    QString path;
    DecodePurpose purpose = DecodePurpose::Preview;
    QSize maximumSize;
    std::optional<RawImageParameters> rawParameters;
};

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

} // namespace ispview
