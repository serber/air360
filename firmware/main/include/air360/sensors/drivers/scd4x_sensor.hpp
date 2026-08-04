#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "air360/sensors/sensor_driver.hpp"
#include "i2cdev.h"

namespace air360 {

enum class Scd4xModel : std::uint8_t {
    kScd40 = 0U,
    kScd41 = 1U,
};

class Scd4xSensor final : public SensorDriver {
  public:
    explicit Scd4xSensor(Scd4xModel model);
    ~Scd4xSensor() override;

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

    Scd4xModel model_;
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

std::unique_ptr<SensorDriver> createScd40Sensor();
std::unique_ptr<SensorDriver> createScd41Sensor();

}  // namespace air360
