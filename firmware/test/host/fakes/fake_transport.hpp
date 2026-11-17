#pragma once
#include <atomic>

namespace fake_transport {
inline std::atomic<int> i2c_init_calls{0};
inline std::atomic<int> uart_shutdown_calls{0};
inline std::atomic<int> uart_ports_released{0};
}  // namespace fake_transport
