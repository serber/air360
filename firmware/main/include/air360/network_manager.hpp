#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "air360/config_repository.hpp"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_event_base.h"
#include "esp_netif.h"
#include "esp_wifi_types_generic.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/timers.h"

namespace air360 {

enum class NetworkMode : std::uint8_t {
    kOffline = 0,
    kSetupAp,
    kStation,
};

enum class UplinkBearer : std::uint8_t {
    kNone = 0,
    kWifi,
    kCellular,
};

struct UplinkStatus {
    bool uplink_ready = false;
    UplinkBearer active_bearer = UplinkBearer::kNone;
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
    std::string cellular_ip;  // populated by CellularManager in Phase 1
    std::string last_error;
    std::int32_t last_disconnect_reason = -1;
    std::string last_disconnect_reason_label;
    bool reconnect_backoff_active = false;
    std::uint32_t reconnect_attempt_count = 0U;
    std::uint64_t next_reconnect_uptime_ms = 0U;
    bool setup_ap_retry_active = false;
    std::uint64_t next_setup_ap_retry_uptime_ms = 0U;
    std::string time_sync_error;
    std::int64_t last_time_sync_unix_ms = 0;
};

struct WifiNetworkRecord {
    std::string ssid;
    int rssi = 0;
    wifi_auth_mode_t auth_mode = WIFI_AUTH_OPEN;
};

struct WifiScanSnapshot {
    std::vector<WifiNetworkRecord> networks;
    std::string last_scan_error;
    std::uint64_t last_scan_uptime_ms = 0U;
    bool scan_in_progress = false;
};

struct SntpCheckResult {
    bool success = false;
    std::string error;  // "invalid_input" | "not_connected" | "sync_failed"
};

class NetworkManager {
  public:
    NetworkManager();
    NetworkManager(const NetworkManager&) = delete;
    NetworkManager& operator=(const NetworkManager&) = delete;
    NetworkManager(NetworkManager&&) = delete;
    NetworkManager& operator=(NetworkManager&&) = delete;

    [[nodiscard]] esp_err_t connectStation(
        const DeviceConfig& config,
        std::uint32_t timeout_ms = 15000U);
    [[nodiscard]] esp_err_t startLabAp(const DeviceConfig& config);
    [[nodiscard]] esp_err_t stopStation();
    // Called by CellularManager when the PPP session comes up or drops.
    // Updates cellular_ip in NetworkState and affects uplinkStatus().
    void setCellularStatus(bool ppp_connected, const char* ip_address);
    [[nodiscard]] esp_err_t scanAvailableNetworks();
    [[nodiscard]] esp_err_t ensureStationTime(std::uint32_t timeout_ms = 15000U);
    SntpCheckResult checkSntp(const std::string& server, std::uint32_t timeout_ms = 10000U);
    UplinkStatus uplinkStatus() const;
    NetworkState state() const;
    WifiScanSnapshot wifiScanSnapshot() const;
    bool hasValidTime() const;
    std::int64_t currentUnixMilliseconds() const;

  private:
    enum class ConnectAttemptKind : std::uint8_t {
        kInitial = 0U,
        kRuntimeReconnect,
        kSetupApRetry,
    };

    struct RuntimeContext {
        EventGroupHandle_t station_events = nullptr;
        esp_netif_t* ap_netif = nullptr;
        esp_netif_t* sta_netif = nullptr;
        TimerHandle_t reconnect_timer = nullptr;
        TimerHandle_t setup_ap_retry_timer = nullptr;
        TaskHandle_t worker_task = nullptr;
        StaticSemaphore_t scan_request_mutex_buf = {};
        SemaphoreHandle_t scan_request_mutex = nullptr;
        StaticSemaphore_t scan_done_buf = {};
        SemaphoreHandle_t scan_done = nullptr;
        esp_event_handler_instance_t wifi_handler = nullptr;
        esp_event_handler_instance_t ip_handler = nullptr;
        bool handlers_registered = false;
        bool auto_connect_on_sta_start = false;
        bool reconnect_cycle_active = false;
        std::uint64_t ignore_disconnect_until_ms = 0U;
        bool wifi_initialized = false;
        bool sntp_initialized = false;
        bool mdns_initialized = false;
    };

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
    static void reconnectTimerCallback(TimerHandle_t timer);
    static void setupApRetryTimerCallback(TimerHandle_t timer);
    static void workerTask(void* arg);

    esp_err_t ensureWifiInit();
    esp_err_t attemptStationConnect(
        const DeviceConfig& config,
        std::uint32_t timeout_ms,
        ConnectAttemptKind kind);
    esp_err_t startAsyncScanAndWait();
    esp_err_t synchronizeTime(std::uint32_t timeout_ms = 15000U);
    void workerLoop();
    void notifyWorker(std::uint32_t request_bits);
    void startMdns(const std::string& hostname);
    void lock() const;
    void unlock() const;

    mutable StaticSemaphore_t mutex_buffer_{};
    mutable SemaphoreHandle_t mutex_ = nullptr;
    RuntimeContext runtime_{};
    DeviceConfig last_config_{};
    bool has_last_config_ = false;
    NetworkState state_{};
    std::vector<WifiNetworkRecord> available_networks_{};
    std::string last_scan_error_{};
    std::uint64_t last_scan_uptime_ms_ = 0U;
    bool scan_in_progress_ = false;
    std::string configured_sntp_server_{};
};

}  // namespace air360
