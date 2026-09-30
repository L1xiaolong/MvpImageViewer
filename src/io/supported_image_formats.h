#pragma once

#include <QString>
#include <QStringList>

namespace mvpview {

// Central product-facing suffix list used by scanners and tools. Availability-sensitive formats
// such as camera RAW are included only when their adapter is usable in the current build.
[[nodiscard]] QStringList supportedImageSuffixes();
[[nodiscard]] QStringList supportedImageNameFilters();
[[nodiscard]] bool hasSupportedImageSuffix(const QString& path);

} // namespace mvpview
