#include "air360/sensors/drivers/sps30_sensor.hpp"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

#include "sensirion_common.h"
#include "sensirion_i2c.h"
#include "sensirion_i2c_hal.h"
#include "sps30_i2c.h"

#include "air360/sensors/drivers/sps30_i2c_support.hpp"
#include "air360/sensors/transport_binding.hpp"
#include "esp_log.h"
#include "esp_timer.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.sensor.sps30";
// SPS30 runs happily on standard-mode I2C; 100 kHz matches the vendor examples
// and keeps the shared bus conservative for mixed-sensor deployments.
constexpr std::uint32_t kSps30I2cSpeedHz = 100000U;
// The fan-cleaning blow-out runs autonomously for ~10 s; we wait a little longer
// before declaring the one-shot maintenance action complete so the fan has spun
// back down before normal measurements are trusted again.
constexpr std::uint64_t kFanCleanDurationMs = 12'000U;

std::uint64_t nowMilliseconds() {
    return static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
}

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

}  // namespace

Sps30Sensor::~Sps30Sensor() {
    teardown();
}

void Sps30Sensor::teardown() {
    if (device_initialized_) {
        i2c_dev_delete_mutex(&device_);
        device_initialized_ = false;
    }
    initialized_ = false;
    soft_fail_policy_.onPollOk();
    fan_clean_state_ = MaintenanceActionState::kIdle;
    fan_clean_issued_ = false;
}

SensorType Sps30Sensor::type() const {
    return SensorType::kSps30;
}

esp_err_t Sps30Sensor::init(
    const SensorRecord& record,
    const SensorDriverContext& context) {
    teardown();
    record_ = record;
    measurement_.clear();
    clearError();
    soft_fail_policy_.onPollOk();

    std::memset(&device_, 0, sizeof(device_));
    esp_err_t err = context.i2c_bus_manager->setupDevice(record, kSps30I2cSpeedHz, device_);
    if (err != ESP_OK) {
        setError(std::string("Failed to set up I2C device for SPS30: ") + esp_err_to_name(err));
        return err;
    }
    device_initialized_ = true;

    err = i2c_dev_check_present(&device_);
    if (err != ESP_OK) {
        setError("SPS30 not found on the selected I2C bus and address.");
        return ESP_ERR_NOT_FOUND;
    }

    {
        SensirionI2cContextGuard hal_guard(&device_);
        sensirion_i2c_hal_init();
        sps30_init(record.i2c_address);
    }

    esp_err_t start_err = startMeasurement();
    if (start_err != ESP_OK) {
        const std::int16_t wake_err = [&]() {
            SensirionI2cContextGuard hal_guard(&device_);
            return sps30_wake_up_sequence();
        }();
        if (wake_err != NO_ERROR) {
            setError(std::string("Failed to wake SPS30: ") + describeResult(wake_err) + ".");
            return mapResultToEspErr(wake_err);
        }

        start_err = startMeasurement();
        if (start_err != ESP_OK) {
            return start_err;
        }
    }

    // Arm (or disarm) the fan-cleaning one-shot maintenance action. Re-arming on
    // every init keeps run-once at-least-once semantics across a mid-action
    // reboot until the manager clears the pending action.
    const bool fan_clean_armed =
        record_.pending_maintenance_action ==
        static_cast<std::uint8_t>(MaintenanceActionKind::kFanClean);
    if (fan_clean_armed) {
        fan_clean_state_ = MaintenanceActionState::kRunning;
        fan_clean_issued_ = false;
        fan_clean_started_ms_ = 0U;
        fan_clean_status_ = "Fan cleaning: queued";
        ESP_LOGI(kTag, "SPS30 fan cleaning armed; will run once after first poll");
    } else {
        fan_clean_state_ = MaintenanceActionState::kIdle;
        fan_clean_status_.clear();
    }

    initialized_ = true;
    return ESP_OK;
}

esp_err_t Sps30Sensor::poll() {
    if (!initialized_) {
        setError("SPS30 is not initialized.");
        return ESP_ERR_INVALID_STATE;
    }

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

    const std::int16_t result = [&]() {
        SensirionI2cContextGuard hal_guard(&device_);
        return sps30_read_measurement_values_float(
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
    }();
    if (result != NO_ERROR) {
        return reportPollFailure(
            kTag,
            std::string("Failed to read SPS30 measurement: ") + describeResult(result) + ".",
            mapResultToEspErr(result));
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
    notePollSuccess();
    stepFanCleaning();
    return ESP_OK;
}

void Sps30Sensor::stepFanCleaning() {
    if (fan_clean_state_ != MaintenanceActionState::kRunning) {
        return;
    }

    if (!fan_clean_issued_) {
        const std::int16_t result = [&]() {
            SensirionI2cContextGuard hal_guard(&device_);
            return sps30_start_fan_cleaning();
        }();
        if (result != NO_ERROR) {
            fan_clean_state_ = MaintenanceActionState::kFailed;
            fan_clean_status_ =
                std::string("Fan cleaning failed: ") + describeResult(result);
            ESP_LOGW(kTag, "SPS30 fan cleaning command failed: %s", describeResult(result).c_str());
            return;
        }
        fan_clean_issued_ = true;
        fan_clean_started_ms_ = nowMilliseconds();
        fan_clean_status_ = "Fan cleaning: running";
        ESP_LOGI(kTag, "SPS30 fan cleaning started");
        return;
    }

    if (nowMilliseconds() - fan_clean_started_ms_ >= kFanCleanDurationMs) {
        fan_clean_state_ = MaintenanceActionState::kCompleted;
        fan_clean_status_ = "Fan cleaning complete";
        ESP_LOGI(kTag, "SPS30 fan cleaning complete");
    }
}

SensorMeasurement Sps30Sensor::latestMeasurement() const {
    return measurement_;
}

MaintenanceActionState Sps30Sensor::maintenanceActionState() const {
    return fan_clean_state_;
}

std::string Sps30Sensor::maintenanceStatus() const {
    return fan_clean_status_;
}

void Sps30Sensor::acknowledgeMaintenanceAction() {
    // Manager has recorded the result and cleared the pending action from NVS;
    // return to idle so it is not re-reported. Keep the status string for the UI.
    fan_clean_state_ = MaintenanceActionState::kIdle;
}

esp_err_t Sps30Sensor::startMeasurement() {
    const std::int16_t result = [&]() {
        SensirionI2cContextGuard hal_guard(&device_);
        return sps30_start_measurement(SPS30_OUTPUT_FORMAT_OUTPUT_FORMAT_FLOAT);
    }();
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
