#pragma once

// Test control surface for fakes/sensor_registry_fake.cpp. The fake registry
// knows two sensor types:
//   - SensorType::kSht4x   (I2C, kAfterNetwork)
//   - SensorType::kIna219  (I2C, kBeforeNetwork)
// Both create FakeSensorDriver instances whose behaviour is scripted per
// sensor id through fake_sensors::behaviour(id).

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>

#include "esp_err.h"

namespace fake_sensors {

struct Behaviour {
    std::atomic<int> init_calls{0};
    std::atomic<int> poll_calls{0};
    std::atomic<int> drivers_created{0};
    std::atomic<int> drivers_destroyed{0};
    std::atomic<esp_err_t> init_result{ESP_OK};
    std::atomic<esp_err_t> poll_result{ESP_OK};
    // When true, every successful poll produces a new sample (new
    // sample_time_ms); when false the cached sample is returned unchanged.
    std::atomic<bool> fresh_samples{true};
    // Optional delay inside poll() to model a slow bus transaction.
    std::atomic<int> poll_delay_ms{0};
};

inline std::mutex table_mutex;
inline std::map<std::uint32_t, std::unique_ptr<Behaviour>> table;

inline Behaviour& behaviour(std::uint32_t sensor_id) {
    std::lock_guard<std::mutex> guard(table_mutex);
    auto& slot = table[sensor_id];
    if (!slot) {
        slot = std::make_unique<Behaviour>();
    }
    return *slot;
}

inline void reset() {
    std::lock_guard<std::mutex> guard(table_mutex);
    table.clear();
}

}  // namespace fake_sensors
