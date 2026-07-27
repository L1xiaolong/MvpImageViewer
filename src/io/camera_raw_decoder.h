#pragma once

#include "io/image_decoder.h"

#include <QStringList>

namespace ispview {

class CameraRawDecoder final : public IImageDecoder {
  public:
    [[nodiscard]] static bool isAvailable();
    [[nodiscard]] static QStringList supportedSuffixes();

    [[nodiscard]] QString cacheIdentity() const override;
    [[nodiscard]] DecodeExecutionMode executionMode(const QString& path) const override;
    [[nodiscard]] bool canDecode(const QString& path) const override;
    [[nodiscard]] DecodeResult decode(const DecodeRequest& request) const override;
};

} // namespace ispview
