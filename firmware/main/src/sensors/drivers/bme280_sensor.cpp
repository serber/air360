#include "air360/sensors/drivers/bme280_sensor.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

#include "air360/sensors/transport_binding.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

namespace air360 {

namespace {

constexpr std::uint8_t kRegisterChipId = 0xD0U;
constexpr std::uint8_t kExpectedChipId = 0x60U;
constexpr std::uint8_t kRegisterCalibration0 = 0x88U;
constexpr std::uint8_t kRegisterHumidity1 = 0xA1U;
constexpr std::uint8_t kRegisterCalibration1 = 0xE1U;
constexpr std::uint8_t kRegisterReset = 0xE0U;
constexpr std::uint8_t kRegisterCtrlHum = 0xF2U;
constexpr std::uint8_t kRegisterStatus = 0xF3U;
constexpr std::uint8_t kRegisterCtrlMeas = 0xF4U;
constexpr std::uint8_t kRegisterConfig = 0xF5U;
constexpr std::uint8_t kRegisterMeasurementStart = 0xF7U;
constexpr std::uint8_t kResetCommand = 0xB6U;
constexpr std::uint8_t kStatusImUpdateMask = 0x01U;
constexpr std::uint8_t kStatusMeasuringMask = 0x08U;
constexpr std::uint8_t kConfigFilterOff = 0x00U;
constexpr std::uint8_t kOversampling1x = 0x01U;
constexpr std::uint8_t kPowerModeForced = 0x01U;
constexpr std::uint8_t kCtrlMeasForced1x =
    static_cast<std::uint8_t>((kOversampling1x << 5U) | (kOversampling1x << 2U) | kPowerModeForced);
constexpr std::uint32_t kMeasurementTimeUs =
    1250U + (2300U * kOversampling1x) + ((2300U * kOversampling1x) + 575U) +
    ((2300U * kOversampling1x) + 575U);
constexpr TickType_t kMeasurementWaitTicks =
    pdMS_TO_TICKS((kMeasurementTimeUs + 999U) / 1000U + 1U);
constexpr TickType_t kStatusPollTicks = pdMS_TO_TICKS(2U);
constexpr std::uint8_t kStatusPollAttempts = 5U;
constexpr std::uint8_t kChipIdReadAttempts = 5U;
constexpr TickType_t kChipIdRetryTicks = pdMS_TO_TICKS(2U);

std::uint16_t readU16Le(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(data[0] | (static_cast<std::uint16_t>(data[1]) << 8U));
}

std::int16_t readS16Le(const std::uint8_t* data) {
    return static_cast<std::int16_t>(readU16Le(data));
}

void delayAtLeastOneTick(TickType_t ticks) {
    vTaskDelay(ticks == 0 ? 1 : ticks);
}

}  // namespace

SensorType Bme280Sensor::type() const {
    return SensorType::kBme280;
}

esp_err_t Bme280Sensor::init(
    const SensorRecord& record,
    const SensorDriverContext& context) {
    record_ = record;
    i2c_bus_manager_ = context.i2c_bus_manager;
    measurement_.clear();
    t_fine_ = 0;
    initialized_ = false;
    last_error_.clear();

    if (i2c_bus_manager_ == nullptr) {
        setError("I2C bus manager is unavailable.");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = i2c_bus_manager_->probe(record_.i2c_bus_id, record_.i2c_address);
    if (err != ESP_OK) {
        setError("BME280 probe failed on the selected I2C bus and address.");
        return err;
    }

    std::uint8_t chip_id = 0U;
    err = readChipIdWithRetry(chip_id);
    if (err != ESP_OK) {
        setError("Failed to read BME280 chip id.");
        return err;
    }

    if (chip_id != kExpectedChipId) {
        char message[64];
        std::snprintf(
            message,
            sizeof(message),
            "Unexpected BME280 chip id 0x%02x.",
            static_cast<unsigned>(chip_id));
        setError(message);
        return ESP_ERR_NOT_FOUND;
    }

    err = resetSensor();
    if (err != ESP_OK) {
        setError("Failed to reset BME280.");
        return err;
    }

    err = readCalibration();
    if (err != ESP_OK) {
        return err;
    }

    err = configureSensor();
    if (err != ESP_OK) {
        return err;
    }

    initialized_ = true;
    last_error_.clear();
    return ESP_OK;
}

esp_err_t Bme280Sensor::poll() {
    if (!initialized_ || i2c_bus_manager_ == nullptr) {
        setError("BME280 is not initialized.");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = startForcedMeasurement();
    if (err != ESP_OK) {
        setError("Failed to start forced BME280 measurement.");
        initialized_ = false;
        return err;
    }

    err = waitForMeasurement();
    if (err != ESP_OK) {
        setError("Timed out waiting for BME280 measurement.");
        initialized_ = false;
        return err;
    }

    std::int32_t raw_temperature = 0;
    std::int32_t raw_pressure = 0;
    std::int32_t raw_humidity = 0;
    err = readRawValues(raw_temperature, raw_pressure, raw_humidity);
    if (err != ESP_OK) {
        setError("Failed to read raw BME280 measurement registers.");
        initialized_ = false;
        return err;
    }

    const double adc_t = static_cast<double>(raw_temperature);
    const double var1_t =
        ((adc_t / 16384.0) - (static_cast<double>(calibration_.dig_t1) / 1024.0)) *
        static_cast<double>(calibration_.dig_t2);
    const double var2_t =
        (((adc_t / 131072.0) - (static_cast<double>(calibration_.dig_t1) / 8192.0)) *
         ((adc_t / 131072.0) - (static_cast<double>(calibration_.dig_t1) / 8192.0))) *
        static_cast<double>(calibration_.dig_t3);
    t_fine_ = static_cast<std::int32_t>(var1_t + var2_t);
    const double temperature_c = (var1_t + var2_t) / 5120.0;

    double pressure_hpa = 0.0;
    {
        double var1 = (static_cast<double>(t_fine_) / 2.0) - 64000.0;
        double var2 = var1 * var1 * static_cast<double>(calibration_.dig_p6) / 32768.0;
        var2 = var2 + (var1 * static_cast<double>(calibration_.dig_p5) * 2.0);
        var2 = (var2 / 4.0) + (static_cast<double>(calibration_.dig_p4) * 65536.0);
        var1 =
            (static_cast<double>(calibration_.dig_p3) * var1 * var1 / 524288.0 +
             static_cast<double>(calibration_.dig_p2) * var1) /
            524288.0;
        var1 = (1.0 + var1 / 32768.0) * static_cast<double>(calibration_.dig_p1);

        if (std::abs(var1) < 0.000001) {
            setError("BME280 pressure compensation hit zero divisor.");
            initialized_ = false;
            return ESP_ERR_INVALID_STATE;
        }

        double pressure = 1048576.0 - static_cast<double>(raw_pressure);
        pressure = ((pressure - (var2 / 4096.0)) * 6250.0) / var1;
        var1 = static_cast<double>(calibration_.dig_p9) * pressure * pressure / 2147483648.0;
        var2 = pressure * static_cast<double>(calibration_.dig_p8) / 32768.0;
        pressure = pressure + (var1 + var2 + static_cast<double>(calibration_.dig_p7)) / 16.0;
        pressure_hpa = pressure / 100.0;
    }

    double humidity_percent = 0.0;
    {
        double humidity = static_cast<double>(t_fine_) - 76800.0;
        humidity =
            (static_cast<double>(raw_humidity) -
             (static_cast<double>(calibration_.dig_h4) * 64.0 +
              static_cast<double>(calibration_.dig_h5) / 16384.0 * humidity)) *
            (static_cast<double>(calibration_.dig_h2) / 65536.0 *
             (1.0 +
              static_cast<double>(calibration_.dig_h6) / 67108864.0 * humidity *
                  (1.0 + static_cast<double>(calibration_.dig_h3) / 67108864.0 * humidity)));
        humidity =
            humidity *
            (1.0 - static_cast<double>(calibration_.dig_h1) * humidity / 524288.0);
        humidity_percent = std::clamp(humidity, 0.0, 100.0);
    }

    measurement_.sample_time_ms =
        static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
    measurement_.value_count = 0U;
    measurement_.addValue(SensorValueKind::kTemperatureC, static_cast<float>(temperature_c));
    measurement_.addValue(SensorValueKind::kHumidityPercent, static_cast<float>(humidity_percent));
    measurement_.addValue(SensorValueKind::kPressureHpa, static_cast<float>(pressure_hpa));
    last_error_.clear();
    return ESP_OK;
}

SensorMeasurement Bme280Sensor::latestMeasurement() const {
    return measurement_;
}

std::string Bme280Sensor::lastError() const {
    return last_error_;
}

esp_err_t Bme280Sensor::resetSensor() {
    esp_err_t err = i2c_bus_manager_->writeRegister(
        record_.i2c_bus_id,
        record_.i2c_address,
        kRegisterReset,
        kResetCommand);
    if (err != ESP_OK) {
        return err;
    }

    delayAtLeastOneTick(pdMS_TO_TICKS(3U));

    std::uint8_t status = 0U;
    for (std::uint8_t attempt = 0; attempt < kStatusPollAttempts; ++attempt) {
        err = i2c_bus_manager_->readRegister(
            record_.i2c_bus_id,
            record_.i2c_address,
            kRegisterStatus,
            &status,
            1U);
        if (err != ESP_OK) {
            return err;
        }

        if ((status & kStatusImUpdateMask) == 0U) {
            return ESP_OK;
        }

        delayAtLeastOneTick(kStatusPollTicks);
    }

    return ESP_ERR_TIMEOUT;
}

esp_err_t Bme280Sensor::readChipId(std::uint8_t& chip_id) {
    return i2c_bus_manager_->readRegister(
        record_.i2c_bus_id,
        record_.i2c_address,
        kRegisterChipId,
        &chip_id,
        1U);
}

esp_err_t Bme280Sensor::readChipIdWithRetry(std::uint8_t& chip_id) {
    esp_err_t last_err = ESP_FAIL;
    chip_id = 0U;

    for (std::uint8_t attempt = 0; attempt < kChipIdReadAttempts; ++attempt) {
        last_err = readChipId(chip_id);
        if (last_err == ESP_OK && chip_id == kExpectedChipId) {
            return ESP_OK;
        }

        if (attempt + 1U < kChipIdReadAttempts) {
            delayAtLeastOneTick(kChipIdRetryTicks);
        }
    }

    return last_err;
}

esp_err_t Bme280Sensor::readCalibration() {
    std::uint8_t calib0[24]{};
    esp_err_t err = i2c_bus_manager_->readRegister(
        record_.i2c_bus_id,
        record_.i2c_address,
        kRegisterCalibration0,
        calib0,
        sizeof(calib0));
    if (err != ESP_OK) {
        setError("Failed to read BME280 temperature and pressure calibration.");
        return err;
    }

    std::uint8_t dig_h1 = 0U;
    err = i2c_bus_manager_->readRegister(
        record_.i2c_bus_id,
        record_.i2c_address,
        kRegisterHumidity1,
        &dig_h1,
        1U);
    if (err != ESP_OK) {
        setError("Failed to read BME280 humidity calibration register H1.");
        return err;
    }

    std::uint8_t calib1[7]{};
    err = i2c_bus_manager_->readRegister(
        record_.i2c_bus_id,
        record_.i2c_address,
        kRegisterCalibration1,
        calib1,
        sizeof(calib1));
    if (err != ESP_OK) {
        setError("Failed to read BME280 humidity calibration block.");
        return err;
    }

    calibration_.dig_t1 = readU16Le(&calib0[0]);
    calibration_.dig_t2 = readS16Le(&calib0[2]);
    calibration_.dig_t3 = readS16Le(&calib0[4]);
    calibration_.dig_p1 = readU16Le(&calib0[6]);
    calibration_.dig_p2 = readS16Le(&calib0[8]);
    calibration_.dig_p3 = readS16Le(&calib0[10]);
    calibration_.dig_p4 = readS16Le(&calib0[12]);
    calibration_.dig_p5 = readS16Le(&calib0[14]);
    calibration_.dig_p6 = readS16Le(&calib0[16]);
    calibration_.dig_p7 = readS16Le(&calib0[18]);
    calibration_.dig_p8 = readS16Le(&calib0[20]);
    calibration_.dig_p9 = readS16Le(&calib0[22]);
    calibration_.dig_h1 = dig_h1;
    calibration_.dig_h2 = readS16Le(&calib1[0]);
    calibration_.dig_h3 = calib1[2];
    calibration_.dig_h4 =
        static_cast<std::int16_t>((static_cast<std::int16_t>(static_cast<std::int8_t>(calib1[3])) << 4) |
                                  (calib1[4] & 0x0F));
    calibration_.dig_h5 =
        static_cast<std::int16_t>((static_cast<std::int16_t>(static_cast<std::int8_t>(calib1[5])) << 4) |
                                  (calib1[4] >> 4));
    calibration_.dig_h6 = static_cast<std::int8_t>(calib1[6]);
    return ESP_OK;
}

esp_err_t Bme280Sensor::configureSensor() {
    esp_err_t err = i2c_bus_manager_->writeRegister(
        record_.i2c_bus_id,
        record_.i2c_address,
        kRegisterCtrlHum,
        kOversampling1x);
    if (err != ESP_OK) {
        setError("Failed to configure BME280 humidity oversampling.");
        return err;
    }

    err = i2c_bus_manager_->writeRegister(
        record_.i2c_bus_id,
        record_.i2c_address,
        kRegisterConfig,
        kConfigFilterOff);
    if (err != ESP_OK) {
        setError("Failed to configure BME280 sensor config register.");
        return err;
    }

    std::uint8_t status = 0U;
    err = i2c_bus_manager_->readRegister(
        record_.i2c_bus_id,
        record_.i2c_address,
        kRegisterStatus,
        &status,
        1U);
    if (err != ESP_OK) {
        setError("Failed to read BME280 status after configuration.");
        return err;
    }

    return ESP_OK;
}

esp_err_t Bme280Sensor::startForcedMeasurement() {
    esp_err_t err = i2c_bus_manager_->writeRegister(
        record_.i2c_bus_id,
        record_.i2c_address,
        kRegisterCtrlHum,
        kOversampling1x);
    if (err != ESP_OK) {
        return err;
    }

    return i2c_bus_manager_->writeRegister(
        record_.i2c_bus_id,
        record_.i2c_address,
        kRegisterCtrlMeas,
        kCtrlMeasForced1x);
}

esp_err_t Bme280Sensor::waitForMeasurement() {
    delayAtLeastOneTick(kMeasurementWaitTicks);

    std::uint8_t status = 0U;
    for (std::uint8_t attempt = 0; attempt < kStatusPollAttempts; ++attempt) {
        esp_err_t err = i2c_bus_manager_->readRegister(
            record_.i2c_bus_id,
            record_.i2c_address,
            kRegisterStatus,
            &status,
            1U);
        if (err != ESP_OK) {
            return err;
        }

        if ((status & kStatusMeasuringMask) == 0U) {
            return ESP_OK;
        }

        delayAtLeastOneTick(kStatusPollTicks);
    }

    return ESP_ERR_TIMEOUT;
}

esp_err_t Bme280Sensor::readRawValues(
    std::int32_t& raw_temperature,
    std::int32_t& raw_pressure,
    std::int32_t& raw_humidity) {
    std::uint8_t raw_data[8]{};
    esp_err_t err = i2c_bus_manager_->readRegister(
        record_.i2c_bus_id,
        record_.i2c_address,
        kRegisterMeasurementStart,
        raw_data,
        sizeof(raw_data));
    if (err != ESP_OK) {
        return err;
    }

    raw_pressure =
        (static_cast<std::int32_t>(raw_data[0]) << 12) |
        (static_cast<std::int32_t>(raw_data[1]) << 4) |
        (static_cast<std::int32_t>(raw_data[2]) >> 4);
    raw_temperature =
        (static_cast<std::int32_t>(raw_data[3]) << 12) |
        (static_cast<std::int32_t>(raw_data[4]) << 4) |
        (static_cast<std::int32_t>(raw_data[5]) >> 4);
    raw_humidity =
        (static_cast<std::int32_t>(raw_data[6]) << 8) |
        static_cast<std::int32_t>(raw_data[7]);
    return ESP_OK;
}

void Bme280Sensor::setError(const std::string& message) {
    last_error_ = message;
}

std::unique_ptr<SensorDriver> createBme280Sensor() {
    return std::make_unique<Bme280Sensor>();
}

}  // namespace air360
