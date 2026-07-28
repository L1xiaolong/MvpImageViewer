#pragma once

#include "io/image_decoder.h"

namespace ispview {

class QtImageDecoder final : public IImageDecoder {
  public:
    [[nodiscard]] static bool autoOrientationEnabled();
    [[nodiscard]] static bool preserveHighBitDepth();
    static void setAutoOrientationEnabled(bool enabled);
    static void setPreserveHighBitDepth(bool enabled);

    [[nodiscard]] QString cacheIdentity() const override;
    [[nodiscard]] bool canDecode(const QString& path) const override;
    [[nodiscard]] DecodeResult decode(const DecodeRequest& request) const override;
};

} // namespace ispview
