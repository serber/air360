#include "air360/sensors/drivers/veml7700_sensor.hpp"

#include <cstring>
#include <memory>
#include <string>

#include "esp_timer.h"
#include "i2cdev.h"
#include "sdkconfig.h"

namespace air360 {

namespace {

#ifndef CONFIG_AIR360_I2C0_SDA_GPIO
#define CONFIG_AIR360_I2C0_SDA_GPIO 8
#endif

#ifndef CONFIG_AIR360_I2C0_SCL_GPIO
#define CONFIG_AIR360_I2C0_SCL_GPIO 9
#endif

constexpr i2c_port_t kVeml7700I2cPort = I2C_NUM_0;
constexpr gpio_num_t kVeml7700SdaPin = static_cast<gpio_num_t>(CONFIG_AIR360_I2C0_SDA_GPIO);
constexpr gpio_num_t kVeml7700SclPin = static_cast<gpio_num_t>(CONFIG_AIR360_I2C0_SCL_GPIO);
constexpr std::uint32_t kVeml7700I2cSpeedHz = 100000U;

}  // namespace

Veml7700Sensor::~Veml7700Sensor() {
    reset();
}

SensorType Veml7700Sensor::type() const {
    return SensorType::kVeml7700;
}

esp_err_t Veml7700Sensor::init(
    const SensorRecord& record,
    const SensorDriverContext& context) {
    static_cast<void>(context);
    reset();
    record_ = record;
    measurement_.clear();
    last_error_.clear();

    esp_err_t err = i2cdev_init();
    if (err != ESP_OK) {
        setError("Failed to initialize i2cdev subsystem for VEML7700.");
        return err;
    }

    std::memset(&device_, 0, sizeof(device_));
    std::memset(&config_, 0, sizeof(config_));
    err = veml7700_init_desc(&device_, kVeml7700I2cPort, kVeml7700SdaPin, kVeml7700SclPin);
    if (err != ESP_OK) {
        setError("Failed to initialize VEML7700 descriptor.");
        reset();
        return err;
    }

    descriptor_initialized_ = true;
    device_.cfg.master.clk_speed = kVeml7700I2cSpeedHz;
    device_.cfg.sda_pullup_en = 1;
    device_.cfg.scl_pullup_en = 1;

    err = veml7700_probe(&device_);
    if (err != ESP_OK) {
        setError(std::string("Failed to detect VEML7700 sensor: ") + esp_err_to_name(err));
        reset();
        return err;
    }

    config_.gain = VEML7700_GAIN_DIV_8;
    config_.integration_time = VEML7700_INTEGRATION_TIME_100MS;
    config_.persistence_protect = VEML7700_PERSISTENCE_PROTECTION_1;
    config_.interrupt_enable = 0;
    config_.shutdown = 0;
    config_.threshold_high = 0U;
    config_.threshold_low = 0U;
    config_.power_saving_mode = VEML7700_POWER_SAVING_MODE_500MS;
    config_.power_saving_enable = 0;

    err = veml7700_set_config(&device_, &config_);
    if (err != ESP_OK) {
        setError(std::string("Failed to configure VEML7700 sensor: ") + esp_err_to_name(err));
        reset();
        return err;
    }

    initialized_ = true;
    return ESP_OK;
}

esp_err_t Veml7700Sensor::poll() {
    if (!initialized_ || !descriptor_initialized_) {
        setError("VEML7700 is not initialized.");
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t illuminance_lux = 0U;
    const esp_err_t err = veml7700_get_ambient_light(&device_, &config_, &illuminance_lux);
    if (err != ESP_OK) {
        setError(std::string("Failed to read VEML7700 ambient light: ") + esp_err_to_name(err));
        initialized_ = false;
        return err;
    }

    measurement_.clear();
    measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
    measurement_.addValue(SensorValueKind::kIlluminanceLux, static_cast<float>(illuminance_lux));
    last_error_.clear();
    return ESP_OK;
}

SensorMeasurement Veml7700Sensor::latestMeasurement() const {
    return measurement_;
}

std::string Veml7700Sensor::lastError() const {
    return last_error_;
}

void Veml7700Sensor::reset() {
    initialized_ = false;
    if (descriptor_initialized_) {
        veml7700_free_desc(&device_);
        std::memset(&device_, 0, sizeof(device_));
        descriptor_initialized_ = false;
    }
}

void Veml7700Sensor::setError(const std::string& message) {
    last_error_ = message;
}

std::unique_ptr<SensorDriver> createVeml7700Sensor() {
    return std::make_unique<Veml7700Sensor>();
}

}  // namespace air360
