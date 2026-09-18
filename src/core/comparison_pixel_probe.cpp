#include "core/comparison_pixel_probe.h"

#include <QtCore/qfloat16.h>

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

QString rgbText(const QColor& color) {
    return color.isValid() ? QStringLiteral("RGB(%1,%2,%3)")
                                 .arg(color.red())
                                 .arg(color.green())
                                 .arg(color.blue())
                           : QString{};
}

QString encodedRgbText(const QImage& image, int x, int y, const QColor& color) {
    switch (image.format()) {
    case QImage::Format_RGBA64:
    case QImage::Format_RGBA64_Premultiplied: {
        // pixelColor() unpremultiplies, so the reported sample is the encoded colour value.
        const QRgba64 pixel = image.pixelColor(x, y).rgba64();
        return QStringLiteral("RGB(%1,%2,%3)")
            .arg(pixel.red())
            .arg(pixel.green())
            .arg(pixel.blue());
    }
    case QImage::Format_RGBA16FPx4:
    case QImage::Format_RGBA32FPx4: {
        // Read the stored samples instead of routing them through QColor, which quantizes a
        // floating point frame to 16-bit integers and would print noisy digits such as
        // 0.5000076 for an HDR sample of 0.5.
        constexpr int channelsPerPixel = 4;
        std::array<double, 3> channels{};
        if (image.format() == QImage::Format_RGBA32FPx4) {
            const auto* line = reinterpret_cast<const float*>(image.constScanLine(y));
            for (std::size_t channel = 0; channel < channels.size(); ++channel) {
                channels[channel] =
                    static_cast<double>(line[x * channelsPerPixel + static_cast<int>(channel)]);
            }
        } else {
            const auto* line = reinterpret_cast<const qfloat16*>(image.constScanLine(y));
            for (std::size_t channel = 0; channel < channels.size(); ++channel) {
                channels[channel] = static_cast<double>(static_cast<float>(
                    line[x * channelsPerPixel + static_cast<int>(channel)]));
            }
        }
        return QStringLiteral("RGB(%1,%2,%3)")
            .arg(QString::number(channels[0], 'g', 7))
            .arg(QString::number(channels[1], 'g', 7))
            .arg(QString::number(channels[2], 'g', 7));
    }
    default:
        return rgbText(color);
    }
}

int toByte(double value) {
    return std::clamp(static_cast<int>(std::lround(value * 255.0)), 0, 255);
}

// Rounds a normalized sample into the source container's numeric range so reported values keep
// the RAW/YUV bit depth instead of collapsing to 8-bit display units.
int toSourceSample(double value, int maximum) {
    return std::clamp(static_cast<int>(std::lround(value * maximum)), 0, maximum);
}

QString sourceRgbValueText(const std::array<double, 3>& rgb, int maximum) {
    return QStringLiteral("RGB(%1,%2,%3)")
        .arg(toSourceSample(rgb[0], maximum))
        .arg(toSourceSample(rgb[1], maximum))
        .arg(toSourceSample(rgb[2], maximum));
}

// Sample range of the source container: ten bits for P010, eight for the byte oriented YUV
// formats, and the RAW container's own depth for Bayer frames.
int sourceSampleMaximum(const RawImageParameters& parameters) {
    if (!parameters.isYuv()) {
        return parameters.maximumSampleValue();
    }
    return parameters.format == RawPixelFormat::P010 ? 1023 : 255;
}

std::array<double, 2> interpolatedChromaAt(const RawPlaneAccessor& accessor,
                                           const QPoint& sourcePixel) {
    const QSize sourceSize = accessor.sourceSize();
    const QSize chromaSize((sourceSize.width() + 1) / 2, (sourceSize.height() + 1) / 2);
    const double chromaX = std::clamp(
        (sourcePixel.x() + 0.5) * chromaSize.width() / sourceSize.width() - 0.5,
        0.0, static_cast<double>(chromaSize.width() - 1));
    const double chromaY = std::clamp(
        (sourcePixel.y() + 0.5) * chromaSize.height() / sourceSize.height() - 0.5,
        0.0, static_cast<double>(chromaSize.height() - 1));
    const int x0 = static_cast<int>(std::floor(chromaX));
    const int y0 = static_cast<int>(std::floor(chromaY));
    const int x1 = std::min(x0 + 1, chromaSize.width() - 1);
    const int y1 = std::min(y0 + 1, chromaSize.height() - 1);
    const double fx = chromaX - x0;
    const double fy = chromaY - y0;
    const auto at = [&](int x, int y) {
        const auto value = accessor.yuvAtSourcePixel({x * 2, y * 2});
        return value ? std::array<double, 2>{static_cast<double>(value->u),
                                             static_cast<double>(value->v)}
                     : std::array<double, 2>{};
    };
    const auto topLeft = at(x0, y0);
    const auto topRight = at(x1, y0);
    const auto bottomLeft = at(x0, y1);
    const auto bottomRight = at(x1, y1);
    std::array<double, 2> result{};
    for (std::size_t channel = 0; channel < result.size(); ++channel) {
        const double top = topLeft[channel] * (1.0 - fx) + topRight[channel] * fx;
        const double bottom = bottomLeft[channel] * (1.0 - fx) + bottomRight[channel] * fx;
        result[channel] = top * (1.0 - fy) + bottom * fy;
    }
    return result;
}

std::array<double, 3> yuvRgb(const RawImageParameters& parameters, const RawPlaneAccessor& accessor,
                             const YuvPlaneSample& sample) {
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
    const auto chroma = interpolatedChromaAt(accessor, sample.sourcePixel);
    const double u = (chroma[0] - chromaCenter) / chromaScale;
    const double v = (chroma[1] - chromaCenter) / chromaScale;
    return {y + coefficients[0] * v, y - coefficients[1] * u - coefficients[2] * v,
            y + coefficients[3] * u};
}

QColor yuvDisplayColor(const RawImageParameters& parameters, const RawPlaneAccessor& accessor,
                       const YuvPlaneSample& sample) {
    const auto rgb = yuvRgb(parameters, accessor, sample);
    return QColor::fromRgb(toByte(rgb[0]), toByte(rgb[1]), toByte(rgb[2]));
}

// Demosaiced RAW values before display encoding: black level removal, white balance, and the
// colour correction matrix, kept in normalized units so callers can report them in RAW units.
std::array<double, 3> bayerLinearRgb(const RawImageParameters& parameters,
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
    std::array<double, 3> corrected{};
    for (std::size_t output = 0; output < corrected.size(); ++output) {
        double correctedValue = 0.0;
        for (std::size_t input = 0; input < balanced.size(); ++input) {
            correctedValue +=
                parameters.colorCorrectionMatrix[output * 3 + input] * balanced[input];
        }
        corrected[output] = std::clamp(correctedValue, 0.0, 1.0);
    }
    return corrected;
}

QColor bayerDisplayColor(const RawImageParameters& parameters,
                         const RawPlaneAccessor& accessor,
                         const BayerPlaneSample& center) {
    if (!parameters.demosaic) {
        const int white = parameters.whiteLevel > parameters.blackLevel
                              ? parameters.whiteLevel
                              : parameters.maximumSampleValue();
        const auto sample = accessor.bayerAtSourcePixel(center.sourcePixel);
        const double normalized =
            sample && white > parameters.blackLevel
                ? std::clamp((sample->value - parameters.blackLevel) /
                                 static_cast<double>(white - parameters.blackLevel),
                             0.0, 1.0)
                : 0.0;
        const int gray = toByte(std::pow(normalized, 1.0 / parameters.displayGamma));
        return QColor::fromRgb(gray, gray, gray);
    }
    const auto corrected = bayerLinearRgb(parameters, accessor, center);
    const auto encoded = [&parameters](double value) {
        return toByte(std::pow(value, 1.0 / parameters.displayGamma));
    };
    return QColor::fromRgb(encoded(corrected[0]), encoded(corrected[1]), encoded(corrected[2]));
}

} // namespace

QString ComparisonPixelSample::sourceValueText() const {
    if (sourceSamplesPending) {
        // Mirrors the gallery probe: the exact samples need the full decode that the request
        // this sample triggered will deliver.
        return QStringLiteral("Loading pixel data…");
    }
    if (yuv) {
        QString text = QStringLiteral("YUV(%1,%2,%3)").arg(yuv->y).arg(yuv->u).arg(yuv->v);
        // The converted RGB accompanies the source samples, in the same bit depth, so an
        // operator can read the YUV triple and the resulting colour value together.
        if (!sourceRgbText.isEmpty()) {
            text += QStringLiteral(" · ") + sourceRgbText;
        }
        return text;
    }
    if (bayer) {
        // Without demosaicing only the CFA sample at this position is meaningful. Demosaiced
        // frames report the pipeline RGB in the RAW container's own value range.
        if (sourceRgbText.isEmpty()) {
            return QStringLiteral("RAW(%1)").arg(bayer->value);
        }
        return sourceRgbText;
    }
    return encodedValueText.isEmpty() ? rgbText(displayColor) : encodedValueText;
}

QString ComparisonPixelSample::displayValueText() const { return rgbText(displayColor); }

QSize ComparisonPixelProbe::logicalFrameSize(const ImageFrame& frame) {
    // Mirror of QmlImageCanvas' zoom/pan space: RAW/YUV planes render through the oriented source
    // size, while QImage backed frames (encoded images, bounded RAW/YUV previews, camera RAW)
    // render at the decoded size they carry.
    if (frame.rawParameters) {
        const QSize oriented =
            orientedImageSize(frame.rawParameters->size, frame.rawParameters->orientation);
        if (oriented.isValid()) {
            return oriented;
        }
    }
    if (!frame.descriptor.size.isEmpty()) {
        return frame.descriptor.size;
    }
    const QImage* display = frame.qImage();
    return display ? display->size() : QSize{};
}

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
    return sampleAtDisplayPixel(frame, displayPixel);
}

ComparisonPixelSample ComparisonPixelProbe::sampleAtLogicalPixel(const ImageFrame& frame,
                                                                  const QPoint& logicalPixel,
                                                                  const QSize& logicalSize) {
    const RawPlaneAccessor rawAccessor(frame);
    const QSize samplingSize =
        rawAccessor.isValid() ? rawAccessor.displaySize() : frame.descriptor.size;
    if (samplingSize.isEmpty() || logicalSize.isEmpty()) {
        return {};
    }
    if (samplingSize == logicalSize) {
        return sampleAtDisplayPixel(frame, logicalPixel);
    }
    // A bounded RAW/YUV preview keeps the full source size as its zoom/pan space while the probe
    // can only read the decoded proxy, so the hovered pixel maps proportionally instead of being
    // rejected as out of range.
    const QPoint samplingPixel(
        std::clamp(static_cast<int>((logicalPixel.x() + 0.5) * samplingSize.width() /
                                    logicalSize.width()),
                   0, samplingSize.width() - 1),
        std::clamp(static_cast<int>((logicalPixel.y() + 0.5) * samplingSize.height() /
                                    logicalSize.height()),
                   0, samplingSize.height() - 1));
    ComparisonPixelSample result = sampleAtDisplayPixel(frame, samplingPixel);
    if (result.valid) {
        // The caller reported a coordinate in its own zoom/pan space, so it stays the pixel of
        // record while sourcePixel keeps addressing the stored plane.
        result.displayPixel = logicalPixel;
    }
    return result;
}

ComparisonPixelSample ComparisonPixelProbe::sampleAtDisplayPixel(const ImageFrame& frame,
                                                                  const QPoint& displayPixel) {
    const RawPlaneAccessor rawAccessor(frame);
    const QSize logicalSize =
        rawAccessor.isValid() ? rawAccessor.displaySize() : frame.descriptor.size;
    if (!QRect(QPoint{}, logicalSize).contains(displayPixel)) {
        return {};
    }

    ComparisonPixelSample result;
    result.valid = true;
    result.displayPixel = displayPixel;
    result.sourcePixel = displayPixel;
    result.sourceSamplesPending = frame.sourceSamplesPending;
    if (rawAccessor.isValid()) {
        if (rawAccessor.isYuv()) {
            result.yuv = rawAccessor.yuvAtDisplayPixel(displayPixel);
            if (result.yuv) {
                result.sourcePixel = result.yuv->sourcePixel;
                result.displayColor =
                    yuvDisplayColor(*frame.rawParameters, rawAccessor, *result.yuv);
                result.sourceRgbText = sourceRgbValueText(
                    yuvRgb(*frame.rawParameters, rawAccessor, *result.yuv),
                    sourceSampleMaximum(*frame.rawParameters));
            }
        } else {
            result.bayer = rawAccessor.bayerAtDisplayPixel(displayPixel);
            if (result.bayer) {
                result.sourcePixel = result.bayer->sourcePixel;
                result.displayColor =
                    bayerDisplayColor(*frame.rawParameters, rawAccessor, *result.bayer);
                if (frame.rawParameters->demosaic) {
                    result.sourceRgbText = sourceRgbValueText(
                        bayerLinearRgb(*frame.rawParameters, rawAccessor, *result.bayer),
                        sourceSampleMaximum(*frame.rawParameters));
                }
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
        result.encodedValueText =
            encodedRgbText(*display, sampleX, sampleY, result.displayColor);
    }
    return result;
}

} // namespace ispview
