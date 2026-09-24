#pragma once

#include <mutex>

namespace ispview {

inline std::mutex& exiv2MetadataMutex() {
    static std::mutex mutex;
    return mutex;
}

} // namespace ispview
