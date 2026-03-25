#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "air360/sensors/sensor_config.hpp"
#include "air360/sensors/sensor_driver.hpp"
#include "air360/sensors/sensor_registry.hpp"
#include "air360/sensors/transport_binding.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace air360 {

struct SensorRuntimeInfo {
    std::uint32_t id = 0U;
    bool enabled = false;
    SensorType sensor_type = SensorType::kUnknown;
    TransportKind transport_kind = TransportKind::kUnknown;
    std::string type_key;
    std::string type_name;
    std::string display_name;
    std::string binding_summary;
    SensorRuntimeState state = SensorRuntimeState::kUnsupported;
    SensorMeasurement measurement{};
    std::uint64_t last_sample_time_ms = 0U;
    std::string last_error;
};

class SensorManager {
  public:
    void applyConfig(const SensorConfigList& config);
    void stop();

    std::vector<SensorRuntimeInfo> sensors() const;
    std::size_t configuredCount() const;
    std::size_t enabledCount() const;

  private:
    struct ManagedSensor {
        SensorRecord record{};
        const SensorDescriptor* descriptor = nullptr;
        std::unique_ptr<SensorDriver> driver;
        SensorRuntimeInfo runtime{};
        bool driver_ready = false;
        std::uint64_t next_action_time_ms = 0U;
    };

    void ensureMutex() const;
    void lock() const;
    void unlock() const;
    std::vector<ManagedSensor> buildManagedSensors(const SensorConfigList& config);
    void startLocked();
    static void taskEntry(void* arg);
    void taskMain();

    mutable StaticSemaphore_t mutex_buffer_{};
    mutable SemaphoreHandle_t mutex_ = nullptr;
    TaskHandle_t task_ = nullptr;
    bool stop_requested_ = false;
    std::vector<ManagedSensor> sensors_;
    I2cBusManager i2c_bus_manager_;
};

}  // namespace air360
