#pragma once

#include "core/raw_image_parameters.h"

#include <array>

namespace ispview {

// Matches the std140 BayerParameters block in bayer.frag. RAW bytes remain in their
// source packing; the shader uses these values to unpack and reproduce the CPU reference
// display transform without changing the original values used by pixel inspection.
// imageAndStorage.z carries the CFA sample-block edge (1 for standard Bayer, 2 for Quad Bayer).
// whiteBalance.w carries the display-mode flag: 0 = CFA false-colour mosaic, 1 = demosaic RGB.
[[nodiscard]] std::array<float, 28>
makeBayerRenderUniformData(const RawImageParameters& parameters);

} // namespace ispview
