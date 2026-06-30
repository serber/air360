#include "air360/sensors/drivers/scd30_sensor.hpp"

#include <cmath>
#include <cstring>
#include <memory>
#include <string>

#include "air360/sensors/transport_binding.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "scd30.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.sensor.scd30";

// Forced recalibration (FRC) tuning. The SCD30 datasheet asks for the sensor to
// run in a stable environment in continuous mode at a fast (≈2 s) rate for at
// least two minutes before the FRC command is issued, so we force the fastest
// rate the driver accepts while FRC is armed and gate the command on both
// elapsed time and a minimum number of fresh samples. The esp-idf-lib driver
// rejects intervals ≤ 2 s (CHECK_ARG: interval > 2), so 3 s is the floor.
// 400 ppm is the fresh outdoor-air reference matching the permanently-powered
// outdoor-unit use case.
constexpr std::uint16_t kFrcMeasurementIntervalSec = 3U;
constexpr std::uint64_t kFrcWarmupMs = 120'000U;
constexpr std::uint64_t kFrcTimeoutMs = 300'000U;
constexpr std::uint32_t kFrcMinGoodSamples = 20U;
constexpr std::uint16_t kFrcReferenceCo2Ppm = 400U;

std::uint64_t nowMilliseconds() {
    return static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
}

std::uint16_t measurementIntervalSeconds(std::uint32_t poll_interval_ms) {
    const std::uint32_t rounded_up = (poll_interval_ms + 999U) / 1000U;
    if (rounded_up <= 2U) {
        return 3U;
    }
    if (rounded_up >= 1800U) {
        return 1799U;
    }
    return static_cast<std::uint16_t>(rounded_up);
}

}  // namespace

Scd30Sensor::~Scd30Sensor() {
    teardown();
}

SensorType Scd30Sensor::type() const {
    return SensorType::kScd30;
}

esp_err_t Scd30Sensor::init(const SensorRecord& record, const SensorDriverContext& context) {
    teardown();
    record_ = record;
    measurement_.clear();
    clearError();
    soft_fail_policy_.onPollOk();

    i2c_port_t port = I2C_NUM_0;
    gpio_num_t sda = GPIO_NUM_NC;
    gpio_num_t scl = GPIO_NUM_NC;
    if (!context.i2c_bus_manager->resolvePins(record.i2c_bus_id, port, sda, scl)) {
        setError("Unknown I2C bus id for SCD30.");
        return ESP_ERR_NOT_SUPPORTED;
    }

    std::memset(&device_, 0, sizeof(device_));
    esp_err_t err = scd30_init_desc(&device_, port, sda, scl);
    if (err != ESP_OK) {
        setError("Failed to initialize SCD30 descriptor.");
        teardown();
        return err;
    }
    descriptor_initialized_ = true;

    err = i2c_dev_check_present(&device_);
    if (err != ESP_OK) {
        setError("SCD30 sensor was not detected on I2C bus 0 at address 0x61.");
        teardown();
        return err;
    }

    // A forced recalibration (FRC) action needs a fast 2 s sampling rate during
    // its warm-up; otherwise honor the configured poll interval.
    const bool frc_armed =
        record_.pending_maintenance_action ==
        static_cast<std::uint8_t>(MaintenanceActionKind::kForcedRecalibration);
    const std::uint16_t interval_sec =
        frc_armed ? kFrcMeasurementIntervalSec
                  : measurementIntervalSeconds(record_.poll_interval_ms);
    err = scd30_set_measurement_interval(&device_, interval_sec);
    if (err != ESP_OK) {
        setError("Failed to configure SCD30 measurement interval.");
        teardown();
        return err;
    }

    err = scd30_trigger_continuous_measurement(&device_, 0U);
    if (err != ESP_OK) {
        setError("Failed to start continuous SCD30 measurement.");
        teardown();
        return err;
    }

    // Apply the configured automatic self-calibration (ASC) state. ASC is the
    // SCD30's interpretation of SensorRecord::startup_calibration and is the
    // recommended mode for permanently powered outdoor units with regular fresh
    // air. The write is idempotent, so re-asserting it on every init keeps the
    // firmware config authoritative even if the sensor is swapped. A failure is
    // non-fatal: the sensor still measures, just without the requested ASC
    // state, so we log and continue instead of failing init.
    const bool asc_enabled = record_.startup_calibration != 0U;
    if (esp_err_t asc_err = scd30_set_automatic_self_calibration(&device_, asc_enabled);
        asc_err != ESP_OK) {
        ESP_LOGW(
            kTag,
            "Failed to set SCD30 ASC to %s: %s",
            asc_enabled ? "enabled" : "disabled",
            esp_err_to_name(asc_err));
    } else {
        ESP_LOGI(kTag, "SCD30 ASC %s", asc_enabled ? "enabled" : "disabled");
    }

    // Arm (or disarm) the FRC one-shot maintenance action. Re-arming on every
    // init keeps run-once at-least-once semantics: a reboot mid-FRC simply
    // restarts the warm-up until the manager clears the pending action.
    if (frc_armed) {
        frc_state_ = MaintenanceActionState::kRunning;
        frc_started_ms_ = nowMilliseconds();
        frc_good_samples_ = 0U;
        frc_status_ = "FRC: warming up";
        ESP_LOGI(
            kTag,
            "SCD30 FRC armed: warming up %llu s at %u s rate before recalibrating to %u ppm",
            static_cast<unsigned long long>(kFrcWarmupMs / 1000ULL),
            static_cast<unsigned>(kFrcMeasurementIntervalSec),
            static_cast<unsigned>(kFrcReferenceCo2Ppm));
    } else {
        frc_state_ = MaintenanceActionState::kIdle;
        frc_status_.clear();
    }

    measurement_running_ = true;
    initialized_ = true;
    setError("Waiting for first SCD30 sample.");
    return ESP_OK;
}

esp_err_t Scd30Sensor::poll() {
    if (!initialized_ || !descriptor_initialized_) {
        setError("SCD30 sensor is not initialized.");
        return ESP_ERR_INVALID_STATE;
    }

    bool data_ready = false;
    if (esp_err_t err = scd30_get_data_ready_status(&device_, &data_ready); err != ESP_OK) {
        return reportPollFailure(kTag, "Failed to query SCD30 data-ready status.", err);
    }

    if (!data_ready) {
        measurement_.clear();
        soft_fail_policy_.onPollOk();
        setError("Waiting for new SCD30 sample.");
        stepForcedRecalibration(false);
        return ESP_OK;
    }

    float co2_ppm = 0.0F;
    float temperature_c = 0.0F;
    float humidity_percent = 0.0F;
    if (esp_err_t err = scd30_read_measurement(&device_, &co2_ppm, &temperature_c, &humidity_percent);
        err != ESP_OK) {
        return reportPollFailure(kTag, "Failed to read SCD30 measurement.", err);
    }

    if (std::isnan(co2_ppm) || std::isnan(temperature_c) || std::isnan(humidity_percent)) {
        return reportPollFailure(
            kTag, "SCD30 driver returned invalid values.", ESP_ERR_INVALID_RESPONSE);
    }

    measurement_.clear();
    measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
    measurement_.addValue(SensorValueKind::kCo2Ppm, co2_ppm);
    measurement_.addValue(SensorValueKind::kTemperatureC, temperature_c);
    measurement_.addValue(SensorValueKind::kHumidityPercent, humidity_percent);
    notePollSuccess();
    stepForcedRecalibration(true);
    return ESP_OK;
}

void Scd30Sensor::stepForcedRecalibration(bool got_sample) {
    if (frc_state_ != MaintenanceActionState::kRunning) {
        return;
    }

    if (got_sample) {
        ++frc_good_samples_;
    }

    // Restores the configured measurement interval once FRC reaches a terminal
    // state, undoing the 2 s rate forced during warm-up. Non-fatal on error.
    const auto restore_interval = [this]() {
        const std::uint16_t interval_sec =
            measurementIntervalSeconds(record_.poll_interval_ms);
        if (esp_err_t err = scd30_set_measurement_interval(&device_, interval_sec);
            err != ESP_OK) {
            ESP_LOGW(
                kTag,
                "Failed to restore SCD30 measurement interval after FRC: %s",
                esp_err_to_name(err));
        }
    };

    const std::uint64_t elapsed_ms = nowMilliseconds() - frc_started_ms_;
    if (elapsed_ms < kFrcWarmupMs || frc_good_samples_ < kFrcMinGoodSamples) {
        if (elapsed_ms >= kFrcTimeoutMs) {
            frc_state_ = MaintenanceActionState::kFailed;
            frc_status_ = "FRC failed: timed out before enough stable samples";
            ESP_LOGW(
                kTag,
                "SCD30 FRC timed out after %llu ms with %u samples; giving up",
                static_cast<unsigned long long>(elapsed_ms),
                static_cast<unsigned>(frc_good_samples_));
            restore_interval();
        } else {
            char buffer[48];
            std::snprintf(
                buffer,
                sizeof(buffer),
                "FRC: warming up (%llus/%llus)",
                static_cast<unsigned long long>(elapsed_ms / 1000ULL),
                static_cast<unsigned long long>(kFrcWarmupMs / 1000ULL));
            frc_status_ = buffer;
        }
        return;
    }

    if (esp_err_t err = scd30_set_forced_recalibration_value(&device_, kFrcReferenceCo2Ppm);
        err != ESP_OK) {
        frc_state_ = MaintenanceActionState::kFailed;
        frc_status_ = std::string("FRC failed: ") + esp_err_to_name(err);
        ESP_LOGW(kTag, "SCD30 FRC command failed: %s", esp_err_to_name(err));
    } else {
        frc_state_ = MaintenanceActionState::kCompleted;
        frc_status_ = "FRC complete (400 ppm reference)";
        ESP_LOGI(
            kTag,
            "SCD30 FRC applied at %u ppm after %u samples",
            static_cast<unsigned>(kFrcReferenceCo2Ppm),
            static_cast<unsigned>(frc_good_samples_));
    }
    restore_interval();
}

SensorMeasurement Scd30Sensor::latestMeasurement() const {
    return measurement_;
}

MaintenanceActionState Scd30Sensor::maintenanceActionState() const {
    return frc_state_;
}

std::string Scd30Sensor::maintenanceStatus() const {
    return frc_status_;
}

void Scd30Sensor::acknowledgeMaintenanceAction() {
    // The manager has recorded the terminal result and cleared the pending
    // action from NVS; return to idle so it is not re-reported. Keep the status
    // string so the UI can still show the last outcome until reconfigure.
    frc_state_ = MaintenanceActionState::kIdle;
}

void Scd30Sensor::teardown() {
    initialized_ = false;
    soft_fail_policy_.onPollOk();
    frc_state_ = MaintenanceActionState::kIdle;
    frc_good_samples_ = 0U;
    if (descriptor_initialized_) {
        if (measurement_running_) {
            scd30_stop_continuous_measurement(&device_);
            measurement_running_ = false;
        }
        scd30_free_desc(&device_);
        std::memset(&device_, 0, sizeof(device_));
        descriptor_initialized_ = false;
    }
}

std::unique_ptr<SensorDriver> createScd30Sensor() {
    return std::make_unique<Scd30Sensor>();
}

}  // namespace air360
