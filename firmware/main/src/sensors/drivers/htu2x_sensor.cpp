#include "air360/sensors/drivers/htu2x_sensor.hpp"

#include <cmath>
#include <cstring>
#include <memory>
#include <string>

#include "air360/sensors/transport_binding.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "si7021.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.sensor.htu2x";
// Give HTU2X one second after power-up/reset before the first transaction;
// earlier reads are unreliable on cold boot.
constexpr std::uint32_t kHtu2xStartupDelayMs = 1000U;
// Use a conservative bus speed for compatibility with older HTU21D/Si7021-class parts.
constexpr std::uint32_t kHtu2xI2cSpeedHz = 100000U;

}  // namespace

Htu2xSensor::~Htu2xSensor() {
    teardown();
}

SensorType Htu2xSensor::type() const {
    return SensorType::kHtu2x;
}

esp_err_t Htu2xSensor::init(const SensorRecord& record, const SensorDriverContext& context) {
    teardown();
    record_ = record;
    measurement_.clear();
    clearError();
    soft_fail_policy_.onPollOk();

    i2c_port_t port = I2C_NUM_0;
    gpio_num_t sda = GPIO_NUM_NC;
    gpio_num_t scl = GPIO_NUM_NC;
    if (!context.i2c_bus_manager->resolvePins(record.i2c_bus_id, port, sda, scl)) {
        setError("Unknown I2C bus id for HTU2X.");
        return ESP_ERR_NOT_SUPPORTED;
    }

    std::memset(&device_, 0, sizeof(device_));
    esp_err_t err = si7021_init_desc(&device_, port, sda, scl);
    if (err != ESP_OK) {
        setError("Failed to initialize HTU2X descriptor.");
        teardown();
        return err;
    }
    descriptor_initialized_ = true;
    device_.cfg.master.clk_speed = kHtu2xI2cSpeedHz;
    device_.cfg.sda_pullup_en = 1;
    device_.cfg.scl_pullup_en = 1;

    // HTU21D-compatible parts may ignore the very first transaction right after boot.
    vTaskDelay(pdMS_TO_TICKS(kHtu2xStartupDelayMs));

    err = si7021_reset(&device_);
    if (err != ESP_OK) {
        setError(std::string("Failed to reset HTU2X: ") + esp_err_to_name(err));
        teardown();
        return err;
    }

    initialized_ = true;
    return ESP_OK;
}

esp_err_t Htu2xSensor::poll() {
    if (!initialized_ || !descriptor_initialized_) {
        setError("HTU2X sensor is not initialized.");
        return ESP_ERR_INVALID_STATE;
    }

    float temperature_c = 0.0F;
    if (esp_err_t err = si7021_measure_temperature(&device_, &temperature_c); err != ESP_OK) {
        return reportPollFailure(
            kTag,
            std::string("Failed to read HTU2X temperature: ") + esp_err_to_name(err),
            err);
    }

    float humidity_percent = 0.0F;
    if (esp_err_t err = si7021_measure_humidity(&device_, &humidity_percent); err != ESP_OK) {
        return reportPollFailure(
            kTag,
            std::string("Failed to read HTU2X humidity: ") + esp_err_to_name(err),
            err);
    }

    if (std::isnan(temperature_c) || std::isnan(humidity_percent)) {
        return reportPollFailure(
            kTag, "HTU2X driver returned invalid values.", ESP_ERR_INVALID_RESPONSE);
    }

    measurement_.clear();
    measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
    measurement_.addValue(SensorValueKind::kTemperatureC, temperature_c);
    measurement_.addValue(SensorValueKind::kHumidityPercent, humidity_percent);
    notePollSuccess();
    return ESP_OK;
}

SensorMeasurement Htu2xSensor::latestMeasurement() const {
    return measurement_;
}

void Htu2xSensor::teardown() {
    initialized_ = false;
    soft_fail_policy_.onPollOk();
    if (descriptor_initialized_) {
        si7021_free_desc(&device_);
        std::memset(&device_, 0, sizeof(device_));
        descriptor_initialized_ = false;
    }
}

std::unique_ptr<SensorDriver> createHtu2xSensor() {
    return std::make_unique<Htu2xSensor>();
}

}  // namespace air360
