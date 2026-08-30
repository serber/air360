#include "air360/sensors/drivers/ina226_sensor.hpp"

#include <memory>
#include <string>

#include "air360/sensors/transport_binding.hpp"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.sensor.ina226";
// Standard R100 shunt, same family as the INA219 breakout modules already
// supported. The INA226 shunt ADC full-scale range is only +-81.92 mV, so a
// 100 mOhm shunt tops out around 0.8 A -- fine for monitoring the device's own
// consumption, unlike INA219's wider +-320 mV gain-selectable range.
constexpr float kShuntResistanceOhm = 0.1F;
constexpr float kMaxCurrentA = 0.8F;
constexpr std::uint32_t kIna226I2cSpeedHz = 100000U;

}  // namespace

Ina226Sensor::~Ina226Sensor() {
    teardown();
}

SensorType Ina226Sensor::type() const {
    return SensorType::kIna226;
}

esp_err_t Ina226Sensor::init(const SensorRecord& record, const SensorDriverContext& context) {
    teardown();
    measurement_.clear();
    clearError();
    soft_fail_policy_.onPollOk();

    i2c_master_bus_handle_t bus_handle = nullptr;
    esp_err_t err = context.i2c_bus_manager->getMasterBusHandle(record.i2c_bus_id, bus_handle);
    if (err != ESP_OK) {
        setError(std::string("Failed to get I2C master bus handle for INA226: ") + esp_err_to_name(err));
        return err;
    }

    ina226_config_t config{};
    config.i2c_address = record.i2c_address;
    config.i2c_clock_speed = kIna226I2cSpeedHz;
    config.averaging_mode = INA226_AVG_MODE_1;
    config.shunt_voltage_conv_time = INA226_VOLT_CONV_TIME_1_1MS;
    config.bus_voltage_conv_time = INA226_VOLT_CONV_TIME_1_1MS;
    config.operating_mode = INA226_OP_MODE_CONT_SHUNT_BUS;
    config.shunt_resistance = kShuntResistanceOhm;
    config.max_current = kMaxCurrentA;

    err = ina226_init(bus_handle, &config, &handle_);
    if (err != ESP_OK) {
        setError(std::string("Failed to initialize INA226 (chip ID mismatch or I2C error): ") +
                 esp_err_to_name(err));
        handle_ = nullptr;
        return err;
    }

    err = ina226_calibrate(handle_, kMaxCurrentA, kShuntResistanceOhm);
    if (err != ESP_OK) {
        setError(std::string("Failed to calibrate INA226: ") + esp_err_to_name(err));
        teardown();
        return err;
    }

    initialized_ = true;
    return ESP_OK;
}

esp_err_t Ina226Sensor::poll() {
    if (!initialized_ || handle_ == nullptr) {
        setError("INA226 is not initialized.");
        return ESP_ERR_INVALID_STATE;
    }

    float bus_voltage_v = 0.0F;
    if (esp_err_t err = ina226_get_bus_voltage(handle_, &bus_voltage_v); err != ESP_OK) {
        return reportPollFailure(
            kTag,
            std::string("Failed to read INA226 bus voltage: ") + esp_err_to_name(err),
            err);
    }

    float current_a = 0.0F;
    if (esp_err_t err = ina226_get_current(handle_, &current_a); err != ESP_OK) {
        return reportPollFailure(
            kTag,
            std::string("Failed to read INA226 current: ") + esp_err_to_name(err),
            err);
    }

    float power_w = 0.0F;
    if (esp_err_t err = ina226_get_power(handle_, &power_w); err != ESP_OK) {
        return reportPollFailure(
            kTag,
            std::string("Failed to read INA226 power: ") + esp_err_to_name(err),
            err);
    }

    measurement_.clear();
    measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
    measurement_.addValue(SensorValueKind::kVoltageMv, bus_voltage_v * 1000.0F);
    measurement_.addValue(SensorValueKind::kCurrentMa, current_a * 1000.0F);
    measurement_.addValue(SensorValueKind::kPowerMw, power_w * 1000.0F);
    notePollSuccess();
    return ESP_OK;
}

SensorMeasurement Ina226Sensor::latestMeasurement() const {
    return measurement_;
}

void Ina226Sensor::teardown() {
    initialized_ = false;
    soft_fail_policy_.onPollOk();
    if (handle_ != nullptr) {
        // ina226_delete() releases the device from the shared I2C master bus;
        // the bus itself is owned by I2cBusManager and must not be deleted here.
        if (esp_err_t err = ina226_delete(handle_); err != ESP_OK) {
            ESP_LOGW(kTag, "Failed to delete INA226 device: %s", esp_err_to_name(err));
        }
        handle_ = nullptr;
    }
}

std::unique_ptr<SensorDriver> createIna226Sensor() {
    return std::make_unique<Ina226Sensor>();
}

}  // namespace air360
