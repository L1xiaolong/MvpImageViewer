#pragma once

#include <QByteArray>
#include <QSize>
#include <QString>

namespace ispview {

// Exiv2 adapter for carrying metadata across destructive pixel transforms.
// Third-party metadata types deliberately stop at this boundary.
class MetadataWriter final {
  public:
    [[nodiscard]] static QString copyForImageTransform(const QString& sourcePath,
                                                       QByteArray& encodedImage,
                                                       const QSize& pixelSize);
};

} // namespace ispview
