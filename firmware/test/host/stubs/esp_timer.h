#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>

// Controllable host clock.
// - Default (call-driven): every call advances time by auto_advance_us
//   (1 ms), which keeps legacy single-threaded tests working.
// - real_time = true: time follows std::chrono::steady_clock plus now_us as an
//   offset, so threaded tests see time pass while advanceMs() can still jump
//   ahead (e.g. past warmup windows or retry backoffs).
namespace esp_timer_test {
inline std::atomic<std::int64_t> now_us{0};
inline std::atomic<std::int64_t> auto_advance_us{1000};
inline std::atomic<bool> real_time{false};
inline void advanceMs(std::int64_t ms) { now_us += ms * 1000; }
}

inline std::int64_t esp_timer_get_time() {
    if (esp_timer_test::real_time.load()) {
        const auto since_epoch = std::chrono::steady_clock::now().time_since_epoch();
        return std::chrono::duration_cast<std::chrono::microseconds>(since_epoch).count() +
               esp_timer_test::now_us.load();
    }
    return esp_timer_test::now_us += esp_timer_test::auto_advance_us.load();
}
