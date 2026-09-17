#include "io/supported_image_formats.h"

#include "io/camera_raw_decoder.h"
#include "io/qt_image_decoder.h"

#include <QFileInfo>

#include <algorithm>

namespace ispview {

QStringList supportedImageSuffixes() {
    QStringList suffixes = QtImageDecoder::supportedSuffixes();
    suffixes.append({QStringLiteral("raw"), QStringLiteral("yuv")});
    suffixes.append(CameraRawDecoder::supportedSuffixes());
    suffixes.removeDuplicates();
    return suffixes;
}

QStringList supportedImageNameFilters() {
    QStringList filters;
    for (const QString& suffix : supportedImageSuffixes()) {
        filters.append(QStringLiteral("*.%1").arg(suffix));
        filters.append(QStringLiteral("*.%1").arg(suffix.toUpper()));
    }
    filters.removeDuplicates();
    return filters;
}

bool hasSupportedImageSuffix(const QString& path) {
    const QString suffix = QFileInfo(path).suffix();
    const QStringList supported = supportedImageSuffixes();
    return std::any_of(supported.cbegin(), supported.cend(), [&suffix](const QString& supported) {
        return suffix.compare(supported, Qt::CaseInsensitive) == 0;
    });
}

} // namespace ispview
