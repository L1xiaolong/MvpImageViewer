#include "core/color_conversion.h"

#include <algorithm>
#include <cmath>

namespace ispview {
namespace {

double decodeTransfer(double encoded, YuvTransfer transfer) {
    const double value = std::clamp(encoded, 0.0, 1.0);
    switch (transfer) {
    case YuvTransfer::Linear:
        return value;
    case YuvTransfer::SRgb:
        return value <= 0.04045 ? value / 12.92
                                : std::pow((value + 0.055) / 1.055, 2.4);
    case YuvTransfer::BT709:
    default:
        return value < 0.081 ? value / 4.5
                             : std::pow((value + 0.099) / 1.099, 1.0 / 0.45);
    }
}

double encodeSrgb(double linear) {
    const double value = std::clamp(linear, 0.0, 1.0);
    return value <= 0.0031308 ? 12.92 * value
                              : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
}

// sRGB primaries mapped into a display space's primaries. Derived from the primaries themselves
// (all four spaces are D65, so the matrices are pure gamut scalings) and verified against Qt's
// QColorSpace conversion in the unit tests.
std::array<double, 9> srgbToDisplayMatrix(DisplayPrimaries display) {
    switch (display) {
    case DisplayPrimaries::DisplayP3:
        return {0.822462, 0.177538, 0.0,
                0.033194, 0.966806, 0.0,
                0.017083, 0.072397, 0.910520};
    case DisplayPrimaries::AdobeRgb:
        return {0.715126, 0.284874, 0.0,
                0.0,      1.0,      0.0,
                0.0,      0.041162, 0.958838};
    case DisplayPrimaries::Bt2020:
        return {0.627404, 0.329283, 0.043313,
                0.069097, 0.919540, 0.011362,
                0.016391, 0.088013, 0.895595};
    case DisplayPrimaries::SrgbBt709:
    default:
        return {1.0, 0.0, 0.0,
                0.0, 1.0, 0.0,
                0.0, 0.0, 1.0};
    }
}

// Row-major 3x3 product, matching the layout of yuvPrimariesToSrgbMatrix().
std::array<double, 9> multiply(const std::array<double, 9>& left,
                               const std::array<double, 9>& right) {
    std::array<double, 9> result{};
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            double value = 0.0;
            for (std::size_t inner = 0; inner < 3; ++inner) {
                value += left[row * 3 + inner] * right[inner * 3 + column];
            }
            result[row * 3 + column] = value;
        }
    }
    return result;
}

} // namespace

YuvCoefficients yuvCoefficients(YuvMatrix matrix) {
    switch (matrix) {
    case YuvMatrix::BT601: return {1.402, 0.344136, 0.714136, 1.772};
    case YuvMatrix::BT2020: return {1.4746, 0.164553, 0.571353, 1.8814};
    case YuvMatrix::BT709:
    default: return {1.5748, 0.187324, 0.468124, 1.8556};
    }
}

std::array<double, 9> yuvPrimariesToSrgbMatrix(YuvPrimaries primaries) {
    switch (primaries) {
    case YuvPrimaries::BT2020:
        return {1.660491, -0.587641, -0.072850,
                -0.124551, 1.132900, -0.008349,
                -0.018151, -0.100579, 1.118730};
    case YuvPrimaries::BT601_625:
        return {1.044043, -0.044043, 0.0,
                0.0, 1.0, 0.0,
                0.0, -0.011793, 1.011793};
    case YuvPrimaries::BT601_525:
        return {0.939542, 0.050181, 0.010277,
                0.017772, 0.965793, 0.016435,
                -0.001622, -0.004371, 1.005993};
    case YuvPrimaries::BT709:
    default:
        return {1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
    }
}

std::array<double, 9> yuvPrimariesToDisplayMatrix(YuvPrimaries primaries,
                                                 DisplayPrimaries display) {
    if (display == DisplayPrimaries::SrgbBt709) {
        // Keep the sRGB target bit-exact with the historical results.
        return yuvPrimariesToSrgbMatrix(primaries);
    }
    return multiply(srgbToDisplayMatrix(display), yuvPrimariesToSrgbMatrix(primaries));
}

double encodeForDisplay(double linear, DisplayTransfer transfer) {
    const double value = std::clamp(linear, 0.0, 1.0);
    switch (transfer) {
    case DisplayTransfer::AdobeGamma1998:
        // Adobe RGB (1998) encodes with 563/256, notably not 2.2.
        return std::pow(value, 1.0 / (563.0 / 256.0));
    case DisplayTransfer::Bt2020:
        // Inverse of the curve Qt stores for QColorSpace::Bt2020, so the tag, its ICC profile,
        // and these pixels stay consistent.
        return value <= 0.0181 ? 4.5 * value
                               : 1.0993 * std::pow(value, 1.0 / 2.2) - 0.0993;
    case DisplayTransfer::Srgb:
    default: return encodeSrgb(value);
    }
}

std::array<double, 3> yuvRgbToDisplay(const std::array<double, 3>& sourceRgb,
                                      const RawImageParameters& parameters,
                                      DisplayColorSpace target) {
    std::array<double, 3> linear{};
    for (std::size_t channel = 0; channel < linear.size(); ++channel) {
        linear[channel] = decodeTransfer(sourceRgb[channel], parameters.yuvTransfer);
    }
    const auto matrix =
        yuvPrimariesToDisplayMatrix(parameters.yuvPrimaries, displayPrimaries(target));
    const DisplayTransfer transfer = displayTransfer(target);
    std::array<double, 3> encoded{};
    for (std::size_t output = 0; output < encoded.size(); ++output) {
        double value = 0.0;
        for (std::size_t input = 0; input < linear.size(); ++input) {
            value += matrix[output * 3 + input] * linear[input];
        }
        encoded[output] = encodeForDisplay(value, transfer);
    }
    return encoded;
}

std::array<double, 3> yuvRgbToDisplay(const std::array<double, 3>& sourceRgb,
                                      const RawImageParameters& parameters) {
    return yuvRgbToDisplay(sourceRgb, parameters, currentDisplayColorSpace());
}

std::array<double, 3> yuvRgbToDisplaySrgb(const std::array<double, 3>& sourceRgb,
                                          const RawImageParameters& parameters) {
    return yuvRgbToDisplay(sourceRgb, parameters, DisplayColorSpace::Srgb);
}

} // namespace ispview
