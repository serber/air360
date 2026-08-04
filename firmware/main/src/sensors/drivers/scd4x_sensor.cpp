#include "air360/sensors/drivers/scd4x_sensor.hpp"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "air360/sensors/transport_binding.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "scd4x.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.sensor.scd4x";
constexpr std::uint32_t kScd4xI2cSpeedHz = 100000U;

// Forced recalibration (FRC) tuning. The SCD4x datasheet asks for the sensor to
// run in periodic measurement mode for at least 3 minutes in a stable,
// homogeneous reference concentration before the FRC command is issued, so the
// driver forces the fast (~5 s) regular periodic measurement mode instead of the
// normal low-power (~30 s) mode while FRC is armed, and gates the command on
// both elapsed time and a minimum number of fresh samples. 400 ppm is the
// fresh outdoor-air reference matching the permanently-powered outdoor-unit use
// case.
constexpr std::uint64_t kFrcWarmupMs = 180'000U;
constexpr std::uint64_t kFrcTimeoutMs = 360'000U;
constexpr std::uint32_t kFrcMinGoodSamples = 30U;
constexpr std::uint16_t kFrcReferenceCo2Ppm = 400U;
// Sensirion datasheet: the FRC correction reads back as 0xFFFF when the sensor
// rejects the recalibration (e.g. it was not idle or the concentration step
// was out of range).
constexpr std::uint16_t kFrcCorrectionFailureSentinel = 0xFFFFU;

std::uint64_t nowMilliseconds() {
    return static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
}

}  // namespace

Scd4xSensor::Scd4xSensor(Scd4xModel model) : model_(model) {}

Scd4xSensor::~Scd4xSensor() {
    teardown();
}

SensorType Scd4xSensor::type() const {
    return model_ == Scd4xModel::kScd40 ? SensorType::kScd40 : SensorType::kScd41;
}

esp_err_t Scd4xSensor::init(const SensorRecord& record, const SensorDriverContext& context) {
    teardown();
    record_ = record;
    measurement_.clear();
    clearError();
    soft_fail_policy_.onPollOk();

    i2c_port_t port = I2C_NUM_0;
    gpio_num_t sda = GPIO_NUM_NC;
    gpio_num_t scl = GPIO_NUM_NC;
    if (!context.i2c_bus_manager->resolvePins(record.i2c_bus_id, port, sda, scl)) {
        setError("Unknown I2C bus id for SCD4x.");
        return ESP_ERR_NOT_SUPPORTED;
    }

    std::memset(&device_, 0, sizeof(device_));
    esp_err_t err = scd4x_init_desc(&device_, port, sda, scl);
    if (err != ESP_OK) {
        setError("Failed to initialize SCD4x descriptor.");
        teardown();
        return err;
    }
    descriptor_initialized_ = true;
    context.i2c_bus_manager->applyDescriptorDefaults(device_, kScd4xI2cSpeedHz);

    err = i2c_dev_check_present(&device_);
    if (err != ESP_OK) {
        setError("SCD4x sensor was not detected on I2C bus 0 at address 0x62.");
        teardown();
        return err;
    }

    // Force idle state before sending configuration commands. Most SCD4x
    // commands (including ASC and FRC) NACK while a measurement is running.
    // This is best-effort: the sensor may already be idle (e.g. first boot),
    // in which case the stop command is a harmless no-op.
    if (esp_err_t stop_err = scd4x_stop_periodic_measurement(&device_); stop_err != ESP_OK) {
        ESP_LOGW(kTag, "SCD4x stop-before-configure returned %s (may already be idle)",
                 esp_err_to_name(stop_err));
    }

    // Apply the configured automatic self-calibration (ASC) state. Re-asserting
    // it on every init keeps the firmware config authoritative even if the
    // sensor is swapped. A failure is non-fatal: the sensor still measures,
    // just without the requested ASC state.
    const bool asc_enabled = record_.startup_calibration != 0U;
    if (esp_err_t asc_err = scd4x_set_automatic_self_calibration(&device_, asc_enabled);
        asc_err != ESP_OK) {
        ESP_LOGW(
            kTag,
            "Failed to set SCD4x ASC to %s: %s",
            asc_enabled ? "enabled" : "disabled",
            esp_err_to_name(asc_err));
    } else {
        ESP_LOGI(kTag, "SCD4x ASC %s", asc_enabled ? "enabled" : "disabled");
    }

    // Arm (or disarm) the FRC one-shot maintenance action. Re-arming on every
    // init keeps run-once at-least-once semantics: a reboot mid-FRC simply
    // restarts the warm-up until the manager clears the pending action.
    const bool frc_armed =
        record_.pending_maintenance_action ==
        static_cast<std::uint8_t>(MaintenanceActionKind::kForcedRecalibration);
    if (frc_armed) {
        err = scd4x_start_periodic_measurement(&device_);
    } else {
        err = scd4x_start_low_power_periodic_measurement(&device_);
        frc_state_ = MaintenanceActionState::kIdle;
        frc_status_.clear();
    }
    if (err != ESP_OK) {
        setError("Failed to start SCD4x periodic measurement.");
        teardown();
        return err;
    }

    if (frc_armed) {
        frc_state_ = MaintenanceActionState::kRunning;
        frc_started_ms_ = nowMilliseconds();
        frc_good_samples_ = 0U;
        frc_status_ = "FRC: warming up";
        ESP_LOGI(
            kTag,
            "SCD4x FRC armed: warming up %llu s before recalibrating to %u ppm",
            static_cast<unsigned long long>(kFrcWarmupMs / 1000ULL),
            static_cast<unsigned>(kFrcReferenceCo2Ppm));
    }

    measurement_running_ = true;
    initialized_ = true;
    setError("Waiting for first SCD4x sample.");
    return ESP_OK;
}

esp_err_t Scd4xSensor::poll() {
    if (!initialized_ || !descriptor_initialized_) {
        setError("SCD4x sensor is not initialized.");
        return ESP_ERR_INVALID_STATE;
    }

    bool data_ready = false;
    if (esp_err_t err = scd4x_get_data_ready_status(&device_, &data_ready); err != ESP_OK) {
        return reportPollFailure(kTag, "Failed to query SCD4x data-ready status.", err);
    }

    if (!data_ready) {
        measurement_.clear();
        soft_fail_policy_.onPollOk();
        setError("Waiting for new SCD4x sample.");
        stepForcedRecalibration(false);
        return ESP_OK;
    }

    std::uint16_t co2_ppm = 0U;
    float temperature_c = 0.0F;
    float humidity_percent = 0.0F;
    if (esp_err_t err = scd4x_read_measurement(&device_, &co2_ppm, &temperature_c, &humidity_percent);
        err != ESP_OK) {
        return reportPollFailure(kTag, "Failed to read SCD4x measurement.", err);
    }

    // A CO2 reading of exactly 0 ppm is the sensor's own sentinel for "sample
    // not yet valid" (seen during the first few seconds after starting
    // periodic measurement) rather than a real-world reading.
    if (co2_ppm == 0U) {
        measurement_.clear();
        soft_fail_policy_.onPollOk();
        setError("SCD4x reported an invalid (zero) CO2 reading; waiting for next sample.");
        stepForcedRecalibration(false);
        return ESP_OK;
    }

    measurement_.clear();
    measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
    measurement_.addValue(SensorValueKind::kCo2Ppm, static_cast<float>(co2_ppm));
    measurement_.addValue(SensorValueKind::kTemperatureC, temperature_c);
    measurement_.addValue(SensorValueKind::kHumidityPercent, humidity_percent);
    notePollSuccess();
    stepForcedRecalibration(true);
    return ESP_OK;
}

void Scd4xSensor::stepForcedRecalibration(bool got_sample) {
    if (frc_state_ != MaintenanceActionState::kRunning) {
        return;
    }

    if (got_sample) {
        ++frc_good_samples_;
    }

    // Resumes normal low-power periodic measurement once FRC reaches a
    // terminal state. Stops first unconditionally: the timeout path below never
    // issued an explicit stop (unlike the post-warmup FRC sequence, which stops
    // before recalibrating), so the sensor may still be in fast periodic
    // measurement mode. Non-fatal on error, but the driver forces a re-init if
    // starting fails since the sensor is otherwise left idle and would stop
    // producing measurements.
    const auto resume_low_power = [this]() {
        if (esp_err_t stop_err = scd4x_stop_periodic_measurement(&device_); stop_err != ESP_OK) {
            ESP_LOGW(kTag, "SCD4x stop-before-resume returned %s (may already be idle)",
                     esp_err_to_name(stop_err));
        }
        if (esp_err_t err = scd4x_start_low_power_periodic_measurement(&device_); err != ESP_OK) {
            ESP_LOGE(
                kTag,
                "Failed to resume SCD4x low-power measurement after FRC: %s",
                esp_err_to_name(err));
            initialized_ = false;
            measurement_running_ = false;
            return;
        }
        measurement_running_ = true;
    };

    const std::uint64_t elapsed_ms = nowMilliseconds() - frc_started_ms_;
    if (elapsed_ms < kFrcWarmupMs || frc_good_samples_ < kFrcMinGoodSamples) {
        if (elapsed_ms >= kFrcTimeoutMs) {
            frc_state_ = MaintenanceActionState::kFailed;
            frc_status_ = "FRC failed: timed out before enough stable samples";
            ESP_LOGW(
                kTag,
                "SCD4x FRC timed out after %llu ms with %u samples; giving up",
                static_cast<unsigned long long>(elapsed_ms),
                static_cast<unsigned>(frc_good_samples_));
            measurement_running_ = false;
            resume_low_power();
        } else {
            char buffer[56];
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

    // Warm-up satisfied: stop periodic measurement (required before FRC), issue
    // the recalibration, then unconditionally resume normal operation.
    if (esp_err_t stop_err = scd4x_stop_periodic_measurement(&device_); stop_err != ESP_OK) {
        frc_state_ = MaintenanceActionState::kFailed;
        frc_status_ = std::string("FRC failed: could not stop measurement: ") +
            esp_err_to_name(stop_err);
        ESP_LOGW(kTag, "SCD4x FRC stop-measurement failed: %s", esp_err_to_name(stop_err));
        measurement_running_ = false;
    } else {
        measurement_running_ = false;
        std::uint16_t frc_correction = 0U;
        esp_err_t frc_err =
            scd4x_perform_forced_recalibration(&device_, kFrcReferenceCo2Ppm, &frc_correction);
        if (frc_err != ESP_OK || frc_correction == kFrcCorrectionFailureSentinel) {
            frc_state_ = MaintenanceActionState::kFailed;
            frc_status_ = "FRC failed: sensor rejected calibration";
            ESP_LOGW(
                kTag,
                "SCD4x FRC command failed: %s (correction=0x%04x)",
                esp_err_to_name(frc_err),
                static_cast<unsigned>(frc_correction));
        } else {
            // Persist so the correction survives a sensor power cycle. Only
            // done on an actual calibration event, not every boot, to respect
            // the internal EEPROM's limited write endurance.
            if (esp_err_t persist_err = scd4x_persist_settings(&device_); persist_err != ESP_OK) {
                ESP_LOGW(kTag, "Failed to persist SCD4x settings after FRC: %s",
                         esp_err_to_name(persist_err));
            }
            frc_state_ = MaintenanceActionState::kCompleted;
            frc_status_ = "FRC complete (400 ppm reference)";
            ESP_LOGI(
                kTag,
                "SCD4x FRC applied at %u ppm after %u samples (correction=%d)",
                static_cast<unsigned>(kFrcReferenceCo2Ppm),
                static_cast<unsigned>(frc_good_samples_),
                static_cast<int>(frc_correction) - 0x8000);
        }
    }

    resume_low_power();
}

SensorMeasurement Scd4xSensor::latestMeasurement() const {
    return measurement_;
}

MaintenanceActionState Scd4xSensor::maintenanceActionState() const {
    return frc_state_;
}

std::string Scd4xSensor::maintenanceStatus() const {
    return frc_status_;
}

void Scd4xSensor::acknowledgeMaintenanceAction() {
    // The manager has recorded the terminal result and cleared the pending
    // action from NVS; return to idle so it is not re-reported. Keep the status
    // string so the UI can still show the last outcome until reconfigure.
    frc_state_ = MaintenanceActionState::kIdle;
}

void Scd4xSensor::teardown() {
    initialized_ = false;
    soft_fail_policy_.onPollOk();
    frc_state_ = MaintenanceActionState::kIdle;
    frc_good_samples_ = 0U;
    if (descriptor_initialized_) {
        if (measurement_running_) {
            if (esp_err_t err = scd4x_stop_periodic_measurement(&device_); err != ESP_OK) {
                ESP_LOGW(kTag, "Failed to stop SCD4x periodic measurement: %s",
                         esp_err_to_name(err));
            }
            measurement_running_ = false;
        }
        if (esp_err_t err = scd4x_free_desc(&device_); err != ESP_OK) {
            ESP_LOGW(kTag, "Failed to free SCD4x descriptor: %s", esp_err_to_name(err));
        }
        std::memset(&device_, 0, sizeof(device_));
        descriptor_initialized_ = false;
    }
}

std::unique_ptr<SensorDriver> createScd40Sensor() {
    return std::make_unique<Scd4xSensor>(Scd4xModel::kScd40);
}

std::unique_ptr<SensorDriver> createScd41Sensor() {
    return std::make_unique<Scd4xSensor>(Scd4xModel::kScd41);
}

}  // namespace air360
