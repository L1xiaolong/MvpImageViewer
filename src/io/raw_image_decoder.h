#pragma once

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

// Develops a Bayer buffer with the same black/white levels, demosaic, white balance,
// colour matrix, and display transfer used by the GPU renderer. This is also the bounded
// fallback for camera RAW files whose full sensor mosaic is rendered on the GPU.
[[nodiscard]] QImage renderBayerImage(const QByteArray& bytes,
                                      const RawImageParameters& parameters,
                                      const QSize& outputSize = {});

} // namespace ispview
