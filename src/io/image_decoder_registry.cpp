#include "io/image_decoder_registry.h"

#include <QStringList>

namespace ispview {

QString ImageDecoderRegistry::cacheIdentity() const {
    QStringList identities;
    identities.reserve(static_cast<qsizetype>(decoders_.size()));
    for (const auto& decoder : decoders_) {
        identities.push_back(decoder->cacheIdentity());
    }
    return identities.join(QLatin1Char(';'));
}

void ImageDecoderRegistry::add(std::shared_ptr<const IImageDecoder> decoder) {
    if (decoder) {
        decoders_.push_back(std::move(decoder));
    }
}

DecodeExecutionMode ImageDecoderRegistry::executionMode(const QString& path) const {
    for (const auto& decoder : decoders_) {
        if (decoder->canDecode(path)) {
            return decoder->executionMode(path);
        }
    }
    return DecodeExecutionMode::Parallel;
}

bool ImageDecoderRegistry::canDecode(const QString& path) const {
    for (const auto& decoder : decoders_) {
        if (decoder->canDecode(path)) {
            return true;
        }
    }
    return false;
}

DecodeResult ImageDecoderRegistry::decode(const DecodeRequest& request) const {
    for (const auto& decoder : decoders_) {
        if (decoder->canDecode(request.path)) {
            return decoder->decode(request);
        }
    }
    return {{}, QStringLiteral("Unsupported image format")};
}

} // namespace ispview
