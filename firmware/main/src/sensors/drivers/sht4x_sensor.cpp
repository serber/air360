#include "air360/sensors/drivers/sht4x_sensor.hpp"

#include <cmath>
#include <cstring>
#include <memory>
#include <string>

#include "esp_timer.h"
#include "i2cdev.h"
#include "sdkconfig.h"
#include "sht4x.h"

namespace air360 {

namespace {

#ifndef CONFIG_AIR360_I2C0_SDA_GPIO
#define CONFIG_AIR360_I2C0_SDA_GPIO 8
#endif

#ifndef CONFIG_AIR360_I2C0_SCL_GPIO
#define CONFIG_AIR360_I2C0_SCL_GPIO 9
#endif

constexpr i2c_port_t kSht4xI2cPort = I2C_NUM_0;
constexpr gpio_num_t kSht4xSdaPin = static_cast<gpio_num_t>(CONFIG_AIR360_I2C0_SDA_GPIO);
constexpr gpio_num_t kSht4xSclPin = static_cast<gpio_num_t>(CONFIG_AIR360_I2C0_SCL_GPIO);
constexpr std::uint32_t kSht4xI2cSpeedHz = 100000U;

}  // namespace

Sht4xSensor::~Sht4xSensor() {
    reset();
}

SensorType Sht4xSensor::type() const {
    return SensorType::kSht4x;
}

esp_err_t Sht4xSensor::init(const SensorRecord& record, const SensorDriverContext& context) {
    static_cast<void>(context);
    reset();
    record_ = record;
    measurement_.clear();
    last_error_.clear();

    esp_err_t err = i2cdev_init();
    if (err != ESP_OK) {
        setError("Failed to initialize i2cdev subsystem for SHT4X.");
        return err;
    }

    std::memset(&device_, 0, sizeof(device_));
    err = sht4x_init_desc(&device_, kSht4xI2cPort, kSht4xSdaPin, kSht4xSclPin);
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
        initialized_ = false;
        return err;
    }

    if (std::isnan(temperature_c) || std::isnan(humidity_percent)) {
        setError("SHT4X driver returned invalid values.");
        initialized_ = false;
        return ESP_ERR_INVALID_RESPONSE;
    }

    measurement_.clear();
    measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
    measurement_.addValue(SensorValueKind::kTemperatureC, temperature_c);
    measurement_.addValue(SensorValueKind::kHumidityPercent, humidity_percent);
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
