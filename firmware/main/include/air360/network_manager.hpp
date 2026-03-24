#pragma once

#include <cstdint>
#include <string>

#include "air360/config_repository.hpp"
#include "esp_err.h"
#include "esp_event_base.h"

namespace air360 {

enum class NetworkMode : std::uint8_t {
    kOffline = 0,
    kSetupAp,
    kStation,
};

struct NetworkState {
    NetworkMode mode = NetworkMode::kOffline;
    bool station_config_present = false;
    bool station_connect_attempted = false;
    bool station_connected = false;
    bool lab_ap_active = false;
    std::string station_ssid;
    std::string lab_ap_ssid;
    std::string ip_address;
    std::string last_error;
};

class NetworkManager {
  public:
    esp_err_t connectStation(const DeviceConfig& config, std::uint32_t timeout_ms = 15000U);
    esp_err_t startLabAp(const DeviceConfig& config);
    const NetworkState& state() const;

  private:
    static void handleWifiEvent(
        void* arg,
        esp_event_base_t event_base,
        int32_t event_id,
        void* event_data);
    static void handleIpEvent(
        void* arg,
        esp_event_base_t event_base,
        int32_t event_id,
        void* event_data);

    esp_err_t ensureWifiInit();
    void resetState();

    NetworkState state_{};
};

}  // namespace air360
