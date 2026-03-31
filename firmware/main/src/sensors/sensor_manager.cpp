#include "air360/sensors/sensor_manager.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <string>
#include <utility>

#include "air360/time_utils.hpp"
#include "esp_err.h"
#include "esp_log.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.sensor";
constexpr std::uint64_t kRetryDelayMs = 5000U;
constexpr TickType_t kManagerLoopDelay = pdMS_TO_TICKS(250);
constexpr uint32_t kManagerTaskStackSize = 6144U;
constexpr UBaseType_t kManagerTaskPriority = 5U;

SensorRuntimeState classifyFailureState(esp_err_t err) {
    if (err == ESP_ERR_NOT_FOUND ||
        err == ESP_ERR_INVALID_RESPONSE ||
        err == ESP_ERR_TIMEOUT) {
        return SensorRuntimeState::kAbsent;
    }

    return SensorRuntimeState::kError;
}

std::string errorText(const SensorDriver& driver, esp_err_t err) {
    const std::string driver_error = driver.lastError();
    if (!driver_error.empty()) {
        return driver_error;
    }

    return esp_err_to_name(err);
}

std::string defaultDisplayName(const SensorDescriptor* descriptor, std::uint32_t id) {
    if (descriptor != nullptr && descriptor->display_name != nullptr) {
        return std::string(descriptor->display_name) + " #" + std::to_string(id);
    }

    return std::string("Sensor #") + std::to_string(id);
}

std::string bindingSummary(const SensorRecord& record) {
    char buffer[64];
    switch (record.transport_kind) {
        case TransportKind::kI2c:
            std::snprintf(
                buffer,
                sizeof(buffer),
                "i2c%u @ 0x%02x",
                static_cast<unsigned>(record.i2c_bus_id),
                static_cast<unsigned>(record.i2c_address));
            return buffer;
        case TransportKind::kAnalog:
            std::snprintf(
                buffer,
                sizeof(buffer),
                "GPIO %d",
                static_cast<int>(record.analog_gpio_pin));
            return buffer;
        case TransportKind::kGpio:
            std::snprintf(
                buffer,
                sizeof(buffer),
                "GPIO %d",
                static_cast<int>(record.analog_gpio_pin));
            return buffer;
        case TransportKind::kUart:
            std::snprintf(
                buffer,
                sizeof(buffer),
                "uart%u RX%d TX%d @ %u",
                static_cast<unsigned>(record.uart_port_id),
                static_cast<int>(record.uart_rx_gpio_pin),
                static_cast<int>(record.uart_tx_gpio_pin),
                static_cast<unsigned>(record.uart_baud_rate));
            return buffer;
        case TransportKind::kUnknown:
        default:
            return "unbound";
    }
}

}  // namespace

void SensorManager::setMeasurementStore(MeasurementStore& measurement_store) {
    measurement_store_ = &measurement_store;
}

void SensorManager::applyConfig(const SensorConfigList& config) {
    stop();
    ensureMutex();

    std::vector<ManagedSensor> next_sensors = buildManagedSensors(config);

    lock();
    sensors_ = std::move(next_sensors);
    startLocked();
    unlock();
}

std::vector<SensorManager::ManagedSensor> SensorManager::buildManagedSensors(
    const SensorConfigList& config) {
    SensorRegistry registry;
    const SensorDriverContext driver_context{&i2c_bus_manager_, &uart_port_manager_};
    const std::uint64_t now_ms = uptimeMilliseconds();
    std::vector<ManagedSensor> sensors;
    sensors.reserve(config.sensor_count);

    for (std::size_t index = 0; index < config.sensor_count; ++index) {
        const SensorRecord& record = config.sensors[index];
        const SensorDescriptor* descriptor = registry.findByType(record.sensor_type);

        ManagedSensor managed;
        managed.record = record;
        managed.descriptor = descriptor;
        managed.runtime.id = record.id;
        managed.runtime.enabled = record.enabled != 0U;
        managed.runtime.sensor_type = record.sensor_type;
        managed.runtime.transport_kind = record.transport_kind;
        managed.runtime.type_key =
            descriptor != nullptr ? descriptor->type_key : std::string("unknown");
        managed.runtime.type_name =
            descriptor != nullptr ? descriptor->display_name : std::string("Unknown sensor");
        managed.runtime.display_name =
            record.display_name[0] != '\0' ? std::string(record.display_name)
                                           : defaultDisplayName(descriptor, record.id);
        managed.runtime.binding_summary = bindingSummary(record);

        if (!managed.runtime.enabled) {
            managed.runtime.state = SensorRuntimeState::kDisabled;
        } else if (descriptor == nullptr) {
            managed.runtime.state = SensorRuntimeState::kUnsupported;
            managed.runtime.last_error = "Unsupported sensor type";
        } else if (!registry.supportsTransport(*descriptor, record.transport_kind)) {
            managed.runtime.state = SensorRuntimeState::kUnsupported;
            managed.runtime.last_error = "Unsupported transport for selected sensor";
        } else if (!descriptor->driver_implemented || descriptor->create_driver == nullptr) {
            managed.runtime.state = SensorRuntimeState::kConfigured;
        } else {
            managed.driver = descriptor->create_driver();
            if (!managed.driver) {
                managed.runtime.state = SensorRuntimeState::kError;
                managed.runtime.last_error = "Failed to allocate sensor driver.";
            } else {
                const esp_err_t init_err = managed.driver->init(record, driver_context);
                if (init_err == ESP_OK) {
                    managed.driver_ready = true;
                    managed.runtime.state = SensorRuntimeState::kInitialized;
                    managed.runtime.last_error.clear();
                    managed.runtime.measurement = managed.driver->latestMeasurement();
                    managed.runtime.last_sample_time_ms =
                        managed.runtime.measurement.sample_time_ms;
                    managed.next_action_time_ms = now_ms;
                } else {
                    managed.driver_ready = false;
                    managed.runtime.state = classifyFailureState(init_err);
                    managed.runtime.last_error = errorText(*managed.driver, init_err);
                    managed.next_action_time_ms =
                        now_ms + std::min<std::uint32_t>(record.poll_interval_ms, kRetryDelayMs);
                }
            }
        }

        sensors.push_back(std::move(managed));
    }

    return sensors;
}

void SensorManager::stop() {
    ensureMutex();

    lock();
    const bool had_task = task_ != nullptr;
    stop_requested_ = true;
    unlock();

    if (had_task) {
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(10));
            lock();
            const bool task_stopped = task_ == nullptr;
            unlock();
            if (task_stopped) {
                break;
            }
        }
    }

    lock();
    stop_requested_ = false;
    unlock();
    i2c_bus_manager_.shutdown();
    uart_port_manager_.shutdown();
}

std::vector<SensorRuntimeInfo> SensorManager::sensors() const {
    ensureMutex();
    lock();
    std::vector<SensorRuntimeInfo> snapshot;
    snapshot.reserve(sensors_.size());
    for (const auto& sensor : sensors_) {
        snapshot.push_back(sensor.runtime);
    }
    unlock();
    return snapshot;
}

std::size_t SensorManager::configuredCount() const {
    ensureMutex();
    lock();
    const std::size_t count = sensors_.size();
    unlock();
    return count;
}

std::size_t SensorManager::enabledCount() const {
    ensureMutex();
    lock();
    std::size_t count = 0U;
    for (const auto& sensor : sensors_) {
        if (sensor.runtime.enabled) {
            ++count;
        }
    }
    unlock();
    return count;
}

void SensorManager::ensureMutex() const {
    if (mutex_ == nullptr) {
        mutex_ = xSemaphoreCreateMutexStatic(&mutex_buffer_);
    }
}

void SensorManager::lock() const {
    xSemaphoreTake(mutex_, portMAX_DELAY);
}

void SensorManager::unlock() const {
    xSemaphoreGive(mutex_);
}

void SensorManager::startLocked() {
    if (task_ != nullptr) {
        return;
    }

    bool has_pollable_sensor = false;
    for (const auto& sensor : sensors_) {
        if (sensor.runtime.enabled && sensor.driver != nullptr) {
            has_pollable_sensor = true;
            break;
        }
    }

    if (!has_pollable_sensor) {
        return;
    }

    const BaseType_t created = xTaskCreate(
        &SensorManager::taskEntry,
        "air360_sensor",
        kManagerTaskStackSize,
        this,
        kManagerTaskPriority,
        &task_);
    if (created != pdPASS) {
        ESP_LOGE(kTag, "Failed to start sensor manager task");
        task_ = nullptr;
        for (auto& sensor : sensors_) {
            if (sensor.runtime.enabled && sensor.driver != nullptr) {
                sensor.runtime.state = SensorRuntimeState::kError;
                sensor.runtime.last_error = "Failed to start sensor manager task.";
            }
        }
    }
}

void SensorManager::taskEntry(void* arg) {
    static_cast<SensorManager*>(arg)->taskMain();
}

void SensorManager::taskMain() {
    const SensorDriverContext driver_context{&i2c_bus_manager_, &uart_port_manager_};

    for (;;) {
        lock();
        const bool stop_requested = stop_requested_;
        unlock();
        if (stop_requested) {
            break;
        }

        const std::uint64_t now_ms = uptimeMilliseconds();

        lock();
        const std::size_t sensor_count = sensors_.size();
        unlock();

        for (std::size_t index = 0; index < sensor_count; ++index) {
            SensorDriver* driver = nullptr;
            SensorRecord record{};
            bool needs_init = false;

            lock();
            if (index < sensors_.size()) {
                auto& sensor = sensors_[index];
                if (sensor.runtime.enabled && sensor.driver != nullptr &&
                    now_ms >= sensor.next_action_time_ms) {
                    driver = sensor.driver.get();
                    record = sensor.record;
                    needs_init = !sensor.driver_ready;
                }
            }
            unlock();

            if (driver == nullptr) {
                continue;
            }

            const esp_err_t op_err =
                needs_init ? driver->init(record, driver_context) : driver->poll();
            SensorMeasurement measurement =
                op_err == ESP_OK ? driver->latestMeasurement() : SensorMeasurement{};
            const std::string last_error =
                op_err == ESP_OK ? std::string{} : errorText(*driver, op_err);

            if (op_err == ESP_OK && !measurement.empty() && measurement_store_ != nullptr) {
                const std::int64_t sample_unix_ms = currentUnixMilliseconds();
                if (sample_unix_ms > 0) {
                    measurement_store_->append(
                        MeasurementSample{
                            record.id,
                            record.sensor_type,
                            static_cast<std::uint64_t>(sample_unix_ms),
                            measurement,
                        });
                }
            }

            lock();
            if (index >= sensors_.size()) {
                unlock();
                continue;
            }

            auto& sensor = sensors_[index];
            if (sensor.driver.get() != driver) {
                unlock();
                continue;
            }

            if (op_err == ESP_OK) {
                sensor.driver_ready = true;
                sensor.runtime.measurement = measurement;
                sensor.runtime.last_sample_time_ms = measurement.sample_time_ms;
                sensor.runtime.state = needs_init ? SensorRuntimeState::kInitialized
                                                  : SensorRuntimeState::kPolling;
                sensor.runtime.last_error.clear();
                sensor.next_action_time_ms =
                    now_ms + (needs_init ? 0U : sensor.record.poll_interval_ms);
            } else {
                sensor.driver_ready = false;
                sensor.runtime.state = classifyFailureState(op_err);
                sensor.runtime.last_error = last_error;
                sensor.next_action_time_ms =
                    now_ms + std::min<std::uint32_t>(sensor.record.poll_interval_ms, kRetryDelayMs);
            }
            unlock();
        }

        vTaskDelay(kManagerLoopDelay);
    }

    lock();
    task_ = nullptr;
    unlock();
    vTaskDelete(nullptr);
}

}  // namespace air360
