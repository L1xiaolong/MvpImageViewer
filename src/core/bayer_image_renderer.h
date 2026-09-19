#pragma once

#include "core/raw_image_parameters.h"

#include <QByteArray>
#include <QImage>
#include <QSize>

namespace ispview {

// Develops a Bayer buffer with the same black/white levels, demosaic, white balance,
// colour matrix, and display transfer used by the GPU renderer. This is also the bounded
// fallback for camera RAW files whose full sensor mosaic is rendered on the GPU.
[[nodiscard]] QImage renderBayerImage(const QByteArray& bytes,
                                      const RawImageParameters& parameters,
                                      const QSize& outputSize = {});

} // namespace ispview
