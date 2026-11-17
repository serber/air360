#pragma once
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

namespace fake_transport {
inline std::atomic<int> i2c_init_calls{0};
inline std::atomic<int> uart_shutdown_calls{0};
inline std::atomic<int> uart_ports_released{0};

// Scripted UART: bytes queued in rx are returned by UartPortManager::read()
// (all buffered ports share one stream), every write() is logged, and
// pending_overruns is reported once by the next drainEvents().
inline std::mutex uart_mutex;
inline std::deque<std::uint8_t> rx;
inline std::vector<std::vector<std::uint8_t>> writes;
inline std::uint32_t pending_overruns = 0U;

inline void queueRx(const std::vector<std::uint8_t>& bytes) {
    std::lock_guard<std::mutex> guard(uart_mutex);
    rx.insert(rx.end(), bytes.begin(), bytes.end());
}

inline void resetUart() {
    std::lock_guard<std::mutex> guard(uart_mutex);
    rx.clear();
    writes.clear();
    pending_overruns = 0U;
}
}  // namespace fake_transport
