#include "air360/sensors/drivers/sht3x_sensor.hpp"

#include <cmath>
#include <cstring>
#include <memory>
#include <string>

#include "air360/sensors/transport_binding.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "sht3x.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.sensor.sht3x";
// Standard-mode I2C is enough for the default second-scale polling and keeps
// the shared sensor bus timing aligned with the other humidity drivers.
constexpr std::uint32_t kSht3xI2cSpeedHz = 100000U;

}  // namespace

Sht3xSensor::~Sht3xSensor() {
    teardown();
}

SensorType Sht3xSensor::type() const {
    return SensorType::kSht3x;
}

esp_err_t Sht3xSensor::init(const SensorRecord& record, const SensorDriverContext& context) {
    teardown();
    record_ = record;
    measurement_.clear();
    clearError();
    soft_fail_policy_.onPollOk();

    i2c_port_t port = I2C_NUM_0;
    gpio_num_t sda = GPIO_NUM_NC;
    gpio_num_t scl = GPIO_NUM_NC;
    if (!context.i2c_bus_manager->resolvePins(record.i2c_bus_id, port, sda, scl)) {
        setError("Unknown I2C bus id for SHT3X.");
        return ESP_ERR_NOT_SUPPORTED;
    }

    std::memset(&device_, 0, sizeof(device_));
    esp_err_t err = sht3x_init_desc(&device_, record.i2c_address, port, sda, scl);
    if (err != ESP_OK) {
        setError("Failed to initialize SHT3X descriptor.");
        teardown();
        return err;
    }
    descriptor_initialized_ = true;
    device_.i2c_dev.cfg.master.clk_speed = kSht3xI2cSpeedHz;
    device_.i2c_dev.cfg.sda_pullup_en = 1;
    device_.i2c_dev.cfg.scl_pullup_en = 1;

    err = sht3x_init(&device_);
    if (err != ESP_OK) {
        setError(std::string("Failed to initialize SHT3X sensor: ") + esp_err_to_name(err));
        teardown();
        return err;
    }
    err = sht3x_set_heater(&device_, false);
    if (err != ESP_OK) {
        setError(std::string("Failed to disable SHT3X heater: ") + esp_err_to_name(err));
        teardown();
        return err;
    }

    initialized_ = true;
    return ESP_OK;
}

esp_err_t Sht3xSensor::poll() {
    if (!initialized_ || !descriptor_initialized_) {
        setError("SHT3X sensor is not initialized.");
        return ESP_ERR_INVALID_STATE;
    }

    float temperature_c = 0.0F;
    float humidity_percent = 0.0F;
    if (esp_err_t err = sht3x_measure(&device_, &temperature_c, &humidity_percent);
        err != ESP_OK) {
        return reportPollFailure(
            kTag,
            std::string("Failed to read SHT3X measurement: ") + esp_err_to_name(err),
            err);
    }

    if (std::isnan(temperature_c) || std::isnan(humidity_percent)) {
        return reportPollFailure(
            kTag, "SHT3X driver returned invalid values.", ESP_ERR_INVALID_RESPONSE);
    }

    measurement_.clear();
    measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
    measurement_.addValue(SensorValueKind::kTemperatureC, temperature_c);
    measurement_.addValue(SensorValueKind::kHumidityPercent, humidity_percent);
    notePollSuccess();
    return ESP_OK;
}

SensorMeasurement Sht3xSensor::latestMeasurement() const {
    return measurement_;
}

void Sht3xSensor::teardown() {
    initialized_ = false;
    soft_fail_policy_.onPollOk();
    if (descriptor_initialized_) {
        sht3x_free_desc(&device_);
        std::memset(&device_, 0, sizeof(device_));
        descriptor_initialized_ = false;
    }
}

std::unique_ptr<SensorDriver> createSht3xSensor() {
    return std::make_unique<Sht3xSensor>();
}

}  // namespace air360
