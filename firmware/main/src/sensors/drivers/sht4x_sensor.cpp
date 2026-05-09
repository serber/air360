#include "air360/sensors/drivers/sht4x_sensor.hpp"

#include <cmath>
#include <cstring>
#include <memory>
#include <string>

#include "air360/sensors/transport_binding.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "sht4x.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.sensor.sht4x";
// Standard-mode I2C is sufficient for second-scale environmental polling and
// avoids making this driver the one that dictates a faster shared bus speed.
constexpr std::uint32_t kSht4xI2cSpeedHz = 100000U;

}  // namespace

Sht4xSensor::~Sht4xSensor() {
    teardown();
}

SensorType Sht4xSensor::type() const {
    return SensorType::kSht4x;
}

esp_err_t Sht4xSensor::init(const SensorRecord& record, const SensorDriverContext& context) {
    teardown();
    record_ = record;
    measurement_.clear();
    clearError();
    soft_fail_policy_.onPollOk();

    i2c_port_t port = I2C_NUM_0;
    gpio_num_t sda = GPIO_NUM_NC;
    gpio_num_t scl = GPIO_NUM_NC;
    if (!context.i2c_bus_manager->resolvePins(record.i2c_bus_id, port, sda, scl)) {
        setError("Unknown I2C bus id for SHT4X.");
        return ESP_ERR_NOT_SUPPORTED;
    }

    std::memset(&device_, 0, sizeof(device_));
    esp_err_t err = sht4x_init_desc(&device_, port, sda, scl);
    if (err != ESP_OK) {
        setError("Failed to initialize SHT4X descriptor.");
        teardown();
        return err;
    }
    descriptor_initialized_ = true;
    device_.i2c_dev.cfg.master.clk_speed = kSht4xI2cSpeedHz;
    device_.i2c_dev.cfg.sda_pullup_en = 1;
    device_.i2c_dev.cfg.scl_pullup_en = 1;

    device_.repeatability = SHT4X_HIGH;
    device_.heater = SHT4X_HEATER_OFF;
    err = sht4x_reset(&device_);
    if (err != ESP_OK) {
        setError(std::string("Failed to initialize SHT4X sensor: ") + esp_err_to_name(err));
        teardown();
        return err;
    }

    initialized_ = true;
    return ESP_OK;
}

esp_err_t Sht4xSensor::poll() {
    if (!initialized_ || !descriptor_initialized_) {
        setError("SHT4X sensor is not initialized.");
        return ESP_ERR_INVALID_STATE;
    }

    float temperature_c = 0.0F;
    float humidity_percent = 0.0F;
    if (esp_err_t err = sht4x_measure(&device_, &temperature_c, &humidity_percent);
        err != ESP_OK) {
        return reportPollFailure(
            kTag,
            std::string("Failed to read SHT4X measurement: ") + esp_err_to_name(err),
            err);
    }

    if (std::isnan(temperature_c) || std::isnan(humidity_percent)) {
        return reportPollFailure(
            kTag, "SHT4X driver returned invalid values.", ESP_ERR_INVALID_RESPONSE);
    }

    measurement_.clear();
    measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
    measurement_.addValue(SensorValueKind::kTemperatureC, temperature_c);
    measurement_.addValue(SensorValueKind::kHumidityPercent, humidity_percent);
    notePollSuccess();
    return ESP_OK;
}

SensorMeasurement Sht4xSensor::latestMeasurement() const {
    return measurement_;
}

void Sht4xSensor::teardown() {
    initialized_ = false;
    soft_fail_policy_.onPollOk();
    if (descriptor_initialized_) {
        sht4x_free_desc(&device_);
        std::memset(&device_, 0, sizeof(device_));
        descriptor_initialized_ = false;
    }
}

std::unique_ptr<SensorDriver> createSht4xSensor() {
    return std::make_unique<Sht4xSensor>();
}

}  // namespace air360
