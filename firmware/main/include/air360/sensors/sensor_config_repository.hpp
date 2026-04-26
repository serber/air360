#pragma once

#include "air360/sensors/sensor_config.hpp"
#include "esp_err.h"

namespace air360 {

class SensorConfigRepository {
  public:
    [[nodiscard]] esp_err_t loadOrCreate(
        SensorConfigList& out_config,
        bool& loaded_from_storage,
        bool& wrote_defaults);
    [[nodiscard]] esp_err_t save(const SensorConfigList& config);

  private:
    bool isValid(const SensorConfigList& config) const;
};

}  // namespace air360
