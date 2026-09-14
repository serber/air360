#pragma once
#include <cstdint>
inline std::int64_t esp_timer_get_time() {
    static std::int64_t now = 0;
    return now += 1000;
}
