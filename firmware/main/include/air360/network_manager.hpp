#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "air360/config_repository.hpp"
#include "esp_err.h"
#include "esp_event_base.h"
#include "esp_wifi_types_generic.h"

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
    bool time_sync_attempted = false;
    bool time_synchronized = false;
    bool lab_ap_active = false;
    std::string station_ssid;
    std::string lab_ap_ssid;
    std::string ip_address;
    std::string last_error;
    std::string time_sync_error;
    std::int64_t last_time_sync_unix_ms = 0;
};

struct WifiNetworkRecord {
    std::string ssid;
    int rssi = 0;
    wifi_auth_mode_t auth_mode = WIFI_AUTH_OPEN;
};

struct SntpCheckResult {
    bool success = false;
    std::string error;  // "invalid_input" | "not_connected" | "sync_failed"
};

class NetworkManager {
  public:
    esp_err_t connectStation(const DeviceConfig& config, std::uint32_t timeout_ms = 15000U);
    esp_err_t startLabAp(const DeviceConfig& config);
    esp_err_t scanAvailableNetworks();
    esp_err_t ensureStationTime(std::uint32_t timeout_ms = 15000U);
    SntpCheckResult checkSntp(const std::string& server, std::uint32_t timeout_ms = 10000U);
    const NetworkState& state() const;
    const std::vector<WifiNetworkRecord>& availableNetworks() const;
    const std::string& lastScanError() const;
    std::uint64_t lastScanUptimeMs() const;
    bool hasValidTime() const;
    std::int64_t currentUnixMilliseconds() const;

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
    esp_err_t synchronizeTime(std::uint32_t timeout_ms = 15000U);
    void resetState();

    NetworkState state_{};
    std::vector<WifiNetworkRecord> available_networks_{};
    std::string last_scan_error_{};
    std::uint64_t last_scan_uptime_ms_ = 0U;
    std::string configured_sntp_server_{};
};

}  // namespace air360
