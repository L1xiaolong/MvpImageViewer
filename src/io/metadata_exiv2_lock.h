#pragma once

#include <mutex>

namespace mvpview {

inline std::mutex& exiv2MetadataMutex() {
    static std::mutex mutex;
    return mutex;
}

} // namespace mvpview
