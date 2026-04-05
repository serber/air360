#pragma once

#include <memory>
#include <string>

#include "air360/sensors/sensor_driver.hpp"
#include "esp_err.h"

struct adc_oneshot_unit_ctx_t;
struct adc_cali_scheme_t;

namespace air360 {

class Me3No2Sensor final : public SensorDriver {
  public:
    ~Me3No2Sensor() override;

    SensorType type() const override;
    esp_err_t init(
        const SensorRecord& record,
        const SensorDriverContext& context) override;
    esp_err_t poll() override;
    SensorMeasurement latestMeasurement() const override;
    std::string lastError() const override;

  private:
    void setError(const std::string& message);
    void releaseHandles();

    SensorRecord record_{};
    SensorMeasurement measurement_{};
    std::string last_error_;
    adc_oneshot_unit_ctx_t* adc_handle_ = nullptr;
    adc_cali_scheme_t* cali_handle_ = nullptr;
    int channel_ = -1;
    bool calibration_enabled_ = false;
    bool initialized_ = false;
};

std::unique_ptr<SensorDriver> createMe3No2Sensor();

}  // namespace air360
