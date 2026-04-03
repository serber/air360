#include "air360/sensors/drivers/bme280_sensor.hpp"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

extern "C" {
#include "bme280.h"
}

#include "air360/sensors/transport_binding.hpp"
#include "esp_timer.h"

namespace air360 {

struct Bme280DriverState {
    bme280_dev device{};
    bme280_settings settings{};
};

namespace {

constexpr std::uint8_t kOversampling = BME280_OVERSAMPLING_1X;
constexpr std::uint8_t kFilter = BME280_FILTER_COEFF_OFF;
constexpr std::uint32_t kMeasurementSlackUs = 1000U;
constexpr std::uint32_t kInitRetryDelayUs = 5000U;
constexpr std::uint8_t kInitAttempts = 2U;

esp_err_t mapResultToEspErr(int8_t result) {
    switch (result) {
        case BME280_OK:
            return ESP_OK;
        case BME280_E_DEV_NOT_FOUND:
            return ESP_ERR_NOT_FOUND;
        case BME280_E_INVALID_LEN:
        case BME280_E_NULL_PTR:
            return ESP_ERR_INVALID_ARG;
        case BME280_E_SLEEP_MODE_FAIL:
        case BME280_E_NVM_COPY_FAILED:
        case BME280_E_COMM_FAIL:
        default:
            return ESP_FAIL;
    }
}

std::string describeResult(int8_t result) {
    switch (result) {
        case BME280_OK:
            return "ok";
        case BME280_E_NULL_PTR:
            return "null pointer";
        case BME280_E_COMM_FAIL:
            return "communication failure";
        case BME280_E_INVALID_LEN:
            return "invalid length";
        case BME280_E_DEV_NOT_FOUND:
            return "sensor not found";
        case BME280_E_SLEEP_MODE_FAIL:
            return "failed to enter sleep mode";
        case BME280_E_NVM_COPY_FAILED:
            return "nvm copy failed";
        case BME280_W_INVALID_OSR_MACRO:
            return "invalid oversampling";
        default:
            return "unknown bme280 error";
    }
}

std::string formatChipId(std::uint8_t chip_id) {
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "0x%02x", static_cast<unsigned>(chip_id));
    return buffer;
}

BME280_INTF_RET_TYPE readCallback(
    std::uint8_t reg_addr,
    std::uint8_t* reg_data,
    std::uint32_t length,
    void* intf_ptr) {
    auto* context = static_cast<BoschI2cContext*>(intf_ptr);
    if (context == nullptr) {
        return static_cast<BME280_INTF_RET_TYPE>(-1);
    }

    const esp_err_t err = boschI2cRead(*context, reg_addr, reg_data, length);
    return err == ESP_OK ? BME280_INTF_RET_SUCCESS : static_cast<BME280_INTF_RET_TYPE>(-1);
}

BME280_INTF_RET_TYPE writeCallback(
    std::uint8_t reg_addr,
    const std::uint8_t* reg_data,
    std::uint32_t length,
    void* intf_ptr) {
    auto* context = static_cast<BoschI2cContext*>(intf_ptr);
    if (context == nullptr) {
        return static_cast<BME280_INTF_RET_TYPE>(-1);
    }

    const esp_err_t err = boschI2cWrite(*context, reg_addr, reg_data, length);
    return err == ESP_OK ? BME280_INTF_RET_SUCCESS : static_cast<BME280_INTF_RET_TYPE>(-1);
}

void delayUs(std::uint32_t period_us, void* intf_ptr) {
    static_cast<void>(intf_ptr);
    boschDelayUs(period_us);
}

}  // namespace

Bme280Sensor::~Bme280Sensor() {
    delete state_;
    state_ = nullptr;
}

SensorType Bme280Sensor::type() const {
    return SensorType::kBme280;
}

esp_err_t Bme280Sensor::init(
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

    interface_context_.bus_manager = context.i2c_bus_manager;
    interface_context_.bus_id = record.i2c_bus_id;
    interface_context_.address = record.i2c_address;

    delete state_;
    state_ = new Bme280DriverState{};
    std::memset(&state_->device, 0, sizeof(state_->device));
    std::memset(&state_->settings, 0, sizeof(state_->settings));

    state_->device.intf = BME280_I2C_INTF;
    state_->device.intf_ptr = &interface_context_;
    state_->device.read = &readCallback;
    state_->device.write = &writeCallback;
    state_->device.delay_us = &delayUs;

    std::uint8_t chip_id = 0U;
    const esp_err_t chip_id_err =
        boschI2cRead(interface_context_, BME280_REG_CHIP_ID, &chip_id, 1U);
    if (chip_id_err != ESP_OK) {
        setError("BME280 did not respond on the selected I2C bus and address.");
        delete state_;
        state_ = nullptr;
        return chip_id_err == ESP_ERR_TIMEOUT ? ESP_ERR_NOT_FOUND : chip_id_err;
    }

    if (chip_id != BME280_CHIP_ID) {
        setError(
            std::string("Unexpected chip id for BME280: ") + formatChipId(chip_id) + ".");
        delete state_;
        state_ = nullptr;
        return ESP_ERR_INVALID_RESPONSE;
    }

    int8_t init_result = BME280_E_COMM_FAIL;
    for (std::uint8_t attempt = 0U; attempt < kInitAttempts; ++attempt) {
        init_result = bme280_init(&state_->device);
        if (init_result == BME280_OK) {
            break;
        }

        if (attempt + 1U < kInitAttempts) {
            boschDelayUs(kInitRetryDelayUs);
        }
    }

    if (init_result != BME280_OK) {
        setError(std::string("Failed to initialize BME280: ") + describeResult(init_result) + ".");
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

esp_err_t Bme280Sensor::poll() {
    if (!initialized_ || state_ == nullptr) {
        setError("BME280 is not initialized.");
        return ESP_ERR_INVALID_STATE;
    }

    int8_t result = bme280_set_sensor_mode(BME280_POWERMODE_FORCED, &state_->device);
    if (result != BME280_OK) {
        setError(std::string("Failed to start BME280 forced measurement: ") + describeResult(result) + ".");
        initialized_ = false;
        return mapResultToEspErr(result);
    }

    std::uint32_t measurement_duration_us = 0U;
    result = bme280_cal_meas_delay(&measurement_duration_us, &state_->settings);
    if (result != BME280_OK) {
        setError(std::string("Failed to calculate BME280 measurement delay: ") + describeResult(result) + ".");
        initialized_ = false;
        return mapResultToEspErr(result);
    }
    boschDelayUs(measurement_duration_us + kMeasurementSlackUs);

    bme280_data data{};
    result = bme280_get_sensor_data(BME280_ALL, &data, &state_->device);
    if (result != BME280_OK) {
        setError(std::string("Failed to read BME280 measurement: ") + describeResult(result) + ".");
        initialized_ = false;
        return mapResultToEspErr(result);
    }

    measurement_.clear();
    measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
    measurement_.addValue(SensorValueKind::kTemperatureC, static_cast<float>(data.temperature));
    measurement_.addValue(SensorValueKind::kHumidityPercent, static_cast<float>(data.humidity));
    measurement_.addValue(SensorValueKind::kPressureHpa, static_cast<float>(data.pressure / 100.0));
    last_error_.clear();
    return ESP_OK;
}

SensorMeasurement Bme280Sensor::latestMeasurement() const {
    return measurement_;
}

std::string Bme280Sensor::lastError() const {
    return last_error_;
}

esp_err_t Bme280Sensor::configureSensor() {
    state_->settings.osr_h = kOversampling;
    state_->settings.osr_p = kOversampling;
    state_->settings.osr_t = kOversampling;
    state_->settings.filter = kFilter;
    state_->settings.standby_time = BME280_STANDBY_TIME_0_5_MS;

    int8_t result = bme280_set_sensor_settings(BME280_SEL_ALL_SETTINGS, &state_->settings, &state_->device);
    if (result != BME280_OK) {
        setError(std::string("Failed to configure BME280 settings: ") + describeResult(result) + ".");
        return mapResultToEspErr(result);
    }

    result = bme280_set_sensor_mode(BME280_POWERMODE_SLEEP, &state_->device);
    if (result != BME280_OK) {
        setError(std::string("Failed to put BME280 into sleep mode: ") + describeResult(result) + ".");
        return mapResultToEspErr(result);
    }

    return ESP_OK;
}

void Bme280Sensor::setError(const std::string& message) {
    last_error_ = message;
}

std::unique_ptr<SensorDriver> createBme280Sensor() {
    return std::make_unique<Bme280Sensor>();
}

}  // namespace air360
