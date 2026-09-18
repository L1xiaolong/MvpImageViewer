#include "core/raw_plane_access.h"

#include <QtEndian>

#include <algorithm>
#include <limits>

namespace ispview {
namespace {

bool checkedPlaneEnd(const PlaneBuffer& plane, qsizetype storageSize, qsizetype& end) {
    if (plane.offset < 0 || plane.stride <= 0 || plane.byteSize < 0 ||
        plane.offset > storageSize || plane.byteSize > storageSize - plane.offset) {
        return false;
    }
    end = plane.offset + plane.byteSize;
    return true;
}

quint16 read16(const char* bytes, bool littleEndian) {
    const auto* source = reinterpret_cast<const uchar*>(bytes);
    return littleEndian ? qFromLittleEndian<quint16>(source)
                        : qFromBigEndian<quint16>(source);
}

} // namespace

RawPlaneAccessor::RawPlaneAccessor(const ImageFrame& frame) {
    if (!frame.rawParameters) {
        return;
    }
    const auto* planes = std::get_if<std::shared_ptr<const PlaneBufferSet>>(&frame.storage);
    if (!planes || !*planes) {
        return;
    }
    parameters_ = &*frame.rawParameters;
    storage_ = planes->get();
    valid_ = validateStorage();
}

QSize RawPlaneAccessor::sourceSize() const {
    return parameters_ ? parameters_->size : QSize{};
}

QSize RawPlaneAccessor::displaySize() const {
    return parameters_ ? orientedImageSize(parameters_->size, parameters_->orientation) : QSize{};
}

int RawPlaneAccessor::validBits() const {
    return parameters_ ? parameters_->validBits() : 0;
}

int RawPlaneAccessor::maximumSampleValue() const {
    return parameters_ ? parameters_->maximumSampleValue() : 0;
}

std::optional<YuvPlaneSample>
RawPlaneAccessor::yuvAtDisplayPixel(const QPoint& displayPixel) const {
    if (!valid_ || !isYuv() || !QRect(QPoint{}, displaySize()).contains(displayPixel)) {
        return std::nullopt;
    }
    return yuvAtSourcePixel(
        displayToSourcePixel(displayPixel, parameters_->size, parameters_->orientation));
}

std::optional<YuvPlaneSample>
RawPlaneAccessor::yuvAtSourcePixel(const QPoint& sourcePixel) const {
    if (!valid_ || !isYuv() || !QRect(QPoint{}, sourceSize()).contains(sourcePixel)) {
        return std::nullopt;
    }
    const int bytesPerSample = parameters_->format == RawPixelFormat::P010 ? 2 : 1;
    const auto y = readPlaneSample(0, sourcePixel.x(), sourcePixel.y(), bytesPerSample);
    const int chromaX = sourcePixel.x() / 2;
    const int chromaY = sourcePixel.y() / 2;
    std::optional<quint16> u;
    std::optional<quint16> v;
    if (parameters_->format == RawPixelFormat::I420) {
        u = readPlaneSample(1, chromaX, chromaY, 1);
        v = readPlaneSample(2, chromaX, chromaY, 1);
    } else {
        const auto first = readPlaneSample(1, chromaX * 2, chromaY, bytesPerSample);
        const auto second = readPlaneSample(1, chromaX * 2 + 1, chromaY, bytesPerSample);
        if (parameters_->format == RawPixelFormat::NV21) {
            u = second;
            v = first;
        } else {
            u = first;
            v = second;
        }
    }
    if (!y || !u || !v) {
        return std::nullopt;
    }
    return YuvPlaneSample{*y, *u, *v, sourcePixel};
}

std::optional<BayerPlaneSample>
RawPlaneAccessor::bayerAtDisplayPixel(const QPoint& displayPixel) const {
    if (!valid_ || isYuv() || !QRect(QPoint{}, displaySize()).contains(displayPixel)) {
        return std::nullopt;
    }
    return bayerAtSourcePixel(
        displayToSourcePixel(displayPixel, parameters_->size, parameters_->orientation));
}

std::optional<BayerPlaneSample>
RawPlaneAccessor::bayerAtSourcePixel(const QPoint& sourcePixel) const {
    if (!valid_ || isYuv() || !QRect(QPoint{}, sourceSize()).contains(sourcePixel)) {
        return std::nullopt;
    }
    const auto value = readBayerValue(sourcePixel);
    if (!value) {
        return std::nullopt;
    }
    return BayerPlaneSample{*value,
                            channelAtSourcePixel(parameters_->bayerPattern, sourcePixel,
                                                 parameters_->bayerSampling),
                            sourcePixel};
}

QString RawPlaneAccessor::pixelDescriptionAtDisplayPixel(const QPoint& displayPixel) const {
    if (isYuv()) {
        const auto sample = yuvAtDisplayPixel(displayPixel);
        return sample ? QStringLiteral("YUV(%1,%2,%3)").arg(sample->y).arg(sample->u).arg(sample->v)
                      : QString{};
    }
    const auto sample = bayerAtDisplayPixel(displayPixel);
    if (!sample) {
        return {};
    }
    switch (sample->channel) {
    case BayerSampleChannel::Red:
        return QStringLiteral("RGB(%1,0,0)").arg(sample->value);
    case BayerSampleChannel::GreenRedRow:
    case BayerSampleChannel::GreenBlueRow:
        return QStringLiteral("RGB(0,%1,0)").arg(sample->value);
    case BayerSampleChannel::Blue:
        return QStringLiteral("RGB(0,0,%1)").arg(sample->value);
    }
    return {};
}

BayerSampleChannel RawPlaneAccessor::channelAtSourcePixel(BayerPattern pattern,
                                                           const QPoint& sourcePixel,
                                                           BayerSampling sampling) {
    const int blockSize = bayerSampleBlockSize(sampling);
    const bool evenX = ((sourcePixel.x() / blockSize) & 1) == 0;
    const bool evenY = ((sourcePixel.y() / blockSize) & 1) == 0;
    switch (pattern) {
    case BayerPattern::RGGB:
        return evenY ? (evenX ? BayerSampleChannel::Red
                              : BayerSampleChannel::GreenRedRow)
                     : (evenX ? BayerSampleChannel::GreenBlueRow
                              : BayerSampleChannel::Blue);
    case BayerPattern::GRBG:
        return evenY ? (evenX ? BayerSampleChannel::GreenRedRow
                              : BayerSampleChannel::Red)
                     : (evenX ? BayerSampleChannel::Blue
                              : BayerSampleChannel::GreenBlueRow);
    case BayerPattern::GBRG:
        return evenY ? (evenX ? BayerSampleChannel::GreenBlueRow
                              : BayerSampleChannel::Blue)
                     : (evenX ? BayerSampleChannel::Red
                              : BayerSampleChannel::GreenRedRow);
    case BayerPattern::BGGR:
        return evenY ? (evenX ? BayerSampleChannel::Blue
                              : BayerSampleChannel::GreenBlueRow)
                     : (evenX ? BayerSampleChannel::GreenRedRow
                              : BayerSampleChannel::Red);
    }
    return BayerSampleChannel::GreenRedRow;
}

QString RawPlaneAccessor::channelName(BayerSampleChannel channel) {
    switch (channel) {
    case BayerSampleChannel::Red:
        return QStringLiteral("R");
    case BayerSampleChannel::GreenRedRow:
        return QStringLiteral("Gr");
    case BayerSampleChannel::GreenBlueRow:
        return QStringLiteral("Gb");
    case BayerSampleChannel::Blue:
        return QStringLiteral("B");
    }
    return {};
}

bool RawPlaneAccessor::validateStorage() const {
    if (!parameters_ || !storage_ || parameters_->size.isEmpty() ||
        !parameters_->hasValidBitLayout() || !parameters_->hasValidOrientation()) {
        return false;
    }
    const int width = parameters_->size.width();
    const int height = parameters_->size.height();
    if (!parameters_->isYuv()) {
        return validatePlane(0, height, minimumRowStride(*parameters_));
    }
    const int bytesPerSample = parameters_->format == RawPixelFormat::P010 ? 2 : 1;
    if (!validatePlane(0, height, static_cast<qsizetype>(width) * bytesPerSample)) {
        return false;
    }
    const int chromaHeight = (height + 1) / 2;
    const qsizetype chromaWidth = (static_cast<qsizetype>(width) + 1) / 2;
    if (parameters_->format == RawPixelFormat::I420) {
        return validatePlane(1, chromaHeight, chromaWidth) &&
               validatePlane(2, chromaHeight, chromaWidth);
    }
    return validatePlane(1, chromaHeight, chromaWidth * bytesPerSample * 2);
}

bool RawPlaneAccessor::validatePlane(int index, int rows, qsizetype minimumRowBytes) const {
    if (!storage_ || index < 0 || index >= storage_->planes.size() || rows <= 0 ||
        minimumRowBytes <= 0) {
        return false;
    }
    const PlaneBuffer& plane = storage_->planes.at(index);
    qsizetype planeEnd = 0;
    if (!checkedPlaneEnd(plane, storage_->storage.size(), planeEnd) ||
        plane.stride < minimumRowBytes || rows > plane.byteSize / plane.stride) {
        return false;
    }
    const qsizetype required = static_cast<qsizetype>(rows - 1) * plane.stride + minimumRowBytes;
    return required <= plane.byteSize && plane.offset + required <= planeEnd;
}

std::optional<quint16> RawPlaneAccessor::readPlaneSample(int planeIndex, int sampleX,
                                                         int sampleY,
                                                         int bytesPerSample) const {
    if (!storage_ || planeIndex < 0 || planeIndex >= storage_->planes.size() || sampleX < 0 ||
        sampleY < 0 || (bytesPerSample != 1 && bytesPerSample != 2)) {
        return std::nullopt;
    }
    const PlaneBuffer& plane = storage_->planes.at(planeIndex);
    const qsizetype column = static_cast<qsizetype>(sampleX) * bytesPerSample;
    if (column > plane.stride - bytesPerSample ||
        sampleY > (std::numeric_limits<qsizetype>::max() - column) / plane.stride) {
        return std::nullopt;
    }
    const qsizetype relative = static_cast<qsizetype>(sampleY) * plane.stride + column;
    if (relative < 0 || relative > plane.byteSize - bytesPerSample ||
        plane.offset > storage_->storage.size() - relative - bytesPerSample) {
        return std::nullopt;
    }
    const char* bytes = storage_->storage.constData() + plane.offset + relative;
    if (bytesPerSample == 1) {
        return static_cast<quint8>(*reinterpret_cast<const uchar*>(bytes));
    }
    const quint16 stored = read16(bytes, parameters_->littleEndian);
    return parameters_->msbAligned ? static_cast<quint16>(stored >> 6)
                                   : static_cast<quint16>(stored & 0x03FF);
}

std::optional<quint16>
RawPlaneAccessor::readBayerValue(const QPoint& sourcePixel) const {
    if (!storage_ || storage_->planes.isEmpty()) {
        return std::nullopt;
    }
    const PlaneBuffer& plane = storage_->planes.constFirst();
    const qsizetype rowOffset = static_cast<qsizetype>(sourcePixel.y()) * plane.stride;
    qsizetype column = 0;
    int requiredBytes = 0;
    int lane = 0;
    switch (parameters_->format) {
    case RawPixelFormat::MipiRaw10:
        column = static_cast<qsizetype>(sourcePixel.x() / 4) * 5;
        requiredBytes = 5;
        lane = sourcePixel.x() % 4;
        break;
    case RawPixelFormat::MipiRaw12:
        column = static_cast<qsizetype>(sourcePixel.x() / 2) * 3;
        requiredBytes = 3;
        lane = sourcePixel.x() % 2;
        break;
    case RawPixelFormat::Raw16:
        column = static_cast<qsizetype>(sourcePixel.x()) * 2;
        requiredBytes = 2;
        break;
    default:
        return std::nullopt;
    }
    if (column > plane.stride - requiredBytes || rowOffset > plane.byteSize - requiredBytes ||
        column > plane.byteSize - rowOffset - requiredBytes) {
        return std::nullopt;
    }
    const qsizetype relative = rowOffset + column;
    if (plane.offset > storage_->storage.size() - relative - requiredBytes) {
        return std::nullopt;
    }
    const auto* group = reinterpret_cast<const uchar*>(storage_->storage.constData() +
                                                        plane.offset + relative);
    if (parameters_->format == RawPixelFormat::MipiRaw10) {
        return static_cast<quint16>((group[lane] << 2) |
                                    ((group[4] >> (lane * 2)) & 0x03));
    }
    if (parameters_->format == RawPixelFormat::MipiRaw12) {
        return static_cast<quint16>((group[lane] << 4) |
                                    ((group[2] >> (lane * 4)) & 0x0F));
    }
    quint16 value = read16(reinterpret_cast<const char*>(group), parameters_->littleEndian);
    if (parameters_->validBits() < 16) {
        value = parameters_->msbAligned
                    ? static_cast<quint16>(value >> (16 - parameters_->validBits()))
                    : static_cast<quint16>(value & ((1U << parameters_->validBits()) - 1U));
    }
    return value;
}

} // namespace ispview
