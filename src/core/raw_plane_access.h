#pragma once

#include "core/image_types.h"

#include <QPoint>
#include <QSize>
#include <QString>

#include <optional>

namespace ispview {

enum class BayerSampleChannel { Red, GreenRedRow, GreenBlueRow, Blue };

struct YuvPlaneSample {
    quint16 y = 0;
    quint16 u = 0;
    quint16 v = 0;
    QPoint sourcePixel;
};

struct BayerPlaneSample {
    quint16 value = 0;
    BayerSampleChannel channel = BayerSampleChannel::GreenRedRow;
    QPoint sourcePixel;
};

// Reads engineering values from an immutable Full RAW/YUV ImageFrame. Coordinates passed to
// the display methods use the oriented logical image; source methods address the stored sensor
// plane directly. The accessor never reads the bounded displayImage proxy.
class RawPlaneAccessor final {
  public:
    explicit RawPlaneAccessor(const ImageFrame& frame);

    [[nodiscard]] bool isValid() const { return valid_; }
    [[nodiscard]] bool isYuv() const { return parameters_ && parameters_->isYuv(); }
    [[nodiscard]] QSize sourceSize() const;
    [[nodiscard]] QSize displaySize() const;
    [[nodiscard]] int validBits() const;
    [[nodiscard]] int maximumSampleValue() const;

    [[nodiscard]] std::optional<YuvPlaneSample>
    yuvAtDisplayPixel(const QPoint& displayPixel) const;
    [[nodiscard]] std::optional<YuvPlaneSample>
    yuvAtSourcePixel(const QPoint& sourcePixel) const;
    [[nodiscard]] std::optional<BayerPlaneSample>
    bayerAtDisplayPixel(const QPoint& displayPixel) const;
    [[nodiscard]] std::optional<BayerPlaneSample>
    bayerAtSourcePixel(const QPoint& sourcePixel) const;

    [[nodiscard]] QString pixelDescriptionAtDisplayPixel(const QPoint& displayPixel) const;

    [[nodiscard]] static BayerSampleChannel
    channelAtSourcePixel(BayerPattern pattern, const QPoint& sourcePixel,
                         BayerSampling sampling = BayerSampling::Standard2x2);
    [[nodiscard]] static QString channelName(BayerSampleChannel channel);

  private:
    [[nodiscard]] bool validateStorage() const;
    [[nodiscard]] bool validatePlane(int index, int rows, qsizetype minimumRowBytes) const;
    [[nodiscard]] std::optional<quint16> readPlaneSample(int planeIndex, int sampleX,
                                                        int sampleY,
                                                        int bytesPerSample) const;
    [[nodiscard]] std::optional<quint16>
    readBayerValue(const QPoint& sourcePixel) const;

    const RawImageParameters* parameters_ = nullptr;
    const PlaneBufferSet* storage_ = nullptr;
    bool valid_ = false;
};

} // namespace ispview
