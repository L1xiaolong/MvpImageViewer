#include "render/bayer_render_parameters.h"

namespace ispview {
namespace {

float shaderFormat(RawPixelFormat format) {
    switch (format) {
    case RawPixelFormat::MipiRaw10:
        return 0.0F;
    case RawPixelFormat::MipiRaw12:
        return 1.0F;
    case RawPixelFormat::Raw16:
        return 2.0F;
    default:
        return -1.0F;
    }
}

} // namespace

std::array<float, 28> makeBayerRenderUniformData(const RawImageParameters& parameters) {
    std::array<float, 28> values{};
    values[0] = static_cast<float>(parameters.size.width());
    values[1] = static_cast<float>(parameters.size.height());
    values[2] = static_cast<float>(bayerSampleBlockSize(parameters.bayerSampling));
    values[3] = shaderFormat(parameters.format);
    values[4] = static_cast<float>(parameters.bayerPattern);
    values[5] = parameters.littleEndian ? 1.0F : 0.0F;
    values[6] = parameters.msbAligned ? 1.0F : 0.0F;
    values[7] = static_cast<float>(parameters.validBits());
    values[8] = static_cast<float>(parameters.blackLevel);
    values[9] = static_cast<float>(parameters.whiteLevel > parameters.blackLevel
                                       ? parameters.whiteLevel
                                       : parameters.maximumSampleValue());
    values[10] = static_cast<float>(parameters.displayGamma);
    values[11] = static_cast<float>(parameters.orientation);
    for (std::size_t channel = 0; channel < parameters.whiteBalanceGains.size(); ++channel) {
        values[12 + channel] = static_cast<float>(parameters.whiteBalanceGains[channel]);
    }
    values[15] = parameters.demosaic ? 1.0F : 0.0F;

    // GLSL matrices are column-major. The public parameter is row-major, so each std140
    // vec4 below receives one matrix column and leaves its padding component at zero.
    for (std::size_t column = 0; column < 3; ++column) {
        for (std::size_t row = 0; row < 3; ++row) {
            values[16 + column * 4 + row] =
                static_cast<float>(parameters.colorCorrectionMatrix[row * 3 + column]);
        }
    }
    return values;
}

} // namespace ispview
