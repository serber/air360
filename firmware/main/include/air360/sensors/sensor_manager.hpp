#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "air360/sensors/sensor_config.hpp"

namespace air360 {

struct SensorRuntimeInfo {
    std::uint32_t id = 0U;
    bool enabled = false;
    SensorType sensor_type = SensorType::kUnknown;
    TransportKind transport_kind = TransportKind::kUnknown;
    std::string type_key;
    std::string type_name;
    std::string display_name;
    std::string binding_summary;
    SensorRuntimeState state = SensorRuntimeState::kUnsupported;
    std::string last_error;
};

class SensorManager {
  public:
    void applyConfig(const SensorConfigList& config);

    const std::vector<SensorRuntimeInfo>& sensors() const;
    std::size_t configuredCount() const;
    std::size_t enabledCount() const;

  private:
    std::vector<SensorRuntimeInfo> sensors_;
};

}  // namespace air360
