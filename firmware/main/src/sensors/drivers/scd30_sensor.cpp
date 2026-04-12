#include "air360/sensors/drivers/scd30_sensor.hpp"

#include <cmath>
#include <cstring>
#include <memory>
#include <string>

#include "air360/sensors/transport_binding.hpp"
#include "esp_timer.h"
#include "scd30.h"

namespace air360 {

namespace {

std::uint16_t measurementIntervalSeconds(std::uint32_t poll_interval_ms) {
    const std::uint32_t rounded_up = (poll_interval_ms + 999U) / 1000U;
    if (rounded_up <= 2U) {
        return 3U;
    }
    if (rounded_up >= 1800U) {
        return 1799U;
    }
    return static_cast<std::uint16_t>(rounded_up);
}

}  // namespace

Scd30Sensor::~Scd30Sensor() {
    reset();
}

SensorType Scd30Sensor::type() const {
    return SensorType::kScd30;
}

esp_err_t Scd30Sensor::init(const SensorRecord& record, const SensorDriverContext& context) {
    reset();
    record_ = record;
    measurement_.clear();
    last_error_.clear();

    i2c_port_t port = I2C_NUM_0;
    gpio_num_t sda = GPIO_NUM_NC;
    gpio_num_t scl = GPIO_NUM_NC;
    if (!context.i2c_bus_manager->resolvePins(record.i2c_bus_id, port, sda, scl)) {
        setError("Unknown I2C bus id for SCD30.");
        return ESP_ERR_NOT_SUPPORTED;
    }

    std::memset(&device_, 0, sizeof(device_));
    esp_err_t err = scd30_init_desc(&device_, port, sda, scl);
    if (err != ESP_OK) {
        setError("Failed to initialize SCD30 descriptor.");
        reset();
        return err;
    }
    descriptor_initialized_ = true;

    err = i2c_dev_check_present(&device_);
    if (err != ESP_OK) {
        setError("SCD30 sensor was not detected on I2C bus 0 at address 0x61.");
        reset();
        return err;
    }

    err = scd30_set_measurement_interval(&device_, measurementIntervalSeconds(record_.poll_interval_ms));
    if (err != ESP_OK) {
        setError("Failed to configure SCD30 measurement interval.");
        reset();
        return err;
    }

    err = scd30_trigger_continuous_measurement(&device_, 0U);
    if (err != ESP_OK) {
        setError("Failed to start continuous SCD30 measurement.");
        reset();
        return err;
    }

    measurement_running_ = true;
    initialized_ = true;
    last_error_ = "Waiting for first SCD30 sample.";
    return ESP_OK;
}

esp_err_t Scd30Sensor::poll() {
    if (!initialized_ || !descriptor_initialized_) {
        setError("SCD30 sensor is not initialized.");
        return ESP_ERR_INVALID_STATE;
    }

    bool data_ready = false;
    esp_err_t err = scd30_get_data_ready_status(&device_, &data_ready);
    if (err != ESP_OK) {
        setError("Failed to query SCD30 data-ready status.");
        initialized_ = false;
        return err;
    }

    if (!data_ready) {
        measurement_.clear();
        last_error_ = "Waiting for new SCD30 sample.";
        return ESP_OK;
    }

    float co2_ppm = 0.0F;
    float temperature_c = 0.0F;
    float humidity_percent = 0.0F;
    err = scd30_read_measurement(&device_, &co2_ppm, &temperature_c, &humidity_percent);
    if (err != ESP_OK) {
        setError("Failed to read SCD30 measurement.");
        initialized_ = false;
        return err;
    }

    if (std::isnan(co2_ppm) || std::isnan(temperature_c) || std::isnan(humidity_percent)) {
        setError("SCD30 driver returned invalid values.");
        initialized_ = false;
        return ESP_ERR_INVALID_RESPONSE;
    }

    measurement_.clear();
    measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
    measurement_.addValue(SensorValueKind::kCo2Ppm, co2_ppm);
    measurement_.addValue(SensorValueKind::kTemperatureC, temperature_c);
    measurement_.addValue(SensorValueKind::kHumidityPercent, humidity_percent);
    last_error_.clear();
    return ESP_OK;
}

SensorMeasurement Scd30Sensor::latestMeasurement() const {
    return measurement_;
}

std::string Scd30Sensor::lastError() const {
    return last_error_;
}

void Scd30Sensor::reset() {
    initialized_ = false;
    if (descriptor_initialized_) {
        if (measurement_running_) {
            scd30_stop_continuous_measurement(&device_);
            measurement_running_ = false;
        }
        scd30_free_desc(&device_);
        std::memset(&device_, 0, sizeof(device_));
        descriptor_initialized_ = false;
    }
}

void Scd30Sensor::setError(const std::string& message) {
    last_error_ = message;
}

std::unique_ptr<SensorDriver> createScd30Sensor() {
    return std::make_unique<Scd30Sensor>();
}

}  // namespace air360
