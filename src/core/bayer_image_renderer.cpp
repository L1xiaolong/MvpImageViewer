#include "core/bayer_image_renderer.h"

#include "core/raw_plane_access.h"

#include <QColorSpace>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>

namespace ispview {
namespace {

int toByte(double value) {
    return std::clamp(static_cast<int>(std::lround(value * 255.0)), 0, 255);
}

quint16 read16(const char* bytes, bool littleEndian) {
    const auto* source = reinterpret_cast<const uchar*>(bytes);
    return littleEndian ? qFromLittleEndian<quint16>(source)
                        : qFromBigEndian<quint16>(source);
}

qsizetype rowStride(const RawImageParameters& parameters) {
    return parameters.rowStride > 0 ? parameters.rowStride : minimumRowStride(parameters);
}

int cfaChannel(BayerPattern pattern, BayerSampling sampling, int x, int y) {
    const int blockSize = bayerSampleBlockSize(sampling);
    const bool evenX = ((x / blockSize) & 1) == 0;
    const bool evenY = ((y / blockSize) & 1) == 0;
    switch (pattern) {
    case BayerPattern::RGGB:
        return evenY ? (evenX ? 0 : 1) : (evenX ? 1 : 2);
    case BayerPattern::GRBG:
        return evenY ? (evenX ? 1 : 0) : (evenX ? 2 : 1);
    case BayerPattern::GBRG:
        return evenY ? (evenX ? 1 : 2) : (evenX ? 0 : 1);
    case BayerPattern::BGGR:
        return evenY ? (evenX ? 2 : 1) : (evenX ? 1 : 0);
    }
    return 1;
}

std::optional<quint16> packedBayerValue(const QByteArray& bytes,
                                        const RawImageParameters& parameters, int x, int y) {
    if (x < 0 || y < 0 || x >= parameters.size.width() || y >= parameters.size.height()) {
        return std::nullopt;
    }
    const qsizetype stride = rowStride(parameters);
    const char* row = bytes.constData() + y * stride;
    switch (parameters.format) {
    case RawPixelFormat::MipiRaw10: {
        const int groupX = x / 4;
        const int lane = x % 4;
        const auto* group = reinterpret_cast<const uchar*>(row + groupX * 5);
        return static_cast<quint16>((group[lane] << 2) | ((group[4] >> (lane * 2)) & 0x03));
    }
    case RawPixelFormat::MipiRaw12: {
        const int groupX = x / 2;
        const int lane = x % 2;
        const auto* group = reinterpret_cast<const uchar*>(row + groupX * 3);
        const int lowShift = lane * 4;
        return static_cast<quint16>((group[lane] << 4) | ((group[2] >> lowShift) & 0x0F));
    }
    case RawPixelFormat::Raw16: {
        quint16 value = read16(row + x * 2, parameters.littleEndian);
        if (parameters.validBits() < 16) {
            const quint16 mask = static_cast<quint16>((1U << parameters.validBits()) - 1U);
            value = parameters.msbAligned
                        ? static_cast<quint16>(value >> (16 - parameters.validBits()))
                        : static_cast<quint16>(value & mask);
        }
        return value;
    }
    default:
        return std::nullopt;
    }
}

QImage convertBayer(const QByteArray& bytes, const RawImageParameters& parameters,
                    const QSize& outputSize) {
    const int width = parameters.size.width();
    const int height = parameters.size.height();
    const int maximum = parameters.whiteLevel > parameters.blackLevel
                            ? parameters.whiteLevel
                            : parameters.maximumSampleValue();
    auto normalized = [&](int x, int y) {
        const int raw = packedBayerValue(bytes, parameters, x, y).value_or(0);
        return std::clamp((raw - parameters.blackLevel) /
                              static_cast<double>(maximum - parameters.blackLevel),
                          0.0, 1.0);
    };
    const auto mosaicDisplayValue = [&](int x, int y) {
        return packedBayerValue(bytes, parameters, x, y).value_or(0) /
               static_cast<double>(parameters.maximumSampleValue());
    };

    QImage image(outputSize, QImage::Format_RGBA8888);
    const bool fullSize = outputSize == parameters.size;
    for (int y = 0; y < outputSize.height(); ++y) {
        auto* destination = image.scanLine(y);
        for (int x = 0; x < outputSize.width(); ++x) {
            const int centerX =
                fullSize ? x
                         : std::clamp(static_cast<int>((x + 0.5) * width / outputSize.width()), 0,
                                      width - 1);
            const int centerY =
                fullSize ? y
                         : std::clamp(static_cast<int>((y + 0.5) * height / outputSize.height()), 0,
                                      height - 1);
            if (!parameters.demosaic) {
                const uchar encoded =
                    static_cast<uchar>(toByte(mosaicDisplayValue(centerX, centerY)));
                const int channel = cfaChannel(parameters.bayerPattern,
                                               parameters.bayerSampling, centerX, centerY);
                destination[x * 4 + 0] = channel == 0 ? encoded : 0;
                destination[x * 4 + 1] = channel == 1 ? encoded : 0;
                destination[x * 4 + 2] = channel == 2 ? encoded : 0;
                destination[x * 4 + 3] = 255;
                continue;
            }
            double channels[3]{};
            int counts[3]{};
            const int radius = bayerSampleBlockSize(parameters.bayerSampling);
            for (int dy = -radius; dy <= radius; ++dy) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    const int sx = std::clamp(centerX + dx, 0, width - 1);
                    const int sy = std::clamp(centerY + dy, 0, height - 1);
                    const int channel = cfaChannel(parameters.bayerPattern,
                                                   parameters.bayerSampling, sx, sy);
                    channels[channel] += normalized(sx, sy);
                    ++counts[channel];
                }
            }
            std::array<double, 3> balanced{};
            for (std::size_t channel = 0; channel < balanced.size(); ++channel) {
                const double linear = counts[channel] > 0 ? channels[channel] / counts[channel] : 0;
                balanced[channel] = linear * parameters.whiteBalanceGains[channel];
            }
            for (std::size_t outputChannel = 0; outputChannel < balanced.size(); ++outputChannel) {
                double corrected = 0.0;
                for (std::size_t inputChannel = 0; inputChannel < balanced.size(); ++inputChannel) {
                    corrected +=
                        parameters.colorCorrectionMatrix[outputChannel * 3 + inputChannel] *
                        balanced[inputChannel];
                }
                const double encoded =
                    std::pow(std::clamp(corrected, 0.0, 1.0), 1.0 / parameters.displayGamma);
                destination[x * 4 + static_cast<int>(outputChannel)] =
                    static_cast<uchar>(toByte(encoded));
            }
            destination[x * 4 + 3] = 255;
        }
    }
    return image;
}

} // namespace

QImage renderBayerImage(const QByteArray& bytes, const RawImageParameters& parameters,
                        const QSize& outputSize) {
    QImage image = convertBayer(bytes, parameters,
                                outputSize.isEmpty() ? parameters.size : outputSize);
    // The developed mosaic keeps its documented CCM contract: sRGB/BT.709 primaries with the
    // user's display gamma. It is a user-defined transform, not the application display space,
    // and the properties panel reports it as such.
    image.setColorSpace(QColorSpace(QColorSpace::SRgb));
    return image;
}

} // namespace ispview
