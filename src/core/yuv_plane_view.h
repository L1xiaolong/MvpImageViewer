#pragma once

#include "core/raw_plane_access.h"

#include <QtEndian>

#include <array>

namespace mvpview::detail {

// Bulk analysis only: callers validate the frame with RawPlaneAccessor once,
// then visit in-bounds source/chroma coordinates. The frame owns these pointers.
class YuvPlaneView {
  public:
    explicit YuvPlaneView(const ImageFrame& frame)
        : parameters_(*frame.rawParameters),
          storage_(*std::get<std::shared_ptr<const PlaneBufferSet>>(frame.storage)),
          highBitDepth_(parameters_.format == RawPixelFormat::P010) {
        Q_ASSERT(RawPlaneAccessor(frame).isValid() && parameters_.isYuv());
    }
    int luma(int x, int y) const { return sample(0, x, y); }
    std::array<int, 2> chroma(int x, int y) const {
        if (parameters_.format == RawPixelFormat::I420) return {sample(1, x, y), sample(2, x, y)};
        const int a = sample(1, x * 2, y);
        const int b = sample(1, x * 2 + 1, y);
        return parameters_.format == RawPixelFormat::NV21 ? std::array<int, 2>{b, a}
                                                         : std::array<int, 2>{a, b};
    }

  private:
    int sample(int index, int x, int y) const {
        const auto& plane = storage_.planes.at(index);
        const char* data = storage_.storage.constData() + plane.offset +
                           qsizetype(y) * plane.stride + qsizetype(x) * (highBitDepth_ ? 2 : 1);
        if (!highBitDepth_) return static_cast<uchar>(*data);
        const auto value = parameters_.littleEndian ? qFromLittleEndian<quint16>(data)
                                                   : qFromBigEndian<quint16>(data);
        return parameters_.msbAligned ? value >> 6 : value & 0x03ff;
    }
    const RawImageParameters& parameters_;
    const PlaneBufferSet& storage_;
    bool highBitDepth_;
};
} // namespace mvpview::detail
