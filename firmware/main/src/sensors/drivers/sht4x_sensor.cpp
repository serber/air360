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
constexpr std::uint32_t kSht4xI2cSpeedHz = 100000U;

}  // namespace

Sht4xSensor::~Sht4xSensor() {
    reset();
}

SensorType Sht4xSensor::type() const {
    return SensorType::kSht4x;
}

esp_err_t Sht4xSensor::init(const SensorRecord& record, const SensorDriverContext& context) {
    reset();
    record_ = record;
    measurement_.clear();
    last_error_.clear();

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
        reset();
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
        reset();
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
    esp_err_t err = sht4x_measure(&device_, &temperature_c, &humidity_percent);
    if (err != ESP_OK) {
        setError(std::string("Failed to read SHT4X measurement: ") + esp_err_to_name(err));
        if (soft_fail_policy_.onPollErr()) {
            ESP_LOGE(kTag, "hard error after %u soft fails: %s", kSensorPollFailureReinitThreshold, last_error_.c_str());
            initialized_ = false;
        } else if (soft_fail_policy_.soft_fails == 1U) {
            ESP_LOGW(kTag, "soft fail 1/%u: %s", kSensorPollFailureReinitThreshold, last_error_.c_str());
        }
        return err;
    }

    if (std::isnan(temperature_c) || std::isnan(humidity_percent)) {
        setError("SHT4X driver returned invalid values.");
        if (soft_fail_policy_.onPollErr()) {
            ESP_LOGE(kTag, "hard error after %u soft fails: %s", kSensorPollFailureReinitThreshold, last_error_.c_str());
            initialized_ = false;
        } else if (soft_fail_policy_.soft_fails == 1U) {
            ESP_LOGW(kTag, "soft fail 1/%u: %s", kSensorPollFailureReinitThreshold, last_error_.c_str());
        }
        return ESP_ERR_INVALID_RESPONSE;
    }

    measurement_.clear();
    measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
    measurement_.addValue(SensorValueKind::kTemperatureC, temperature_c);
    measurement_.addValue(SensorValueKind::kHumidityPercent, humidity_percent);
    soft_fail_policy_.onPollOk();
    last_error_.clear();
    return ESP_OK;
}

SensorMeasurement Sht4xSensor::latestMeasurement() const {
    return measurement_;
}

std::string Sht4xSensor::lastError() const {
    return last_error_;
}

void Sht4xSensor::reset() {
    initialized_ = false;
    soft_fail_policy_.onPollOk();
    if (descriptor_initialized_) {
        sht4x_free_desc(&device_);
        std::memset(&device_, 0, sizeof(device_));
        descriptor_initialized_ = false;
    }
}

void Sht4xSensor::setError(const std::string& message) {
    last_error_ = message;
}

std::unique_ptr<SensorDriver> createSht4xSensor() {
    return std::make_unique<Sht4xSensor>();
}

}  // namespace air360
