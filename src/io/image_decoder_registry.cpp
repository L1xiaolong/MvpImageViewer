#include "io/image_decoder_registry.h"

#include "diagnostics/diagnostics.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonObject>
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
        if (!decoder->canDecode(request.path)) {
            continue;
        }
        using namespace diagnostics;
        const QString operation = operationId();
        const bool breadcrumb = request.purpose != DecodePurpose::Thumbnail;
        QJsonObject context{
            {QStringLiteral("operation"), operation},
            {QStringLiteral("file"), fileId(request.path)},
            {QStringLiteral("format"), QFileInfo(request.path).suffix().toLower()},
            {QStringLiteral("decoder"), decoder->cacheIdentity()},
            {QStringLiteral("purpose"), static_cast<int>(request.purpose)}};
        event(Level::Debug, diagnostics::decode(), QStringLiteral("decode.begin"), context,
              breadcrumb);
        QElapsedTimer timer;
        timer.start();
        const DecodeResult result = decoder->decode(request);
        context.insert(QStringLiteral("elapsedMs"), timer.elapsed());
        if (result.frame) {
            context.insert(QStringLiteral("width"), result.frame->descriptor.size.width());
            context.insert(QStringLiteral("height"), result.frame->descriptor.size.height());
            context.insert(QStringLiteral("bits"), result.frame->descriptor.validBits);
        } else {
            context.insert(QStringLiteral("reason"), result.error);
        }
        const bool succeeded = result.succeeded();
        event(succeeded ? Level::Debug : Level::Error, diagnostics::decode(),
              succeeded ? QStringLiteral("decode.complete") : QStringLiteral("decode.failed"),
              context, breadcrumb);
        return result;
    }
    return {{}, QStringLiteral("Unsupported image format")};
}

} // namespace ispview
