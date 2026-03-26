#include "air360/sensors/drivers/bme680_sensor.hpp"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

extern "C" {
#include "bme68x.h"
}

#include "air360/sensors/transport_binding.hpp"
#include "esp_timer.h"

namespace air360 {

struct Bme680DriverState {
    bme68x_dev device{};
    bme68x_conf conf{};
    bme68x_heatr_conf heatr_conf{};
};

namespace {

constexpr std::uint32_t kAmbientTemperatureC = 25U;
constexpr std::uint8_t kOversampling = BME68X_OS_2X;
constexpr std::uint16_t kHeaterTemperatureC = 300U;
constexpr std::uint16_t kHeaterDurationMs = 100U;
constexpr std::uint32_t kMeasurementSlackUs = 1000U;

esp_err_t mapResultToEspErr(int8_t result) {
    switch (result) {
        case BME68X_OK:
            return ESP_OK;
        case BME68X_E_DEV_NOT_FOUND:
            return ESP_ERR_NOT_FOUND;
        case BME68X_E_INVALID_LENGTH:
        case BME68X_E_NULL_PTR:
            return ESP_ERR_INVALID_ARG;
        case BME68X_E_COM_FAIL:
            return ESP_FAIL;
        case BME68X_W_NO_NEW_DATA:
            return ESP_ERR_NOT_FOUND;
        default:
            return ESP_FAIL;
    }
}

std::string describeResult(int8_t result) {
    switch (result) {
        case BME68X_OK:
            return "ok";
        case BME68X_E_NULL_PTR:
            return "null pointer";
        case BME68X_E_COM_FAIL:
            return "communication failure";
        case BME68X_E_DEV_NOT_FOUND:
            return "sensor not found";
        case BME68X_E_INVALID_LENGTH:
            return "invalid length";
        case BME68X_E_SELF_TEST:
            return "self test failed";
        case BME68X_W_DEFINE_OP_MODE:
            return "invalid operation mode";
        case BME68X_W_NO_NEW_DATA:
            return "no new data";
        case BME68X_W_DEFINE_SHD_HEATR_DUR:
            return "heater duration not defined";
        default:
            return "unknown bme68x error";
    }
}

BME68X_INTF_RET_TYPE readCallback(
    std::uint8_t reg_addr,
    std::uint8_t* reg_data,
    std::uint32_t length,
    void* intf_ptr) {
    auto* context = static_cast<BoschI2cContext*>(intf_ptr);
    if (context == nullptr) {
        return static_cast<BME68X_INTF_RET_TYPE>(-1);
    }

    const esp_err_t err = boschI2cRead(*context, reg_addr, reg_data, length);
    return err == ESP_OK ? BME68X_INTF_RET_SUCCESS : static_cast<BME68X_INTF_RET_TYPE>(-1);
}

BME68X_INTF_RET_TYPE writeCallback(
    std::uint8_t reg_addr,
    const std::uint8_t* reg_data,
    std::uint32_t length,
    void* intf_ptr) {
    auto* context = static_cast<BoschI2cContext*>(intf_ptr);
    if (context == nullptr) {
        return static_cast<BME68X_INTF_RET_TYPE>(-1);
    }

    const esp_err_t err = boschI2cWrite(*context, reg_addr, reg_data, length);
    return err == ESP_OK ? BME68X_INTF_RET_SUCCESS : static_cast<BME68X_INTF_RET_TYPE>(-1);
}

void delayUs(std::uint32_t period_us, void* intf_ptr) {
    static_cast<void>(intf_ptr);
    boschDelayUs(period_us);
}

}  // namespace

Bme680Sensor::~Bme680Sensor() {
    delete state_;
    state_ = nullptr;
}

SensorType Bme680Sensor::type() const {
    return SensorType::kBme680;
}

esp_err_t Bme680Sensor::init(
    const SensorRecord& record,
    const SensorDriverContext& context) {
    record_ = record;
    measurement_.clear();
    last_error_.clear();
    initialized_ = false;

    if (context.i2c_bus_manager == nullptr) {
        setError("I2C bus manager is unavailable.");
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t probe_err = context.i2c_bus_manager->probe(record.i2c_bus_id, record.i2c_address);
    if (probe_err != ESP_OK) {
        setError("BME680 probe failed on the selected I2C bus and address.");
        return probe_err;
    }

    interface_context_.bus_manager = context.i2c_bus_manager;
    interface_context_.bus_id = record.i2c_bus_id;
    interface_context_.address = record.i2c_address;

    delete state_;
    state_ = new Bme680DriverState{};

    std::memset(&state_->device, 0, sizeof(state_->device));
    std::memset(&state_->conf, 0, sizeof(state_->conf));
    std::memset(&state_->heatr_conf, 0, sizeof(state_->heatr_conf));

    state_->device.intf = BME68X_I2C_INTF;
    state_->device.intf_ptr = &interface_context_;
    state_->device.read = &readCallback;
    state_->device.write = &writeCallback;
    state_->device.delay_us = &delayUs;
    state_->device.amb_temp = static_cast<int8_t>(kAmbientTemperatureC);

    const int8_t init_result = bme68x_init(&state_->device);
    if (init_result != BME68X_OK) {
        setError(std::string("Failed to initialize BME680: ") + describeResult(init_result) + ".");
        delete state_;
        state_ = nullptr;
        return mapResultToEspErr(init_result);
    }

    const esp_err_t configure_err = configureSensor();
    if (configure_err != ESP_OK) {
        delete state_;
        state_ = nullptr;
        return configure_err;
    }

    initialized_ = true;
    return ESP_OK;
}

esp_err_t Bme680Sensor::poll() {
    if (!initialized_ || state_ == nullptr) {
        setError("BME680 is not initialized.");
        return ESP_ERR_INVALID_STATE;
    }

    int8_t result = bme68x_set_op_mode(BME68X_FORCED_MODE, &state_->device);
    if (result != BME68X_OK) {
        setError(std::string("Failed to start BME680 forced measurement: ") + describeResult(result) + ".");
        initialized_ = false;
        return mapResultToEspErr(result);
    }

    const std::uint32_t measurement_duration_us =
        bme68x_get_meas_dur(BME68X_FORCED_MODE, &state_->conf, &state_->device) +
        (static_cast<std::uint32_t>(state_->heatr_conf.heatr_dur) * 1000U) + kMeasurementSlackUs;
    boschDelayUs(measurement_duration_us);

    bme68x_data data{};
    std::uint8_t field_count = 0U;
    result = bme68x_get_data(BME68X_FORCED_MODE, &data, &field_count, &state_->device);
    if (result != BME68X_OK && result != BME68X_W_NO_NEW_DATA) {
        setError(std::string("Failed to read BME680 measurement: ") + describeResult(result) + ".");
        initialized_ = false;
        return mapResultToEspErr(result);
    }

    if (field_count == 0U || (data.status & BME68X_NEW_DATA_MSK) == 0U) {
        setError("BME680 returned no fresh sample.");
        return ESP_ERR_NOT_FOUND;
    }

    measurement_.clear();
    measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
    measurement_.addValue(SensorValueKind::kTemperatureC, data.temperature);
    measurement_.addValue(SensorValueKind::kHumidityPercent, data.humidity);
    measurement_.addValue(SensorValueKind::kPressureHpa, data.pressure / 100.0F);

    if ((data.status & BME68X_GASM_VALID_MSK) != 0U &&
        (data.status & BME68X_HEAT_STAB_MSK) != 0U) {
        measurement_.addValue(SensorValueKind::kGasResistanceOhms, data.gas_resistance);
    }

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
    state_->conf.filter = BME68X_FILTER_OFF;
    state_->conf.odr = BME68X_ODR_NONE;
    state_->conf.os_hum = kOversampling;
    state_->conf.os_pres = kOversampling;
    state_->conf.os_temp = kOversampling;

    int8_t result = bme68x_set_conf(&state_->conf, &state_->device);
    if (result != BME68X_OK) {
        setError(std::string("Failed to configure BME680 oversampling: ") + describeResult(result) + ".");
        return mapResultToEspErr(result);
    }

    state_->heatr_conf.enable = BME68X_ENABLE;
    state_->heatr_conf.heatr_temp = kHeaterTemperatureC;
    state_->heatr_conf.heatr_dur = kHeaterDurationMs;
    state_->heatr_conf.heatr_temp_prof = nullptr;
    state_->heatr_conf.heatr_dur_prof = nullptr;
    state_->heatr_conf.profile_len = 0U;
    state_->heatr_conf.shared_heatr_dur = 0U;

    result = bme68x_set_heatr_conf(BME68X_FORCED_MODE, &state_->heatr_conf, &state_->device);
    if (result != BME68X_OK) {
        setError(std::string("Failed to configure BME680 gas heater: ") + describeResult(result) + ".");
        return mapResultToEspErr(result);
    }

    return ESP_OK;
}

void Bme680Sensor::setError(const std::string& message) {
    last_error_ = message;
}

std::unique_ptr<SensorDriver> createBme680Sensor() {
    return std::make_unique<Bme680Sensor>();
}

}  // namespace air360
