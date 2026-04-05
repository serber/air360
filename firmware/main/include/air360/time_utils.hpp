#pragma once

#include <cstdint>
#include <ctime>
#include <sys/time.h>

#include "esp_timer.h"

namespace air360 {

constexpr std::time_t kMinValidUnixTimeSeconds = 1700000000;

inline std::uint64_t uptimeMilliseconds() {
    return static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
}

inline bool hasValidUnixTime() {
    struct timeval tv {};
    gettimeofday(&tv, nullptr);
    return tv.tv_sec >= kMinValidUnixTimeSeconds;
}

inline std::int64_t currentUnixMilliseconds() {
    struct timeval tv {};
    gettimeofday(&tv, nullptr);
    if (tv.tv_sec < kMinValidUnixTimeSeconds) {
        return 0;
    }

    return static_cast<std::int64_t>(tv.tv_sec) * 1000LL +
           static_cast<std::int64_t>(tv.tv_usec / 1000);
}

}  // namespace air360
