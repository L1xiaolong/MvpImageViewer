#pragma once

#include "core/image_types.h"

#include <QImage>
#include <QString>

namespace ispview {

// Converts an encoded RGB image's embedded ICC profile into the fixed sRGB display encoding.
// RAW/YUV source planes never pass through this adapter.
class EncodedColorManagement final {
  public:
    [[nodiscard]] static bool isAvailable();
    [[nodiscard]] static bool isEnabled();
    [[nodiscard]] static QString version();
    static void setEnabled(bool enabled);
    static void normalizeToSrgb(QImage& image, ImageMetadata& metadata);
};

} // namespace ispview
