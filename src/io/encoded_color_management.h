#pragma once

#include "core/display_color_space.h"
#include "core/image_types.h"

#include <QImage>
#include <QString>

namespace ispview {

// Converts an encoded RGB image's embedded ICC profile into the application display space.
// RAW/YUV source planes never pass through this adapter.
class EncodedColorManagement final {
  public:
    [[nodiscard]] static bool isAvailable();
    [[nodiscard]] static bool isEnabled();
    [[nodiscard]] static QString version();
    static void setEnabled(bool enabled);
    // Converts into the current display space. Untagged images follow the documented sRGB
    // assumption, so they are converted too whenever the display space is wider than sRGB.
    static void normalizeToDisplay(QImage& image, ImageMetadata& metadata);
    static void normalizeToDisplay(QImage& image, ImageMetadata& metadata,
                                   DisplayColorSpace target);
};

} // namespace ispview
