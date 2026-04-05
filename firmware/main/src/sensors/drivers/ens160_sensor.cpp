#include "air360/sensors/drivers/ens160_sensor.hpp"

#include <cstdio>
#include <cstdint>
#include <memory>
#include <string>

#include "air360/sensors/transport_binding.hpp"
#include "ScioSense_ENS160.h"
#include "Wire.h"
#include "esp_err.h"
#include "esp_timer.h"

namespace air360 {

namespace {

constexpr std::uint8_t kEns160PrimaryAddress = 0x53U;
constexpr std::uint8_t kEns160SecondaryAddress = 0x52U;
constexpr std::uint8_t kOpModeStandard = ENS160_OPMODE_STD;

bool isZeroMeasurement(std::uint8_t aqi, std::uint16_t tvoc, std::uint16_t eco2) {
    return aqi == 0U && tvoc == 0U && eco2 == 0U;
}

}  // namespace

Ens160Sensor::~Ens160Sensor() = default;

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
    sensor_.reset();
    wire_.reset();
    active_address_ = 0U;
    initialized_ = false;

    if (i2c_bus_manager_ == nullptr) {
        setError("I2C bus manager is unavailable.");
        return ESP_ERR_INVALID_STATE;
    }

    wire_ = std::make_unique<::TwoWire>();
    wire_->attach(i2c_bus_manager_, record_.i2c_bus_id);

    const esp_err_t err = bindConfiguredAddress();
    if (err != ESP_OK) {
        return err;
    }

    if (!sensor_->setMode(kOpModeStandard)) {
        setError("Failed to enter ENS160 standard mode.");
        sensor_.reset();
        active_address_ = 0U;
        return ESP_ERR_INVALID_RESPONSE;
    }

    initialized_ = true;
    return ESP_OK;
}

esp_err_t Ens160Sensor::poll() {
    if (!initialized_ || sensor_ == nullptr || active_address_ == 0U) {
        setError("ENS160 is not initialized.");
        return ESP_ERR_INVALID_STATE;
    }

    if (!sensor_->measure(false)) {
        setError("ENS160 has no fresh sample yet.");
        return ESP_OK;
    }

    const std::uint8_t aqi = sensor_->getAQI();
    const std::uint16_t tvoc = sensor_->getTVOC();
    const std::uint16_t eco2 = sensor_->geteCO2();
    const bool zero_sample = isZeroMeasurement(aqi, tvoc, eco2);
    if (!zero_sample) {
        measurement_.clear();
        measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
        measurement_.addValue(SensorValueKind::kAqi, static_cast<float>(aqi));
        measurement_.addValue(SensorValueKind::kTvocPpb, static_cast<float>(tvoc));
        measurement_.addValue(SensorValueKind::kEco2Ppm, static_cast<float>(eco2));
    }

    if (zero_sample) {
        setError("ENS160 returned an all-zero sample; keeping previous reading.");
    } else {
        last_error_.clear();
    }

    return ESP_OK;
}

SensorMeasurement Ens160Sensor::latestMeasurement() const {
    return measurement_;
}

std::string Ens160Sensor::lastError() const {
    return last_error_;
}

esp_err_t Ens160Sensor::bindSensorAtAddress(std::uint8_t address) {
    auto candidate = std::make_unique<::ScioSense_ENS160>(wire_.get(), address);
    if (!candidate->begin(false)) {
        return ESP_ERR_NOT_FOUND;
    }

    sensor_ = std::move(candidate);
    active_address_ = address;
    return ESP_OK;
}

esp_err_t Ens160Sensor::bindConfiguredAddress() {
    const std::uint8_t configured_address = record_.i2c_address;
    const std::uint8_t alternate_address =
        configured_address == kEns160PrimaryAddress ? kEns160SecondaryAddress : kEns160PrimaryAddress;

    const std::uint8_t candidates[] = {configured_address, alternate_address};
    esp_err_t last_err = ESP_ERR_NOT_FOUND;

    for (const std::uint8_t candidate : candidates) {
        if (candidate != kEns160PrimaryAddress && candidate != kEns160SecondaryAddress) {
            continue;
        }

        const esp_err_t bind_err = bindSensorAtAddress(candidate);
        if (bind_err == ESP_OK) {
            return ESP_OK;
        }
        last_err = bind_err;
    }

    active_address_ = 0U;
    sensor_.reset();

    char message[160];
    std::snprintf(
        message,
        sizeof(message),
        "ENS160 initialization failed on I2C bus %u at addresses 0x%02X and 0x%02X: %s.",
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
