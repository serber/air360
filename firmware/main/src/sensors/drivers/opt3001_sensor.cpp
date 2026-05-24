#include "air360/sensors/drivers/opt3001_sensor.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

#include "air360/sensors/transport_binding.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.sensor.opt3001";
constexpr std::uint32_t kOpt3001I2cSpeedHz = 100000U;
constexpr std::uint8_t kRegResult = 0x00U;
constexpr std::uint8_t kRegConfig = 0x01U;
constexpr std::uint8_t kRegManufacturerId = 0x7EU;
constexpr std::uint8_t kRegDeviceId = 0x7FU;
constexpr std::uint16_t kManufacturerIdTi = 0x5449U;
constexpr std::uint16_t kDeviceIdOpt3001 = 0x3001U;
constexpr std::uint16_t kConfigRangeAuto = 0xC000U;
constexpr std::uint16_t kConfigConversionTime800Ms = 0x0800U;
constexpr std::uint16_t kConfigModeSingleShot = 0x0200U;
constexpr std::uint16_t kConfigModeMask = 0x0600U;
constexpr std::uint16_t kConfigConversionReady = 0x0080U;
constexpr TickType_t kConversionDelayTicks = pdMS_TO_TICKS(825U);

std::uint16_t decodeBigEndian(const std::array<std::uint8_t, 2U>& bytes) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[0]) << 8U) | bytes[1]);
}

std::array<std::uint8_t, 2U> encodeBigEndian(std::uint16_t value) {
    return {
        static_cast<std::uint8_t>((value >> 8U) & 0xFFU),
        static_cast<std::uint8_t>(value & 0xFFU),
    };
}

float decodeLux(std::uint16_t raw_result) {
    const std::uint8_t exponent = static_cast<std::uint8_t>((raw_result >> 12U) & 0x0FU);
    const std::uint16_t mantissa = raw_result & 0x0FFFU;
    return static_cast<float>(0.01 * std::ldexp(static_cast<double>(mantissa), exponent));
}

}  // namespace

Opt3001Sensor::~Opt3001Sensor() {
    teardown();
}

SensorType Opt3001Sensor::type() const {
    return SensorType::kOpt3001;
}

esp_err_t Opt3001Sensor::init(
    const SensorRecord& record,
    const SensorDriverContext& context) {
    teardown();
    measurement_.clear();
    clearError();
    soft_fail_policy_.onPollOk();

    esp_err_t err = context.i2c_bus_manager->setupDevice(record, kOpt3001I2cSpeedHz, dev_);
    if (err != ESP_OK) {
        setError(std::string("Failed to set up I2C device for OPT3001: ") + esp_err_to_name(err));
        teardown();
        return err;
    }
    dev_initialized_ = true;

    err = i2c_dev_check_present(&dev_);
    if (err != ESP_OK) {
        setError("OPT3001 did not respond on the selected I2C bus and address.");
        teardown();
        return ESP_ERR_NOT_FOUND;
    }

    err = probe();
    if (err != ESP_OK) {
        teardown();
        return err;
    }

    initialized_ = true;
    return ESP_OK;
}

esp_err_t Opt3001Sensor::poll() {
    if (!initialized_ || !dev_initialized_) {
        setError("OPT3001 is not initialized.");
        return ESP_ERR_INVALID_STATE;
    }

    constexpr std::uint16_t start_config =
        kConfigRangeAuto | kConfigConversionTime800Ms | kConfigModeSingleShot;
    esp_err_t err = writeRegister(kRegConfig, start_config);
    if (err != ESP_OK) {
        return reportPollFailure(
            kTag,
            std::string("Failed to start OPT3001 conversion: ") + esp_err_to_name(err),
            err);
    }

    vTaskDelay(kConversionDelayTicks);

    std::uint16_t config = 0U;
    err = readRegister(kRegConfig, config);
    if (err != ESP_OK) {
        return reportPollFailure(
            kTag,
            std::string("Failed to read OPT3001 configuration: ") + esp_err_to_name(err),
            err);
    }

    if ((config & kConfigConversionReady) == 0U ||
        (config & kConfigModeMask) != 0U) {
        setError("OPT3001 conversion is not ready.");
        soft_fail_policy_.onPollOk();
        return ESP_OK;
    }

    std::uint16_t raw_result = 0U;
    err = readRegister(kRegResult, raw_result);
    if (err != ESP_OK) {
        return reportPollFailure(
            kTag,
            std::string("Failed to read OPT3001 illuminance: ") + esp_err_to_name(err),
            err);
    }

    measurement_.clear();
    measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
    measurement_.addValue(SensorValueKind::kIlluminanceLux, decodeLux(raw_result));
    notePollSuccess();
    return ESP_OK;
}

SensorMeasurement Opt3001Sensor::latestMeasurement() const {
    return measurement_;
}

esp_err_t Opt3001Sensor::readRegister(std::uint8_t reg, std::uint16_t& out_value) {
    std::array<std::uint8_t, 2U> data{};
    const esp_err_t err = i2c_dev_read_reg(&dev_, reg, data.data(), data.size());
    if (err != ESP_OK) {
        return err;
    }
    out_value = decodeBigEndian(data);
    return ESP_OK;
}

esp_err_t Opt3001Sensor::writeRegister(std::uint8_t reg, std::uint16_t value) {
    const std::array<std::uint8_t, 2U> data = encodeBigEndian(value);
    return i2c_dev_write_reg(&dev_, reg, data.data(), data.size());
}

esp_err_t Opt3001Sensor::probe() {
    std::uint16_t manufacturer_id = 0U;
    esp_err_t err = readRegister(kRegManufacturerId, manufacturer_id);
    if (err != ESP_OK) {
        setError(std::string("Failed to read OPT3001 manufacturer ID: ") + esp_err_to_name(err));
        return err;
    }

    std::uint16_t device_id = 0U;
    err = readRegister(kRegDeviceId, device_id);
    if (err != ESP_OK) {
        setError(std::string("Failed to read OPT3001 device ID: ") + esp_err_to_name(err));
        return err;
    }

    if (manufacturer_id != kManufacturerIdTi || device_id != kDeviceIdOpt3001) {
        setError("OPT3001 manufacturer or device ID did not match.");
        return ESP_ERR_NOT_FOUND;
    }

    return ESP_OK;
}

void Opt3001Sensor::teardown() {
    initialized_ = false;
    soft_fail_policy_.onPollOk();
    if (dev_initialized_) {
        i2c_dev_delete_mutex(&dev_);
    }
    std::memset(&dev_, 0, sizeof(dev_));
    dev_initialized_ = false;
}

std::unique_ptr<SensorDriver> createOpt3001Sensor() {
    return std::make_unique<Opt3001Sensor>();
}

}  // namespace air360
