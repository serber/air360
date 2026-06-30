#pragma once

#include <memory>

#include "air360/sensors/sensor_driver.hpp"
#include "i2cdev.h"

namespace air360 {

class Scd30Sensor final : public SensorDriver {
  public:
    Scd30Sensor() = default;
    ~Scd30Sensor() override;

    SensorType type() const override;
    esp_err_t init(
        const SensorRecord& record,
        const SensorDriverContext& context) override;
    esp_err_t poll() override;
    SensorMeasurement latestMeasurement() const override;
    MaintenanceActionState maintenanceActionState() const override;
    std::string maintenanceStatus() const override;
    void acknowledgeMaintenanceAction() override;

  private:
    void teardown();
    // Advances the forced-recalibration (FRC) state machine once per poll while
    // a fresh CO2 sample is available. No-op unless an FRC action is armed.
    void stepForcedRecalibration(bool got_sample);

    SensorRecord record_{};
    i2c_dev_t device_{};
    bool descriptor_initialized_ = false;
    SensorMeasurement measurement_{};
    bool measurement_running_ = false;

    // FRC (forced recalibration) one-shot maintenance action state.
    MaintenanceActionState frc_state_ = MaintenanceActionState::kIdle;
    std::string frc_status_;
    std::uint64_t frc_started_ms_ = 0U;
    std::uint32_t frc_good_samples_ = 0U;
};

std::unique_ptr<SensorDriver> createScd30Sensor();

}  // namespace air360
