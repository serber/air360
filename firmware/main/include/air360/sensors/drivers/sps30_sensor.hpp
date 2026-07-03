#pragma once

#include <memory>

#include "air360/sensors/sensor_driver.hpp"
#include "i2cdev.h"

namespace air360 {

class Sps30Sensor final : public SensorDriver {
  public:
    ~Sps30Sensor();
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
    esp_err_t startMeasurement();
    void teardown();
    // Advances the fan-cleaning state machine once per poll. No-op unless a
    // fan-clean action is armed.
    void stepFanCleaning();

    SensorRecord record_{};
    i2c_dev_t device_{};
    bool device_initialized_ = false;
    SensorMeasurement measurement_{};

    // Fan-cleaning one-shot maintenance action state.
    MaintenanceActionState fan_clean_state_ = MaintenanceActionState::kIdle;
    std::string fan_clean_status_;
    bool fan_clean_issued_ = false;
    std::uint64_t fan_clean_started_ms_ = 0U;
};

std::unique_ptr<SensorDriver> createSps30Sensor();

}  // namespace air360
