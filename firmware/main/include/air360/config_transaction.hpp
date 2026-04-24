#pragma once

#include "air360/cellular_config_repository.hpp"
#include "air360/config_repository.hpp"
#include "esp_err.h"

namespace air360 {

[[nodiscard]] esp_err_t saveDeviceAndCellularConfig(
    const ConfigRepository& config_repository,
    const CellularConfigRepository& cellular_config_repository,
    const DeviceConfig& device_config,
    const CellularConfig& cellular_config);

}  // namespace air360
