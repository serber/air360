#include "air360/sensors/drivers/ds18b20_sensor.hpp"

#include <memory>
#include <string>

#include "ds18b20.h"
#include "esp_timer.h"
#include "onewire_bus.h"
#include "onewire_device.h"

namespace air360 {

Ds18b20Sensor::~Ds18b20Sensor() {
    reset();
}

SensorType Ds18b20Sensor::type() const {
    return SensorType::kDs18b20;
}

esp_err_t Ds18b20Sensor::init(const SensorRecord& record, const SensorDriverContext& context) {
    static_cast<void>(context);
    reset();
    record_ = record;
    measurement_.clear();
    last_error_.clear();
    poll_failure_count_ = 0U;

    if (record_.analog_gpio_pin < 0) {
        setError("DS18B20 GPIO pin is not configured.");
        return ESP_ERR_INVALID_ARG;
    }

    const onewire_bus_config_t bus_config = {
        .bus_gpio_num = record_.analog_gpio_pin,
        .flags =
            {
                .en_pull_up = 1,
            },
    };
    const onewire_bus_rmt_config_t rmt_config = {
        .max_rx_bytes = 10U,
    };
    esp_err_t err = onewire_new_bus_rmt(&bus_config, &rmt_config, &bus_);
    if (err != ESP_OK) {
        setError("Failed to initialize 1-Wire bus.");
        reset();
        return err;
    }

    onewire_device_iter_handle_t iter = nullptr;
    err = onewire_new_device_iter(bus_, &iter);
    if (err != ESP_OK) {
        setError("Failed to create 1-Wire device iterator.");
        reset();
        return err;
    }

    bool found_ds18b20 = false;
    onewire_device_t next_onewire_device{};
    esp_err_t search_result = ESP_OK;
    while ((search_result = onewire_device_iter_get_next(iter, &next_onewire_device)) == ESP_OK) {
        ds18b20_config_t ds18b20_config = {};
        ds18b20_device_handle_t next_device = nullptr;
        err = ds18b20_new_device_from_enumeration(
            &next_onewire_device,
            &ds18b20_config,
            &next_device);
        if (err != ESP_OK) {
            continue;
        }

        if (found_ds18b20) {
            ds18b20_del_device(next_device);
            onewire_del_device_iter(iter);
            setError("Multiple DS18B20 sensors detected on the same GPIO bus. Only one sensor per slot is supported.");
            reset();
            return ESP_ERR_NOT_SUPPORTED;
        }

        device_ = next_device;
        found_ds18b20 = true;
        ds18b20_get_device_address(device_, &address_);
    }

    onewire_del_device_iter(iter);
    if (search_result != ESP_ERR_NOT_FOUND) {
        setError("1-Wire device enumeration failed.");
        reset();
        return search_result;
    }

    if (!found_ds18b20 || device_ == nullptr) {
        setError("No DS18B20 sensor detected on the selected GPIO bus.");
        reset();
        return ESP_ERR_NOT_FOUND;
    }

    err = ds18b20_set_resolution(device_, DS18B20_RESOLUTION_12B);
    if (err != ESP_OK) {
        setError("Failed to configure DS18B20 resolution.");
        reset();
        return err;
    }

    initialized_ = true;
    return ESP_OK;
}

esp_err_t Ds18b20Sensor::poll() {
    if (!initialized_ || device_ == nullptr || bus_ == nullptr) {
        setError("DS18B20 sensor is not initialized.");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ds18b20_trigger_temperature_conversion(device_);
    if (err != ESP_OK) {
        setError("Failed to trigger DS18B20 temperature conversion.");
        if (++poll_failure_count_ >= kSensorPollFailureReinitThreshold) {
            initialized_ = false;
        }
        return err;
    }

    float temperature_c = 0.0F;
    err = ds18b20_get_temperature(device_, &temperature_c);
    if (err != ESP_OK) {
        setError("Failed to read DS18B20 temperature.");
        if (++poll_failure_count_ >= kSensorPollFailureReinitThreshold) {
            initialized_ = false;
        }
        return err;
    }

    measurement_.clear();
    measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
    measurement_.addValue(SensorValueKind::kTemperatureC, temperature_c);
    poll_failure_count_ = 0U;
    last_error_.clear();
    return ESP_OK;
}

SensorMeasurement Ds18b20Sensor::latestMeasurement() const {
    return measurement_;
}

std::string Ds18b20Sensor::lastError() const {
    return last_error_;
}

void Ds18b20Sensor::reset() {
    initialized_ = false;
    poll_failure_count_ = 0U;
    if (device_ != nullptr) {
        ds18b20_del_device(device_);
        device_ = nullptr;
    }
    if (bus_ != nullptr) {
        onewire_bus_del(bus_);
        bus_ = nullptr;
    }
    address_ = 0U;
}

void Ds18b20Sensor::setError(const std::string& message) {
    last_error_ = message;
}

std::unique_ptr<SensorDriver> createDs18b20Sensor() {
    return std::make_unique<Ds18b20Sensor>();
}

}  // namespace air360
