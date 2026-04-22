#include "air360/sensors/drivers/bme680_sensor.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

#include "air360/sensors/transport_binding.hpp"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" {
#include "bme680.h"
}

namespace air360 {

struct Bme680DriverState {
    bme680_t device{};
    bool descriptor_initialized = false;
};

namespace {

constexpr std::uint32_t kBme680I2cSpeedHz = 100000U;
constexpr std::int16_t kAmbientTemperatureC = 25;
constexpr bme680_oversampling_rate_t kOversampling = BME680_OSR_2X;
constexpr bme680_filter_size_t kFilter = BME680_IIR_SIZE_0;
constexpr std::uint16_t kHeaterTemperatureC = 300U;
constexpr std::uint16_t kHeaterDurationMs = 100U;

}  // namespace

Bme680Sensor::Bme680Sensor() : state_(std::make_unique<Bme680DriverState>()) {}

Bme680Sensor::~Bme680Sensor() {
    reset();
    state_.reset();
}

SensorType Bme680Sensor::type() const {
    return SensorType::kBme680;
}

esp_err_t Bme680Sensor::init(
    const SensorRecord& record,
    const SensorDriverContext& context) {
    reset();
    record_ = record;
    measurement_.clear();
    last_error_.clear();
    initialized_ = false;

    i2c_port_t port = I2C_NUM_0;
    gpio_num_t sda = GPIO_NUM_NC;
    gpio_num_t scl = GPIO_NUM_NC;
    if (!context.i2c_bus_manager->resolvePins(record.i2c_bus_id, port, sda, scl)) {
        setError("Unknown I2C bus id for BME680.");
        return ESP_ERR_NOT_SUPPORTED;
    }

    std::memset(&state_->device, 0, sizeof(state_->device));

    esp_err_t err = bme680_init_desc(
        &state_->device,
        record.i2c_address,
        port,
        sda,
        scl);
    if (err != ESP_OK) {
        setError("Failed to initialize BME680 descriptor.");
        reset();
        return err;
    }
    state_->descriptor_initialized = true;
    state_->device.i2c_dev.cfg.master.clk_speed = kBme680I2cSpeedHz;
    state_->device.i2c_dev.cfg.sda_pullup_en = 1;
    state_->device.i2c_dev.cfg.scl_pullup_en = 1;

    err = bme680_init_sensor(&state_->device);
    if (err != ESP_OK) {
        setError(std::string("Failed to initialize BME680 sensor: ") + esp_err_to_name(err));
        reset();
        return err;
    }

    const esp_err_t configure_err = configureSensor();
    if (configure_err != ESP_OK) {
        reset();
        return configure_err;
    }

    initialized_ = true;
    return ESP_OK;
}

esp_err_t Bme680Sensor::poll() {
    if (!initialized_ || !state_) {
        setError("BME680 is not initialized.");
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t measurement_duration_ticks = 0U;
    esp_err_t err = bme680_get_measurement_duration(&state_->device, &measurement_duration_ticks);
    if (err != ESP_OK || measurement_duration_ticks == 0U) {
        setError("Failed to calculate BME680 measurement duration.");
        if (++poll_failure_count_ >= kSensorPollFailureReinitThreshold) {
            initialized_ = false;
        }
        return err != ESP_OK ? err : ESP_FAIL;
    }

    err = bme680_force_measurement(&state_->device);
    if (err != ESP_OK) {
        setError("Failed to start BME680 forced measurement.");
        if (++poll_failure_count_ >= kSensorPollFailureReinitThreshold) {
            initialized_ = false;
        }
        return err;
    }

    vTaskDelay(measurement_duration_ticks);

    bme680_values_float_t data{};
    err = bme680_get_results_float(&state_->device, &data);
    if (err != ESP_OK) {
        setError(std::string("Failed to read BME680 measurement: ") + esp_err_to_name(err));
        if (++poll_failure_count_ >= kSensorPollFailureReinitThreshold) {
            initialized_ = false;
        }
        return err;
    }

    if (std::isnan(data.temperature) ||
        std::isnan(data.humidity) ||
        std::isnan(data.pressure)) {
        setError("BME680 driver returned invalid values.");
        if (++poll_failure_count_ >= kSensorPollFailureReinitThreshold) {
            initialized_ = false;
        }
        return ESP_ERR_INVALID_RESPONSE;
    }

    measurement_.clear();
    measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
    measurement_.addValue(SensorValueKind::kTemperatureC, data.temperature);
    measurement_.addValue(SensorValueKind::kHumidityPercent, data.humidity);
    measurement_.addValue(SensorValueKind::kPressureHpa, data.pressure);

    if (data.gas_resistance > 0.0F) {
        measurement_.addValue(SensorValueKind::kGasResistanceOhms, data.gas_resistance);
    }

    poll_failure_count_ = 0U;
    last_error_.clear();
    return ESP_OK;
}

SensorMeasurement Bme680Sensor::latestMeasurement() const {
    return measurement_;
}

std::string Bme680Sensor::lastError() const {
    return last_error_;
}

esp_err_t Bme680Sensor::configureSensor() {
    esp_err_t err = bme680_set_oversampling_rates(
        &state_->device,
        kOversampling,
        kOversampling,
        kOversampling);
    if (err != ESP_OK) {
        setError("Failed to configure BME680 oversampling.");
        return err;
    }

    err = bme680_set_filter_size(&state_->device, kFilter);
    if (err != ESP_OK) {
        setError("Failed to configure BME680 filter.");
        return err;
    }

    err = bme680_set_heater_profile(&state_->device, 0U, kHeaterTemperatureC, kHeaterDurationMs);
    if (err != ESP_OK) {
        setError("Failed to configure BME680 gas heater profile.");
        return err;
    }

    err = bme680_use_heater_profile(&state_->device, 0);
    if (err != ESP_OK) {
        setError("Failed to enable BME680 gas heater profile.");
        return err;
    }

    err = bme680_set_ambient_temperature(&state_->device, kAmbientTemperatureC);
    if (err != ESP_OK) {
        setError("Failed to configure BME680 ambient temperature.");
        return err;
    }

    return ESP_OK;
}

void Bme680Sensor::reset() {
    initialized_ = false;
    if (!state_) {
        return;
    }

    if (state_->descriptor_initialized) {
        bme680_free_desc(&state_->device);
        std::memset(&state_->device, 0, sizeof(state_->device));
        state_->descriptor_initialized = false;
    }

    poll_failure_count_ = 0U;
}

void Bme680Sensor::setError(const std::string& message) {
    last_error_ = message;
}

std::unique_ptr<SensorDriver> createBme680Sensor() {
    return std::make_unique<Bme680Sensor>();
}

}  // namespace air360
