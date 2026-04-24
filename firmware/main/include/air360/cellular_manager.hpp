#pragma once

#include <cstdint>
#include <string>

#include "air360/cellular_config_repository.hpp"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace air360 {

class NetworkManager;  // forward declaration — avoids circular include

// Runtime state of the cellular modem.
struct CellularState {
    bool enabled = false;
    bool modem_detected = false;
    bool sim_ready = false;
    bool registered = false;
    bool ppp_connected = false;
    bool connectivity_ok = false;
    bool connectivity_check_skipped = false;
    std::string ip_address;
    int rssi_dbm = 0;
    std::string last_error;
    // Reconnect and hardware escalation tracking.
    std::uint32_t reconnect_attempts = 0U;
    std::uint32_t consecutive_failures = 0U;
    std::uint32_t pwrkey_cycles_total = 0U;
    std::uint64_t last_pwrkey_uptime_ms = 0U;
    std::uint64_t next_reconnect_uptime_ms = 0U;
};

// Manages the SIM7600E modem lifecycle.
//
// Call order:
//   1. init(network_manager)  — wire up the uplink bearer sink
//   2. start(config)          — init GPIOs; if enabled, spawn reconnect task
//
// The reconnect task runs indefinitely:
//   attemptConnect() brings up PPP and blocks until the session drops.
//   On failure: fixed exponential backoff, time-based hard retry/PWRKEY
//   escalation, and PWRKEY rate limiting.
class CellularManager {
  public:
    CellularManager();
    CellularManager(const CellularManager&) = delete;
    CellularManager& operator=(const CellularManager&) = delete;
    CellularManager(CellularManager&&) = delete;
    CellularManager& operator=(CellularManager&&) = delete;

    void init(NetworkManager& network_manager);

    // Initialize modem GPIOs and, if cellular is enabled, launch the reconnect
    // task.  No-op if config.enabled == 0.  Calling more than once is a no-op.
    void start(const CellularConfig& config);

    CellularState state() const;
    std::size_t taskStackHighWaterMarkBytes() const;

    // Called internally once PPP is up and an IP has been assigned.
    void onPppConnected(const char* ip_address, const char* check_host);

    // Called internally when the PPP session drops.
    void onPppDisconnected(const char* reason);

  private:
    void lock() const;
    void unlock() const;

    // FreeRTOS task
    static void taskEntry(void* arg);
    void taskBody();

    // Full PPP session lifecycle: allocate DTE/DCE, start PPP, wait for IP,
    // block until session drops, then tear down.  Returns true if the session
    // was up at least once (clean disconnect); false on setup failure.
    bool attemptConnect();

    // Release all modem resources allocated by attemptConnect().
    void teardownModem();

    // Probe and forcibly unwind an established PPP session when no LOST_IP
    // event arrives but the link stops passing traffic.
    bool probeLink();
    void forceDisconnect(const char* reason);

    // Pulse PWRKEY to force the modem off then back on.
    bool doHardwareReset();
    bool waitForNetworkRegistration(void* dce);
    void resetFailureWindow();

    // Fixed exponential backoff capped at 15 min.
    static std::uint32_t computeBackoffMs(std::uint32_t attempt);

    // ESP event handlers (static, called from the system event loop task).
    static void onGotIpEvent(void* arg, esp_event_base_t base, int32_t id, void* data);
    static void onLostIpEvent(void* arg, esp_event_base_t base, int32_t id, void* data);

    CellularConfig config_{};
    CellularState state_;
    NetworkManager* network_manager_ = nullptr;
    TaskHandle_t task_handle_ = nullptr;
    mutable StaticSemaphore_t mutex_buffer_{};
    mutable SemaphoreHandle_t mutex_ = nullptr;

    // Opaque handles for modem resources — typed as void* to avoid pulling
    // esp_modem_api.h and esp_event.h into every translation unit that
    // includes this header.
    void* dce_ = nullptr;              // esp_modem_dce_t*
    void* ppp_netif_ = nullptr;        // esp_netif_t*
    void* ip_got_handler_ = nullptr;   // esp_event_handler_instance_t
    void* ip_lost_handler_ = nullptr;  // esp_event_handler_instance_t
    StaticEventGroup_t ppp_event_group_buf_{};
    EventGroupHandle_t ppp_event_group_ = nullptr;
    char pending_ip_[16]{};            // written by event handler, read by task
    std::uint64_t failure_window_start_ms_ = 0U;
    std::uint32_t pwrkey_cycles_in_failure_window_ = 0U;
    bool hard_retry_logged_ = false;
    bool pwrkey_escalation_logged_ = false;

    static constexpr EventBits_t kGotIpBit  = BIT0;
    static constexpr EventBits_t kLostIpBit = BIT1;

    static constexpr std::uint32_t kTaskStackBytes = 8192U;
    static constexpr UBaseType_t   kTaskPriority   = 5U;

    static constexpr std::uint32_t kHardRetryAfterMs = 120000U;       // 2 min
    static constexpr std::uint32_t kPwrkeyAfterMs = 600000U;          // 10 min
    static constexpr std::uint32_t kMinPwrkeyIntervalMs = 3600000U;   // 1 h
    static constexpr std::uint32_t kMaxPwrkeyBeforeReboot = 2U;

    // SIM7600E PWRKEY pulse durations (GPIO HIGH asserts the line).
    static constexpr std::uint32_t kPwrkeyPowerOffMs   = 3500U;
    static constexpr std::uint32_t kPwrkeyPowerOnMs    = 2000U;
    static constexpr std::uint32_t kModemShutdownWaitMs = 2000U;
    static constexpr std::uint32_t kModemBootWaitMs     = 5000U;

    // How long to wait for IP after entering PPP data mode.
    static constexpr std::uint32_t kPppIpTimeoutMs = 30000U;

    // How long to wait for network registration (polling RSSI).
    static constexpr std::uint32_t kRegMaxWaitMs  = 60000U;
    static constexpr std::uint32_t kRegPollMs     = 2000U;
};

}  // namespace air360
