#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "air360/sensors/sensor_config.hpp"
#include "air360/sensors/sensor_driver.hpp"
#include "air360/sensors/sensor_registry.hpp"
#include "air360/sensors/transport_binding.hpp"
#include "air360/uploads/measurement_store.hpp"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
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
    std::string binding_summary;
    std::uint32_t poll_interval_ms = 0U;
    SensorRuntimeState state = SensorRuntimeState::kUnsupported;
    std::uint32_t failures = 0U;
    std::uint32_t soft_fails = 0U;
    std::uint64_t next_retry_ms = 0U;
    std::string last_error;
};

class SensorManager {
  public:
    SensorManager();
    SensorManager(const SensorManager&) = delete;
    SensorManager& operator=(const SensorManager&) = delete;
    SensorManager(SensorManager&&) = delete;
    SensorManager& operator=(SensorManager&&) = delete;

    void setMeasurementStore(MeasurementStore& measurement_store);
    [[nodiscard]] esp_err_t applyConfig(const SensorConfigList& config);
    [[nodiscard]] esp_err_t stop();

    std::vector<SensorRuntimeInfo> sensors() const;
    std::size_t configuredCount() const;
    std::size_t enabledCount() const;
    std::size_t taskStackHighWaterMarkBytes() const;

  private:
    struct ManagedSensor {
        SensorRecord record{};
        const SensorDescriptor* descriptor = nullptr;
        std::unique_ptr<SensorDriver> driver;
        SensorRuntimeInfo runtime{};
        bool driver_ready = false;
        std::uint32_t consecutive_poll_failures = 0U;
        std::uint64_t next_init_allowed_ms = 0U;
        std::uint64_t next_action_time_ms = 0U;
    };

    void lock() const;
    void unlock() const;
    std::vector<ManagedSensor> buildManagedSensors(const SensorConfigList& config);
    esp_err_t startLocked();
    bool stopRequested() const;
    static void taskEntry(void* arg);
    void taskMain();

    static constexpr EventBits_t kTaskStoppedBit = BIT0;

    mutable StaticSemaphore_t mutex_buffer_{};
    mutable SemaphoreHandle_t mutex_ = nullptr;
    StaticEventGroup_t lifecycle_events_buffer_{};
    EventGroupHandle_t lifecycle_events_ = nullptr;
    TaskHandle_t task_ = nullptr;
    std::atomic_bool stop_requested_{false};
    std::vector<ManagedSensor> sensors_;
    I2cBusManager i2c_bus_manager_;
    UartPortManager uart_port_manager_;
    MeasurementStore* measurement_store_ = nullptr;
};

}  // namespace air360
