#include "air360/sensors/drivers/ens160_sensor.hpp"

#include <cstdio>
#include <cstdint>
#include <memory>
#include <string>

#include "air360/sensors/transport_binding.hpp"
#include "esp_err.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"

namespace air360 {

namespace {

constexpr std::uint8_t kEns160PrimaryAddress = 0x53U;
constexpr std::uint8_t kEns160SecondaryAddress = 0x52U;
constexpr std::uint8_t kRegisterPartId = 0x00U;
constexpr std::uint8_t kRegisterOpMode = 0x10U;
constexpr std::uint8_t kRegisterDataStatus = 0x20U;
constexpr std::uint8_t kRegisterDataAqi = 0x21U;
constexpr std::uint8_t kRegisterDataTvoc = 0x22U;
constexpr std::uint8_t kRegisterDataEco2 = 0x24U;
constexpr std::uint16_t kExpectedPartId = 0x0160U;
constexpr std::uint8_t kOpModeReset = 0xF0U;
constexpr std::uint8_t kOpModeStandard = 0x02U;
constexpr std::uint8_t kStatusNewDataMask = 0x02U;
constexpr std::uint8_t kStatusValidityMask = 0x0CU;
constexpr std::uint8_t kStatusValidityShift = 2U;
constexpr std::uint8_t kStatusValidityNormal = 0U;

bool isZeroMeasurement(std::uint8_t aqi, std::uint16_t tvoc, std::uint16_t eco2) {
    return aqi == 0U && tvoc == 0U && eco2 == 0U;
}

const char* validityText(std::uint8_t validity) {
    switch (validity) {
        case 0U:
            return "normal";
        case 1U:
            return "warm-up";
        case 2U:
            return "initial start-up";
        case 3U:
            return "invalid output";
        default:
            return "unknown";
    }
}

std::uint16_t readLe16(const std::uint8_t* bytes) {
    return static_cast<std::uint16_t>(bytes[0]) |
           (static_cast<std::uint16_t>(bytes[1]) << 8U);
}

}  // namespace

SensorType Ens160Sensor::type() const {
    return SensorType::kEns160;
}

esp_err_t Ens160Sensor::init(
    const SensorRecord& record,
    const SensorDriverContext& context) {
    record_ = record;
    i2c_bus_manager_ = context.i2c_bus_manager;
    measurement_.clear();
    last_error_.clear();
    active_address_ = 0U;
    initialized_ = false;

    if (i2c_bus_manager_ == nullptr) {
        setError("I2C bus manager is unavailable.");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = probeAndBindAddress();
    if (err != ESP_OK) {
        return err;
    }

    std::uint16_t part_id = 0U;
    err = readPartId(part_id);
    if (err != ESP_OK) {
        setError(std::string("Failed to read ENS160 part id: ") + esp_err_to_name(err) + ".");
        return err;
    }

    if (part_id != kExpectedPartId) {
        char message[96];
        std::snprintf(
            message,
            sizeof(message),
            "Unexpected ENS160 part id 0x%04X (expected 0x%04X).",
            static_cast<unsigned>(part_id),
            static_cast<unsigned>(kExpectedPartId));
        setError(message);
        return ESP_ERR_INVALID_RESPONSE;
    }

    err = setOperatingMode(kOpModeReset);
    if (err != ESP_OK) {
        setError(std::string("Failed to reset ENS160: ") + esp_err_to_name(err) + ".");
        return err;
    }
    esp_rom_delay_us(2000U);

    err = setOperatingMode(kOpModeStandard);
    if (err != ESP_OK) {
        setError(std::string("Failed to enter ENS160 standard mode: ") + esp_err_to_name(err) + ".");
        return err;
    }

    initialized_ = true;
    return ESP_OK;
}

esp_err_t Ens160Sensor::poll() {
    if (!initialized_ || i2c_bus_manager_ == nullptr || active_address_ == 0U) {
        setError("ENS160 is not initialized.");
        return ESP_ERR_INVALID_STATE;
    }

    std::uint8_t status = 0U;
    esp_err_t err = readStatus(status);
    if (err != ESP_OK) {
        setError(std::string("Failed to read ENS160 status: ") + esp_err_to_name(err) + ".");
        initialized_ = false;
        return err;
    }

    const std::uint8_t validity = (status & kStatusValidityMask) >> kStatusValidityShift;
    std::uint8_t aqi = 0U;
    std::uint16_t tvoc = 0U;
    std::uint16_t eco2 = 0U;
    err = readMetrics(aqi, tvoc, eco2);
    if (err != ESP_OK) {
        setError(std::string("Failed to read ENS160 metrics: ") + esp_err_to_name(err) + ".");
        initialized_ = false;
        return err;
    }

    const bool has_fresh_sample =
        (status & kStatusNewDataMask) != 0U || validity == kStatusValidityNormal;
    const bool zero_sample = isZeroMeasurement(aqi, tvoc, eco2);
    if (has_fresh_sample && !zero_sample) {
        measurement_.clear();
        measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
        measurement_.addValue(SensorValueKind::kAqi, static_cast<float>(aqi));
        measurement_.addValue(SensorValueKind::kTvocPpb, static_cast<float>(tvoc));
        measurement_.addValue(SensorValueKind::kEco2Ppm, static_cast<float>(eco2));
    }

    last_error_.clear();
    if (validity != kStatusValidityNormal) {
        setError(std::string("ENS160 data validity state: ") + validityText(validity) + ".");
    } else if (zero_sample) {
        setError("ENS160 returned an all-zero sample; keeping previous reading.");
    }

    return ESP_OK;
}

SensorMeasurement Ens160Sensor::latestMeasurement() const {
    return measurement_;
}

std::string Ens160Sensor::lastError() const {
    return last_error_;
}

esp_err_t Ens160Sensor::setOperatingMode(std::uint8_t mode) {
    return i2c_bus_manager_->writeRegister(record_.i2c_bus_id, active_address_, kRegisterOpMode, mode);
}

esp_err_t Ens160Sensor::readPartId(std::uint16_t& out_part_id) {
    std::uint8_t bytes[2]{};
    const esp_err_t err = i2c_bus_manager_->readRegister(
        record_.i2c_bus_id,
        active_address_,
        kRegisterPartId,
        bytes,
        sizeof(bytes));
    if (err != ESP_OK) {
        return err;
    }

    out_part_id = readLe16(bytes);
    return ESP_OK;
}

esp_err_t Ens160Sensor::readStatus(std::uint8_t& out_status) {
    return i2c_bus_manager_->readRegister(
        record_.i2c_bus_id,
        active_address_,
        kRegisterDataStatus,
        &out_status,
        1U);
}

esp_err_t Ens160Sensor::readMetrics(
    std::uint8_t& out_aqi,
    std::uint16_t& out_tvoc,
    std::uint16_t& out_eco2) {
    std::uint8_t aqi_byte = 0U;
    std::uint8_t tvoc_bytes[2]{};
    std::uint8_t eco2_bytes[2]{};

    esp_err_t err = i2c_bus_manager_->readRegister(
        record_.i2c_bus_id,
        active_address_,
        kRegisterDataAqi,
        &aqi_byte,
        1U);
    if (err != ESP_OK) {
        return err;
    }

    err = i2c_bus_manager_->readRegister(
        record_.i2c_bus_id,
        active_address_,
        kRegisterDataTvoc,
        tvoc_bytes,
        sizeof(tvoc_bytes));
    if (err != ESP_OK) {
        return err;
    }

    err = i2c_bus_manager_->readRegister(
        record_.i2c_bus_id,
        active_address_,
        kRegisterDataEco2,
        eco2_bytes,
        sizeof(eco2_bytes));
    if (err != ESP_OK) {
        return err;
    }

    out_aqi = aqi_byte;
    out_tvoc = readLe16(tvoc_bytes);
    out_eco2 = readLe16(eco2_bytes);
    return ESP_OK;
}

esp_err_t Ens160Sensor::probeAndBindAddress() {
    const std::uint8_t configured_address = record_.i2c_address;
    const std::uint8_t alternate_address =
        configured_address == kEns160PrimaryAddress ? kEns160SecondaryAddress : kEns160PrimaryAddress;

    const std::uint8_t candidates[] = {configured_address, alternate_address};
    esp_err_t last_err = ESP_ERR_NOT_FOUND;

    for (const std::uint8_t candidate : candidates) {
        if (candidate != kEns160PrimaryAddress && candidate != kEns160SecondaryAddress) {
            continue;
        }

        active_address_ = candidate;
        std::uint16_t part_id = 0U;
        const esp_err_t read_err = readPartId(part_id);
        if (read_err == ESP_OK && part_id == kExpectedPartId) {
            active_address_ = candidate;
            return ESP_OK;
        }
        last_err = read_err == ESP_OK ? ESP_ERR_INVALID_RESPONSE : read_err;
    }

    active_address_ = 0U;

    char message[160];
    std::snprintf(
        message,
        sizeof(message),
        "ENS160 identification failed on I2C bus %u at addresses 0x%02X and 0x%02X: %s.",
        static_cast<unsigned>(record_.i2c_bus_id),
        static_cast<unsigned>(configured_address),
        static_cast<unsigned>(alternate_address),
        esp_err_to_name(last_err));
    setError(message);
    return last_err;
}

void Ens160Sensor::setError(const std::string& message) {
    last_error_ = message;
}

std::unique_ptr<SensorDriver> createEns160Sensor() {
    return std::make_unique<Ens160Sensor>();
}

}  // namespace air360
