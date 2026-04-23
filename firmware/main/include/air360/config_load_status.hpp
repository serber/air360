#pragma once

#include <cstdint>

#include "esp_err.h"

namespace air360 {

enum class ConfigLoadSource : std::uint8_t {
    kNvsPrimary = 0U,
    kNvsBackup,
    kDefaults,
};

enum class ConfigRepositoryKind : std::uint8_t {
    kDevice = 0U,
    kCellular,
    kSensors,
    kBackends,
};

struct ConfigLoadCounters {
    std::uint32_t nvs_primary = 0U;
    std::uint32_t nvs_backup = 0U;
    std::uint32_t defaults = 0U;
};

struct ConfigLoadRuntimeStatus {
    ConfigLoadSource load_source = ConfigLoadSource::kDefaults;
    ConfigLoadCounters counters{};
    esp_err_t last_error = ESP_OK;
    bool wrote_defaults = false;
};

}  // namespace air360
