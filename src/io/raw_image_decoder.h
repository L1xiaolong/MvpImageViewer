#pragma once

#include "core/bayer_image_renderer.h"
#include "io/image_decoder.h"

#include <optional>

namespace ispview {

class RawImageDecoder final : public IImageDecoder {
  public:
    [[nodiscard]] QString cacheIdentity() const override;
    [[nodiscard]] bool canDecode(const QString& path) const override;
    [[nodiscard]] DecodeResult decode(const DecodeRequest& request) const override;

    [[nodiscard]] static std::optional<quint16> bayerValueAt(const ImageFrame& frame, int x, int y);
    [[nodiscard]] static QString pixelDescription(const ImageFrame& frame, int x, int y);
};

} // namespace ispview
