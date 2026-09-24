#pragma once

#include "core/image_types.h"

#include <QString>

namespace ispview {

// Exiv2 adapter. Third-party metadata types deliberately stop at this boundary.
// Failures are reported on ImageMetadata and never turn a successful pixel decode into an error.
class MetadataReader final {
  public:
    [[nodiscard]] static bool isAvailable();
    [[nodiscard]] static QString version();
    static void enrich(const QString& path, ImageMetadata& metadata);
};

} // namespace ispview
