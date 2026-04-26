#include "air360/sensors/sensor_manager.hpp"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

#include "air360/time_utils.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.sensor";
// First re-init retry after 1 s catches transient bus-settle issues quickly.
constexpr std::uint32_t kInitBackoffBaseMs = 1000U;
// Stop exponential growth at 5 min so failed sensors do not wake the task too
// often, but still retry eventually after long outages.
constexpr std::uint32_t kInitBackoffCapMs = 5U * 60U * 1000U;
// Eight shifts already exceed the 5 min cap, so further growth is wasted.
constexpr std::uint32_t kInitBackoffShiftCap = 8U;
// After 16 consecutive failures the sensor is treated as operator-visiblely
// broken and automatic retries stop until config is rebuilt.
constexpr std::uint32_t kSensorFailureStopThreshold = 16U;
// Soft poll failures retry after at most 5 s to absorb short bus glitches
// without tearing down a driver that was otherwise healthy.
constexpr std::uint64_t kSoftPollRetryDelayMs = 5000U;
// 250 ms loop cadence is fast enough for 1 s retry granularity while keeping
// the manager mostly idle between scheduled sensor actions.
constexpr TickType_t kManagerLoopDelay = pdMS_TO_TICKS(250);
// 6 KB covers the driver registry walk plus per-sensor error formatting.
constexpr uint32_t kManagerTaskStackSize = 6144U;
// Run above idle so scheduled polls are not delayed by background maintenance.
constexpr UBaseType_t kManagerTaskPriority = 5U;
// Stop should complete within one short loop iteration plus driver cleanup.
constexpr std::uint32_t kStopTimeoutMs = 5000U;

std::uint32_t initBackoffDelayMs(std::uint32_t consecutive_failures) {
    const std::uint32_t shift =
        std::min<std::uint32_t>(consecutive_failures, kInitBackoffShiftCap);
    const std::uint32_t delay = kInitBackoffBaseMs << shift;
    return std::min(delay, kInitBackoffCapMs);
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

struct ClaimedUartBinding {
    std::uint8_t port_id = 0U;
    std::int16_t rx_pin = -1;
    std::int16_t tx_pin = -1;
    std::uint32_t baud_rate = 0U;
    std::uint32_t owner_id = 0U;
    std::string owner_name;
};

const ClaimedUartBinding* findClaimedUartBinding(
    const std::vector<ClaimedUartBinding>& claims,
    const SensorRecord& record) {
    for (const auto& claim : claims) {
        if (claim.port_id == record.uart_port_id &&
            claim.rx_pin == record.uart_rx_gpio_pin &&
            claim.tx_pin == record.uart_tx_gpio_pin &&
            claim.baud_rate == record.uart_baud_rate) {
            return &claim;
        }
    }

    return nullptr;
}

}  // namespace

SensorManager::SensorManager() {
    mutex_ = xSemaphoreCreateMutexStatic(&mutex_buffer_);
    lifecycle_events_ = xEventGroupCreateStatic(&lifecycle_events_buffer_);
}

void SensorManager::setMeasurementStore(MeasurementStore& measurement_store) {
    measurement_store_ = &measurement_store;
}

esp_err_t SensorManager::applyConfig(const SensorConfigList& config) {
    const esp_err_t stop_err = stop();
    if (stop_err != ESP_OK) {
        ESP_LOGE(
            kTag,
            "Sensor reconfigure aborted: previous task did not stop within %" PRIu32 " ms",
            kStopTimeoutMs);
        return stop_err;
    }

    const esp_err_t i2c_err = i2c_bus_manager_.init();
    if (i2c_err != ESP_OK) {
        ESP_LOGE(kTag, "I2C bus manager init failed: %s", esp_err_to_name(i2c_err));
    }

    std::vector<ManagedSensor> next_sensors = buildManagedSensors(config);

    lock();
    sensors_ = std::move(next_sensors);
    const esp_err_t start_err = startLocked();
    unlock();
    return start_err;
}

std::vector<SensorManager::ManagedSensor> SensorManager::buildManagedSensors(
    const SensorConfigList& config) {
    SensorRegistry registry;
    const std::uint64_t now_ms = uptimeMilliseconds();
    std::vector<ManagedSensor> sensors;
    std::vector<ClaimedUartBinding> claimed_uart_bindings;
    sensors.reserve(config.sensor_count);
    claimed_uart_bindings.reserve(config.sensor_count);

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
        managed.runtime.binding_summary = bindingSummary(record);
        managed.runtime.poll_interval_ms = record.poll_interval_ms;

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
        } else if (record.transport_kind == TransportKind::kUart) {
            const ClaimedUartBinding* claim =
                findClaimedUartBinding(claimed_uart_bindings, record);
            if (claim != nullptr) {
                managed.runtime.state = SensorRuntimeState::kError;
                managed.runtime.last_error =
                    "UART binding conflicts with enabled sensor " + claim->owner_name +
                    " (#" + std::to_string(claim->owner_id) + ").";
            } else {
                claimed_uart_bindings.push_back(
                    ClaimedUartBinding{
                        record.uart_port_id,
                        record.uart_rx_gpio_pin,
                        record.uart_tx_gpio_pin,
                        record.uart_baud_rate,
                        record.id,
                        managed.runtime.type_name,
                    });
                managed.driver = descriptor->create_driver();
                if (!managed.driver) {
                    managed.runtime.state = SensorRuntimeState::kError;
                    managed.runtime.last_error = "Failed to allocate sensor driver.";
                } else {
                    managed.driver_ready = false;
                    managed.runtime.state = SensorRuntimeState::kConfigured;
                    managed.runtime.last_error.clear();
                    managed.next_init_allowed_ms = now_ms;
                    managed.next_action_time_ms = now_ms;
                }
            }
        } else {
            managed.driver = descriptor->create_driver();
            if (!managed.driver) {
                managed.runtime.state = SensorRuntimeState::kError;
                managed.runtime.last_error = "Failed to allocate sensor driver.";
            } else {
                managed.driver_ready = false;
                managed.runtime.state = SensorRuntimeState::kConfigured;
                managed.runtime.last_error.clear();
                managed.next_init_allowed_ms = now_ms;
                managed.next_action_time_ms = now_ms;
            }
        }

        sensors.push_back(std::move(managed));
    }

    return sensors;
}

esp_err_t SensorManager::stop() {
    lock();
    const TaskHandle_t task = task_;
    if (task != nullptr) {
        stop_requested_.store(true, std::memory_order_release);
        xTaskNotifyGive(task);
    }
    unlock();

    if (task != nullptr) {
        const EventBits_t bits = xEventGroupWaitBits(
            lifecycle_events_,
            kTaskStoppedBit,
            pdTRUE,
            pdFALSE,
            pdMS_TO_TICKS(kStopTimeoutMs));
        if ((bits & kTaskStoppedBit) == 0U) {
            ESP_LOGE(
                kTag,
                "Timed out waiting for sensor manager task to stop (%" PRIu32 " ms)",
                kStopTimeoutMs);
            return ESP_ERR_TIMEOUT;
        }
    }

    stop_requested_.store(false, std::memory_order_release);
    uart_port_manager_.shutdown();
    return ESP_OK;
}

std::vector<SensorRuntimeInfo> SensorManager::sensors() const {
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
    lock();
    const std::size_t count = sensors_.size();
    unlock();
    return count;
}

std::size_t SensorManager::enabledCount() const {
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

std::size_t SensorManager::taskStackHighWaterMarkBytes() const {
    lock();
    const TaskHandle_t task = task_;
    unlock();

    if (task == nullptr) {
        return 0U;
    }

    return static_cast<std::size_t>(uxTaskGetStackHighWaterMark(task)) * sizeof(StackType_t);
}

void SensorManager::lock() const {
    xSemaphoreTake(mutex_, portMAX_DELAY);
}

void SensorManager::unlock() const {
    xSemaphoreGive(mutex_);
}

esp_err_t SensorManager::startLocked() {
    if (task_ != nullptr) {
        return ESP_OK;
    }

    bool has_pollable_sensor = false;
    for (const auto& sensor : sensors_) {
        if (sensor.runtime.enabled && sensor.driver != nullptr) {
            has_pollable_sensor = true;
            break;
        }
    }

    if (!has_pollable_sensor) {
        return ESP_OK;
    }

    stop_requested_.store(false, std::memory_order_release);
    xEventGroupClearBits(lifecycle_events_, kTaskStoppedBit);

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
        return ESP_FAIL;
    }

    return ESP_OK;
}

bool SensorManager::stopRequested() const {
    return stop_requested_.load(std::memory_order_acquire);
}

void SensorManager::taskEntry(void* arg) {
    static_cast<SensorManager*>(arg)->taskMain();
}

void SensorManager::taskMain() {
    esp_task_wdt_add(nullptr);
    ESP_LOGI(kTag, "TWDT: air360_sensor subscribed");

    const SensorDriverContext driver_context{&i2c_bus_manager_, &uart_port_manager_};

    for (;;) {
        if (stopRequested()) {
            break;
        }

        const std::uint64_t now_ms = uptimeMilliseconds();

        lock();
        const std::size_t sensor_count = sensors_.size();
        unlock();

        for (std::size_t index = 0; index < sensor_count; ++index) {
            if (stopRequested()) {
                break;
            }

            SensorDriver* driver = nullptr;
            SensorRecord record{};
            bool needs_init = false;

            lock();
            if (index < sensors_.size()) {
                auto& sensor = sensors_[index];
                if (sensor.runtime.enabled && sensor.driver != nullptr &&
                    sensor.runtime.state != SensorRuntimeState::kFailed &&
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
            const std::string driver_status =
                op_err == ESP_OK ? driver->lastError() : std::string{};
            const std::string last_error =
                op_err == ESP_OK ? std::string{} : errorText(*driver, op_err);

            if (op_err == ESP_OK && !measurement.empty() && measurement_store_ != nullptr) {
                measurement_store_->recordMeasurement(
                    record.id,
                    record.sensor_type,
                    measurement,
                    currentUnixMilliseconds());
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
                sensor.consecutive_poll_failures = 0U;
                sensor.runtime.state = needs_init ? SensorRuntimeState::kInitialized
                                                  : SensorRuntimeState::kPolling;
                if (!needs_init) {
                    sensor.runtime.failures = 0U;
                }
                sensor.runtime.soft_fails = driver->softFailCount();
                sensor.runtime.next_retry_ms = 0U;
                sensor.runtime.last_error = driver_status;
                sensor.next_action_time_ms =
                    now_ms + (needs_init ? 0U : sensor.record.poll_interval_ms);
            } else {
                sensor.runtime.state = classifyFailureState(op_err);
                sensor.runtime.last_error = last_error;
                sensor.runtime.failures++;

                if (!needs_init) {
                    sensor.consecutive_poll_failures++;
                }

                const bool soft_poll_failure =
                    !needs_init &&
                    sensor.consecutive_poll_failures < kSensorPollFailureReinitThreshold;
                if (soft_poll_failure) {
                    sensor.driver_ready = true;
                    sensor.next_action_time_ms =
                        now_ms + std::min<std::uint64_t>(
                                     sensor.record.poll_interval_ms,
                                     kSoftPollRetryDelayMs);
                    sensor.runtime.next_retry_ms = sensor.next_action_time_ms;
                    sensor.runtime.soft_fails = driver->softFailCount();
                    unlock();
                    continue;
                }
                sensor.runtime.soft_fails = driver->softFailCount();

                sensor.driver_ready = false;
                sensor.consecutive_poll_failures = 0U;

                if (sensor.runtime.failures >= kSensorFailureStopThreshold) {
                    sensor.runtime.state = SensorRuntimeState::kFailed;
                    sensor.runtime.next_retry_ms = 0U;
                    sensor.next_init_allowed_ms = 0U;
                    sensor.next_action_time_ms = std::numeric_limits<std::uint64_t>::max();
                    ESP_LOGE(
                        kTag,
                        "Sensor #%" PRIu32 " (%s) marked failed after %" PRIu32
                        " consecutive failures; re-enable or reload config to retry",
                        sensor.runtime.id,
                        sensor.runtime.type_key.c_str(),
                        sensor.runtime.failures);
                    unlock();
                    continue;
                }

                const std::uint32_t delay_ms = initBackoffDelayMs(sensor.runtime.failures - 1U);
                sensor.next_init_allowed_ms = now_ms + delay_ms;
                sensor.next_action_time_ms = sensor.next_init_allowed_ms;
                sensor.runtime.next_retry_ms = sensor.next_init_allowed_ms;
                ESP_LOGW(
                    kTag,
                    "Sensor #%" PRIu32 " (%s) init backoff after %" PRIu32
                    " consecutive failures; retry in %" PRIu32 " ms at uptime %" PRIu64 " ms",
                    sensor.runtime.id,
                    sensor.runtime.type_key.c_str(),
                    sensor.runtime.failures,
                    delay_ms,
                    sensor.next_init_allowed_ms);
            }
            unlock();
        }

        // The notification count only wakes the polling loop; no per-notification state is encoded.
        static_cast<void>(ulTaskNotifyTake(pdTRUE, kManagerLoopDelay));
        esp_task_wdt_reset();
    }

    lock();
    task_ = nullptr;
    unlock();
    esp_task_wdt_delete(nullptr);
    xEventGroupSetBits(lifecycle_events_, kTaskStoppedBit);
    vTaskDelete(nullptr);
}

}  // namespace air360
