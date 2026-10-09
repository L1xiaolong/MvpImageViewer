#pragma once

#include <QSize>
#include <limits>
#include <initializer_list>

namespace mvpview {

inline qsizetype nominalGpuBytes(const QSize& size, int bytesPerPixel, int samples = 1) {
    if (size.isEmpty() || bytesPerPixel <= 0 || samples <= 0) return 0;
    qsizetype bytes = 1;
    for (const qsizetype factor : {qsizetype(size.width()), qsizetype(size.height()),
                                  qsizetype(bytesPerPixel), qsizetype(samples)}) {
        if (bytes > std::numeric_limits<qsizetype>::max() / factor)
            return std::numeric_limits<qsizetype>::max();
        bytes *= factor;
    }
    return bytes;
}

} // namespace mvpview
