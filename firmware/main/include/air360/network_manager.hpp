#pragma once

#include <string>

#include "air360/config_repository.hpp"
#include "esp_err.h"

namespace air360 {

struct NetworkState {
    bool lab_ap_active = false;
    std::string ip_address;
};

class NetworkManager {
  public:
    esp_err_t startLabAp(const DeviceConfig& config);
    const NetworkState& state() const;

  private:
    NetworkState state_{};
};

}  // namespace air360
