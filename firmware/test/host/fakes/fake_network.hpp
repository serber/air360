#pragma once

#include <atomic>

// Test control for fakes/network_manager_fake.cpp.
namespace fake_network {
inline std::atomic<bool> uplink_ready{true};
}  // namespace fake_network
