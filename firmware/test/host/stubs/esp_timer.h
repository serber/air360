#pragma once
#include <atomic>
#include <cstdint>

// Controllable host clock. By default every call advances time by
// auto_advance_us (1 ms), which keeps legacy tests that only need a
// monotonically increasing clock working. Tests that need deterministic time
// set auto_advance_us = 0 and move now_us explicitly.
namespace esp_timer_test {
inline std::atomic<std::int64_t> now_us{0};
inline std::atomic<std::int64_t> auto_advance_us{1000};
inline void advanceMs(std::int64_t ms) { now_us += ms * 1000; }
}

inline std::int64_t esp_timer_get_time() {
    return esp_timer_test::now_us += esp_timer_test::auto_advance_us.load();
}
