#include "air360/sensors/drivers/veml7700_sensor.hpp"

#include <cstring>
#include <memory>
#include <string>

#include "air360/sensors/transport_binding.hpp"
#include "esp_log.h"
#include "esp_timer.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.sensor.veml7700";
// Lux readings are infrequent and low-bandwidth, so standard-mode I2C is enough.
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
    reset();
    record_ = record;
    measurement_.clear();
    last_error_.clear();

    i2c_port_t port = I2C_NUM_0;
    gpio_num_t sda = GPIO_NUM_NC;
    gpio_num_t scl = GPIO_NUM_NC;
    if (!context.i2c_bus_manager->resolvePins(record.i2c_bus_id, port, sda, scl)) {
        setError("Unknown I2C bus id for VEML7700.");
        return ESP_ERR_NOT_SUPPORTED;
    }

    std::memset(&device_, 0, sizeof(device_));
    std::memset(&config_, 0, sizeof(config_));
    esp_err_t err = veml7700_init_desc(&device_, port, sda, scl);
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
        if (soft_fail_policy_.onPollErr()) {
            ESP_LOGE(kTag, "hard error after %u soft fails: %s", kSensorPollFailureReinitThreshold, last_error_.c_str());
            initialized_ = false;
        } else if (soft_fail_policy_.soft_fails == 1U) {
            ESP_LOGW(kTag, "soft fail 1/%u: %s", kSensorPollFailureReinitThreshold, last_error_.c_str());
        }
        return err;
    }

    measurement_.clear();
    measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
    measurement_.addValue(SensorValueKind::kIlluminanceLux, static_cast<float>(illuminance_lux));
    soft_fail_policy_.onPollOk();
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
    soft_fail_policy_.onPollOk();
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
