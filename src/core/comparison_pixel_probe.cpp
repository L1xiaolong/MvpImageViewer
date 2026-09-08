#include "core/comparison_pixel_probe.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace ispview {
namespace {

QPoint pixelAtNormalizedPoint(const QPointF& normalizedPoint, const QSize& size) {
    if (size.isEmpty() || !std::isfinite(normalizedPoint.x()) ||
        !std::isfinite(normalizedPoint.y()) || normalizedPoint.x() < 0.0 ||
        normalizedPoint.x() > 1.0 || normalizedPoint.y() < 0.0 || normalizedPoint.y() > 1.0) {
        return {-1, -1};
    }
    const double u = normalizedPoint.x();
    const double v = normalizedPoint.y();
    return {std::min(static_cast<int>(std::floor(u * size.width())), size.width() - 1),
            std::min(static_cast<int>(std::floor(v * size.height())), size.height() - 1)};
}

QString rgbaText(const QColor& color) {
    return color.isValid() ? QStringLiteral("RGBA(%1,%2,%3,%4)")
                                 .arg(color.red())
                                 .arg(color.green())
                                 .arg(color.blue())
                                 .arg(color.alpha())
                           : QString{};
}

int toByte(double value) {
    return std::clamp(static_cast<int>(std::lround(value * 255.0)), 0, 255);
}

QColor yuvDisplayColor(const RawImageParameters& parameters, const YuvPlaneSample& sample) {
    std::array<double, 4> coefficients{};
    switch (parameters.yuvMatrix) {
    case YuvMatrix::BT601:
        coefficients = {1.402, 0.344136, 0.714136, 1.772};
        break;
    case YuvMatrix::BT2020:
        coefficients = {1.4746, 0.164553, 0.571353, 1.8814};
        break;
    case YuvMatrix::BT709:
        coefficients = {1.5748, 0.187324, 0.468124, 1.8556};
        break;
    }
    const int bits = parameters.format == RawPixelFormat::P010 ? 10 : 8;
    const double maximum = static_cast<double>((1 << bits) - 1);
    const double levelScale = static_cast<double>(1 << (bits - 8));
    const double yOffset =
        parameters.range == QuantizationRange::Limited ? 16.0 * levelScale : 0.0;
    const double yScale =
        parameters.range == QuantizationRange::Limited ? 219.0 * levelScale : maximum;
    const double chromaCenter = static_cast<double>(1 << (bits - 1));
    const double chromaScale =
        parameters.range == QuantizationRange::Limited ? 224.0 * levelScale : maximum;
    const double y = (sample.y - yOffset) / yScale;
    const double u = (sample.u - chromaCenter) / chromaScale;
    const double v = (sample.v - chromaCenter) / chromaScale;
    return QColor::fromRgb(toByte(y + coefficients[0] * v),
                           toByte(y - coefficients[1] * u - coefficients[2] * v),
                           toByte(y + coefficients[3] * u));
}

QColor bayerDisplayColor(const RawImageParameters& parameters,
                         const RawPlaneAccessor& accessor,
                         const BayerPlaneSample& center) {
    const int white = parameters.whiteLevel > parameters.blackLevel
                          ? parameters.whiteLevel
                          : parameters.maximumSampleValue();
    const auto normalized = [&parameters, &accessor, white](const QPoint& pixel) {
        const auto sample = accessor.bayerAtSourcePixel(pixel);
        if (!sample || white <= parameters.blackLevel)
            return 0.0;
        return std::clamp((sample->value - parameters.blackLevel) /
                              static_cast<double>(white - parameters.blackLevel),
                          0.0, 1.0);
    };
    if (!parameters.demosaic) {
        const int gray = toByte(std::pow(normalized(center.sourcePixel),
                                         1.0 / parameters.displayGamma));
        return QColor::fromRgb(gray, gray, gray);
    }

    std::array<double, 3> sums{};
    std::array<int, 3> counts{};
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const QPoint samplePixel(
                std::clamp(center.sourcePixel.x() + dx, 0, parameters.size.width() - 1),
                std::clamp(center.sourcePixel.y() + dy, 0, parameters.size.height() - 1));
            const auto channel =
                RawPlaneAccessor::channelAtSourcePixel(parameters.bayerPattern, samplePixel);
            const int channelIndex = channel == BayerSampleChannel::Red
                                         ? 0
                                     : channel == BayerSampleChannel::Blue ? 2
                                                                          : 1;
            sums[static_cast<std::size_t>(channelIndex)] += normalized(samplePixel);
            ++counts[static_cast<std::size_t>(channelIndex)];
        }
    }
    std::array<double, 3> balanced{};
    for (std::size_t channel = 0; channel < balanced.size(); ++channel) {
        balanced[channel] = counts[channel] > 0
                                ? sums[channel] / counts[channel] *
                                      parameters.whiteBalanceGains[channel]
                                : 0.0;
    }
    std::array<int, 3> encoded{};
    for (std::size_t output = 0; output < encoded.size(); ++output) {
        double corrected = 0.0;
        for (std::size_t input = 0; input < balanced.size(); ++input) {
            corrected += parameters.colorCorrectionMatrix[output * 3 + input] * balanced[input];
        }
        encoded[output] = toByte(std::pow(std::clamp(corrected, 0.0, 1.0),
                                          1.0 / parameters.displayGamma));
    }
    return QColor::fromRgb(encoded[0], encoded[1], encoded[2]);
}

} // namespace

QString ComparisonPixelSample::sourceValueText() const {
    if (yuv) {
        return QStringLiteral("YUV(%1,%2,%3)").arg(yuv->y).arg(yuv->u).arg(yuv->v);
    }
    if (bayer) {
        return QStringLiteral("RAW(%1, %2)")
            .arg(bayer->value)
            .arg(RawPlaneAccessor::channelName(bayer->channel));
    }
    return rgbaText(displayColor);
}

QString ComparisonPixelSample::displayValueText() const { return rgbaText(displayColor); }

QPointF ComparisonPixelProbe::normalizedPixelCenter(const QPoint& pixel, const QSize& imageSize) {
    if (imageSize.isEmpty() || !QRect(QPoint{}, imageSize).contains(pixel)) {
        return {};
    }
    return {(pixel.x() + 0.5) / imageSize.width(), (pixel.y() + 0.5) / imageSize.height()};
}

ComparisonPixelSample ComparisonPixelProbe::sample(const ImageFrame& frame,
                                                   const QPointF& normalizedPoint) {
    const RawPlaneAccessor rawAccessor(frame);
    const QSize logicalSize =
        rawAccessor.isValid() ? rawAccessor.displaySize() : frame.descriptor.size;
    const QPoint displayPixel = pixelAtNormalizedPoint(normalizedPoint, logicalSize);
    if (!QRect(QPoint{}, logicalSize).contains(displayPixel)) {
        return {};
    }

    ComparisonPixelSample result;
    result.valid = true;
    result.displayPixel = displayPixel;
    result.sourcePixel = displayPixel;
    if (rawAccessor.isValid()) {
        if (rawAccessor.isYuv()) {
            result.yuv = rawAccessor.yuvAtDisplayPixel(displayPixel);
            if (result.yuv) {
                result.sourcePixel = result.yuv->sourcePixel;
                result.displayColor = yuvDisplayColor(*frame.rawParameters, *result.yuv);
            }
        } else {
            result.bayer = rawAccessor.bayerAtDisplayPixel(displayPixel);
            if (result.bayer) {
                result.sourcePixel = result.bayer->sourcePixel;
                result.displayColor =
                    bayerDisplayColor(*frame.rawParameters, rawAccessor, *result.bayer);
            }
        }
    }

    const QImage* display = frame.qImage();
    if (!result.displayColor.isValid() && display && !display->isNull()) {
        const int sampleX = std::clamp(
            static_cast<int>((displayPixel.x() + 0.5) * display->width() / logicalSize.width()), 0,
            display->width() - 1);
        const int sampleY = std::clamp(
            static_cast<int>((displayPixel.y() + 0.5) * display->height() / logicalSize.height()),
            0, display->height() - 1);
        result.displayColor = display->pixelColor(sampleX, sampleY);
    }
    return result;
}

} // namespace ispview
