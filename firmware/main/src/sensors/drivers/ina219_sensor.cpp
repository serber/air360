#include "air360/sensors/drivers/ina219_sensor.hpp"

#include <cstring>
#include <memory>
#include <string>

#include "air360/sensors/transport_binding.hpp"
#include "esp_timer.h"
#include "ina219.h"

namespace air360 {

namespace {

constexpr float kShuntResistanceOhm = 0.1F;  // 100 mOhm on standard module
constexpr std::uint32_t kIna219I2cSpeedHz = 100000U;

}  // namespace

Ina219Sensor::~Ina219Sensor() {
    reset();
}

SensorType Ina219Sensor::type() const {
    return SensorType::kIna219;
}

esp_err_t Ina219Sensor::init(const SensorRecord& record, const SensorDriverContext& context) {
    reset();
    measurement_.clear();
    last_error_.clear();

    i2c_port_t port = I2C_NUM_0;
    gpio_num_t sda = GPIO_NUM_NC;
    gpio_num_t scl = GPIO_NUM_NC;
    if (!context.i2c_bus_manager->resolvePins(record.i2c_bus_id, port, sda, scl)) {
        setError("Unknown I2C bus id for INA219.");
        return ESP_ERR_NOT_SUPPORTED;
    }

    std::memset(&device_, 0, sizeof(device_));
    esp_err_t err = ina219_init_desc(&device_, record.i2c_address, port, sda, scl);
    if (err != ESP_OK) {
        setError(std::string("Failed to initialize INA219 descriptor: ") + esp_err_to_name(err));
        reset();
        return err;
    }
    descriptor_initialized_ = true;
    device_.i2c_dev.cfg.master.clk_speed = kIna219I2cSpeedHz;

    err = ina219_init(&device_);
    if (err != ESP_OK) {
        setError(std::string("Failed to initialize INA219: ") + esp_err_to_name(err));
        reset();
        return err;
    }

    err = ina219_configure(
        &device_,
        INA219_BUS_RANGE_32V,
        INA219_GAIN_0_125,
        INA219_RES_12BIT_1S,
        INA219_RES_12BIT_1S,
        INA219_MODE_CONT_SHUNT_BUS);
    if (err != ESP_OK) {
        setError(std::string("Failed to configure INA219: ") + esp_err_to_name(err));
        reset();
        return err;
    }

    err = ina219_calibrate(&device_, kShuntResistanceOhm);
    if (err != ESP_OK) {
        setError(std::string("Failed to calibrate INA219: ") + esp_err_to_name(err));
        reset();
        return err;
    }

    initialized_ = true;
    last_error_.clear();
    return ESP_OK;
}

esp_err_t Ina219Sensor::poll() {
    if (!initialized_ || !descriptor_initialized_) {
        setError("INA219 is not initialized.");
        return ESP_ERR_INVALID_STATE;
    }

    float bus_voltage_v = 0.0F;
    esp_err_t err = ina219_get_bus_voltage(&device_, &bus_voltage_v);
    if (err != ESP_OK) {
        setError(std::string("Failed to read INA219 bus voltage: ") + esp_err_to_name(err));
        initialized_ = false;
        return err;
    }

    float current_a = 0.0F;
    err = ina219_get_current(&device_, &current_a);
    if (err != ESP_OK) {
        setError(std::string("Failed to read INA219 current: ") + esp_err_to_name(err));
        initialized_ = false;
        return err;
    }

    float power_w = 0.0F;
    err = ina219_get_power(&device_, &power_w);
    if (err != ESP_OK) {
        setError(std::string("Failed to read INA219 power: ") + esp_err_to_name(err));
        initialized_ = false;
        return err;
    }

    measurement_.clear();
    measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
    measurement_.addValue(SensorValueKind::kVoltageMv, bus_voltage_v * 1000.0F);
    measurement_.addValue(SensorValueKind::kCurrentMa, current_a * 1000.0F);
    measurement_.addValue(SensorValueKind::kPowerMw, power_w * 1000.0F);
    last_error_.clear();
    return ESP_OK;
}

SensorMeasurement Ina219Sensor::latestMeasurement() const {
    return measurement_;
}

std::string Ina219Sensor::lastError() const {
    return last_error_;
}

void Ina219Sensor::reset() {
    initialized_ = false;
    if (descriptor_initialized_) {
        ina219_free_desc(&device_);
        std::memset(&device_, 0, sizeof(device_));
        descriptor_initialized_ = false;
    }
}

void Ina219Sensor::setError(const std::string& message) {
    last_error_ = message;
}

std::unique_ptr<SensorDriver> createIna219Sensor() {
    return std::make_unique<Ina219Sensor>();
}

}  // namespace air360
