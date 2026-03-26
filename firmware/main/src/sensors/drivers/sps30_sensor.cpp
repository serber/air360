#include "air360/sensors/drivers/sps30_sensor.hpp"

#include <cstdint>
#include <memory>
#include <string>

extern "C" {
#include "sensirion_common.h"
#include "sensirion_i2c.h"
#include "sensirion_i2c_hal.h"
#include "sps30_i2c.h"
}

#include "air360/sensors/drivers/sps30_i2c_support.hpp"
#include "air360/sensors/transport_binding.hpp"
#include "esp_timer.h"

namespace air360 {

namespace {

esp_err_t mapResultToEspErr(std::int16_t result) {
    switch (result) {
        case NO_ERROR:
            return ESP_OK;
        case CRC_ERROR:
            return ESP_ERR_INVALID_RESPONSE;
        case BYTE_NUM_ERROR:
            return ESP_ERR_INVALID_ARG;
        case I2C_BUS_ERROR:
        case I2C_NACK_ERROR:
        default:
            return ESP_FAIL;
    }
}

std::string describeResult(std::int16_t result) {
    switch (result) {
        case NO_ERROR:
            return "ok";
        case CRC_ERROR:
            return "crc error";
        case I2C_BUS_ERROR:
            return "i2c bus error";
        case I2C_NACK_ERROR:
            return "i2c nack";
        case BYTE_NUM_ERROR:
            return "invalid byte count";
        default:
            return "unknown sps30 error";
    }
}

void prepareContext(I2cBusManager* bus_manager, const SensorRecord& record) {
    sps30HalSetContext(bus_manager, record.i2c_bus_id);
    sensirion_i2c_hal_select_bus(record.i2c_bus_id);
    sps30_init(record.i2c_address);
}

}  // namespace

SensorType Sps30Sensor::type() const {
    return SensorType::kSps30;
}

esp_err_t Sps30Sensor::init(
    const SensorRecord& record,
    const SensorDriverContext& context) {
    record_ = record;
    i2c_bus_manager_ = context.i2c_bus_manager;
    measurement_.clear();
    last_error_.clear();
    initialized_ = false;

    if (i2c_bus_manager_ == nullptr) {
        setError("I2C bus manager is unavailable.");
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t probe_err = i2c_bus_manager_->probe(record.i2c_bus_id, record.i2c_address);
    if (probe_err != ESP_OK) {
        setError("SPS30 probe failed on the selected I2C bus and address.");
        return probe_err;
    }

    prepareContext(i2c_bus_manager_, record_);
    sensirion_i2c_hal_init();

    esp_err_t start_err = startMeasurement();
    if (start_err != ESP_OK) {
        prepareContext(i2c_bus_manager_, record_);
        const std::int16_t wake_err = sps30_wake_up_sequence();
        if (wake_err != NO_ERROR) {
            setError(std::string("Failed to wake SPS30: ") + describeResult(wake_err) + ".");
            return mapResultToEspErr(wake_err);
        }

        start_err = startMeasurement();
        if (start_err != ESP_OK) {
            return start_err;
        }
    }

    initialized_ = true;
    return ESP_OK;
}

esp_err_t Sps30Sensor::poll() {
    if (!initialized_ || i2c_bus_manager_ == nullptr) {
        setError("SPS30 is not initialized.");
        return ESP_ERR_INVALID_STATE;
    }

    prepareContext(i2c_bus_manager_, record_);

    float mc_1p0 = 0.0F;
    float mc_2p5 = 0.0F;
    float mc_4p0 = 0.0F;
    float mc_10p0 = 0.0F;
    float nc_0p5 = 0.0F;
    float nc_1p0 = 0.0F;
    float nc_2p5 = 0.0F;
    float nc_4p0 = 0.0F;
    float nc_10p0 = 0.0F;
    float typical_particle_size = 0.0F;

    const std::int16_t result = sps30_read_measurement_values_float(
        &mc_1p0,
        &mc_2p5,
        &mc_4p0,
        &mc_10p0,
        &nc_0p5,
        &nc_1p0,
        &nc_2p5,
        &nc_4p0,
        &nc_10p0,
        &typical_particle_size);
    if (result != NO_ERROR) {
        setError(std::string("Failed to read SPS30 measurement: ") + describeResult(result) + ".");
        initialized_ = false;
        return mapResultToEspErr(result);
    }

    measurement_.clear();
    measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
    measurement_.addValue(SensorValueKind::kPm1_0UgM3, mc_1p0);
    measurement_.addValue(SensorValueKind::kPm2_5UgM3, mc_2p5);
    measurement_.addValue(SensorValueKind::kPm4_0UgM3, mc_4p0);
    measurement_.addValue(SensorValueKind::kPm10_0UgM3, mc_10p0);
    measurement_.addValue(SensorValueKind::kNc0_5PerCm3, nc_0p5);
    measurement_.addValue(SensorValueKind::kNc1_0PerCm3, nc_1p0);
    measurement_.addValue(SensorValueKind::kNc2_5PerCm3, nc_2p5);
    measurement_.addValue(SensorValueKind::kNc4_0PerCm3, nc_4p0);
    measurement_.addValue(SensorValueKind::kNc10_0PerCm3, nc_10p0);
    measurement_.addValue(SensorValueKind::kTypicalParticleSizeUm, typical_particle_size);
    last_error_.clear();
    return ESP_OK;
}

SensorMeasurement Sps30Sensor::latestMeasurement() const {
    return measurement_;
}

std::string Sps30Sensor::lastError() const {
    return last_error_;
}

void Sps30Sensor::setError(const std::string& message) {
    last_error_ = message;
}

esp_err_t Sps30Sensor::startMeasurement() {
    prepareContext(i2c_bus_manager_, record_);
    const std::int16_t result =
        sps30_start_measurement(SPS30_OUTPUT_FORMAT_OUTPUT_FORMAT_FLOAT);
    if (result != NO_ERROR) {
        setError(std::string("Failed to start SPS30 measurement: ") + describeResult(result) + ".");
        return mapResultToEspErr(result);
    }

    return ESP_OK;
}

std::unique_ptr<SensorDriver> createSps30Sensor() {
    return std::make_unique<Sps30Sensor>();
}

}  // namespace air360
