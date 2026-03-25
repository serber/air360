#include "air360/sensors/sensor_manager.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <string>
#include <utility>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.sensor";
constexpr std::uint64_t kRetryDelayMs = 5000U;
constexpr TickType_t kManagerLoopDelay = pdMS_TO_TICKS(250);
constexpr uint32_t kManagerTaskStackSize = 6144U;
constexpr UBaseType_t kManagerTaskPriority = 5U;

std::uint64_t uptimeMilliseconds() {
    return static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
}

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
        case TransportKind::kUnknown:
        default:
            return "unbound";
    }
}

}  // namespace

void SensorManager::applyConfig(const SensorConfigList& config) {
    stop();
    ensureMutex();
    lock();
    sensors_.clear();
    sensors_.reserve(config.sensor_count);

    SensorRegistry registry;
    const SensorDriverContext driver_context{&i2c_bus_manager_};
    const std::uint64_t now_ms = uptimeMilliseconds();
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

        sensors_.push_back(std::move(managed));
    }

    startLocked();
    unlock();
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
    const SensorDriverContext driver_context{&i2c_bus_manager_};

    for (;;) {
        lock();
        const bool stop_requested = stop_requested_;
        unlock();
        if (stop_requested) {
            break;
        }

        const std::uint64_t now_ms = uptimeMilliseconds();

        lock();
        for (auto& sensor : sensors_) {
            if (!sensor.runtime.enabled || sensor.driver == nullptr) {
                continue;
            }

            if (now_ms < sensor.next_action_time_ms) {
                continue;
            }

            if (!sensor.driver_ready) {
                const esp_err_t init_err = sensor.driver->init(sensor.record, driver_context);
                if (init_err == ESP_OK) {
                    sensor.driver_ready = true;
                    sensor.runtime.state = SensorRuntimeState::kInitialized;
                    sensor.runtime.last_error.clear();
                    sensor.runtime.measurement = sensor.driver->latestMeasurement();
                    sensor.runtime.last_sample_time_ms =
                        sensor.runtime.measurement.sample_time_ms;
                    sensor.next_action_time_ms = now_ms;
                } else {
                    sensor.runtime.state = classifyFailureState(init_err);
                    sensor.runtime.last_error = errorText(*sensor.driver, init_err);
                    sensor.next_action_time_ms =
                        now_ms + std::min<std::uint32_t>(sensor.record.poll_interval_ms, kRetryDelayMs);
                }
                continue;
            }

            const esp_err_t poll_err = sensor.driver->poll();
            if (poll_err == ESP_OK) {
                sensor.runtime.measurement = sensor.driver->latestMeasurement();
                sensor.runtime.last_sample_time_ms =
                    sensor.runtime.measurement.sample_time_ms;
                sensor.runtime.state = SensorRuntimeState::kPolling;
                sensor.runtime.last_error.clear();
                sensor.next_action_time_ms = now_ms + sensor.record.poll_interval_ms;
            } else {
                sensor.driver_ready = false;
                sensor.runtime.state = classifyFailureState(poll_err);
                sensor.runtime.last_error = errorText(*sensor.driver, poll_err);
                sensor.next_action_time_ms =
                    now_ms + std::min<std::uint32_t>(sensor.record.poll_interval_ms, kRetryDelayMs);
            }
        }
        unlock();

        vTaskDelay(kManagerLoopDelay);
    }

    lock();
    task_ = nullptr;
    unlock();
    vTaskDelete(nullptr);
}

}  // namespace air360
