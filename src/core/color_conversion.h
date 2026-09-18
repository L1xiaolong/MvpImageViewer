#pragma once

#include "core/display_color_space.h"
#include "core/raw_image_parameters.h"

#include <array>

namespace ispview {

struct YuvCoefficients {
    double redV;
    double greenU;
    double greenV;
    double blueU;
};

[[nodiscard]] YuvCoefficients yuvCoefficients(YuvMatrix matrix);
// Source primaries mapped into the fixed sRGB space. Kept public because it is the regression
// reference for the display-space matrices and because sRGB remains the default display space.
[[nodiscard]] std::array<double, 9> yuvPrimariesToSrgbMatrix(YuvPrimaries primaries);
// Source primaries mapped into the primaries of a display space. Both sides are D65, so no
// chromatic adaptation is needed.
[[nodiscard]] std::array<double, 9>
yuvPrimariesToDisplayMatrix(YuvPrimaries primaries, DisplayPrimaries display);
// Linear light encoded with the transfer curve of a display space.
[[nodiscard]] double encodeForDisplay(double linear, DisplayTransfer transfer);
// Converts source nonlinear R'G'B' values into the requested display encoding.
[[nodiscard]] std::array<double, 3>
yuvRgbToDisplay(const std::array<double, 3>& sourceRgb, const RawImageParameters& parameters,
                DisplayColorSpace target);
// Same conversion targeting the application-wide display space.
[[nodiscard]] std::array<double, 3>
yuvRgbToDisplay(const std::array<double, 3>& sourceRgb, const RawImageParameters& parameters);
// Explicit sRGB target, which is what the viewer used before the display space was configurable.
[[nodiscard]] std::array<double, 3>
yuvRgbToDisplaySrgb(const std::array<double, 3>& sourceRgb,
                    const RawImageParameters& parameters);

} // namespace ispview
