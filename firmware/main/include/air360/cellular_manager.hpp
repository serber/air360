#pragma once

#include <cstdint>
#include <string>

#include "air360/cellular_config_repository.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace air360 {

class NetworkManager;  // forward declaration — avoids circular include

// Runtime state of the cellular modem.
// Populated progressively as modem bring-up advances.
struct CellularState {
    bool enabled = false;
    bool modem_detected = false;
    bool sim_ready = false;
    bool registered = false;
    bool ppp_connected = false;
    bool connectivity_ok = false;
    bool connectivity_check_skipped = false;
    std::string ip_address;
    std::string operator_name;
    std::string rat;            // radio access technology, e.g. "LTE"
    int rssi_dbm = 0;
    std::string last_error;
    // Phase 7: reconnect tracking
    std::uint32_t reconnect_attempts = 0U;
    std::uint64_t next_reconnect_uptime_ms = 0U;
};

// Manages the SIM7600E modem lifecycle.
//
// Call order:
//   1. init(network_manager)    — wire up the uplink bearer sink
//   2. start(config)            — init GPIOs; if enabled, spawn reconnect task
//
// The reconnect task runs indefinitely:
//   attemptConnect() → on success wait for disconnect → repeat
//   on failure: exponential backoff; after kMaxReconnectAttempts consecutive
//               failures pulse PWRKEY to hard-reset the modem and start fresh.
//
// Thread-safety: state() returns a const-ref that is read-only.  All writes
// to state_ happen from the cellular task only.  The 10-s polling interval in
// the main task means race windows are negligibly short and the consequence is
// only a stale status snapshot — not a crash.
class CellularManager {
  public:
    // Phase 4: registers the NetworkManager that receives PPP bearer updates.
    void init(NetworkManager& network_manager);

    // Phase 7: configure modem GPIO pins and, if cellular is enabled, launch
    // the reconnect task.  No-op if config.enabled == 0.
    // Must be called after init().  Calling more than once is a no-op.
    void start(const CellularConfig& config);

    const CellularState& state() const;

    // Called by the cellular task once the PPP session is up.
    // Runs the connectivity check, updates state, and notifies NetworkManager.
    //   ip_address — dotted-decimal IP string assigned to the PPP netif
    //   check_host — ICMP ping target; null or empty = skip
    void onPppConnected(const char* ip_address, const char* check_host);

    // Called by the cellular task when the PPP session drops.
    // Notifies NetworkManager so uplinkStatus() reflects the loss.
    //   reason — short description; may be null
    void onPppDisconnected(const char* reason);

  private:
    static void taskEntry(void* arg);
    void taskBody();

    // Attempt to bring up one PPP session.
    // Phase 1 replaces this stub with real esp_modem bring-up code.
    // Returns true if PPP is up; false on any failure.
    bool attemptConnect();

    // Pulse PWRKEY to force the modem off then back on.
    void doHardwareReset();

    // Exponential backoff capped at kMaxBackoffMs.
    // attempt=0 → kBaseBackoffMs; doubles each step.
    static std::uint32_t computeBackoffMs(std::uint32_t attempt);

    CellularConfig config_{};
    CellularState state_;
    NetworkManager* network_manager_ = nullptr;
    TaskHandle_t task_handle_ = nullptr;

    static constexpr std::uint32_t kTaskStackBytes = 8192U;
    static constexpr UBaseType_t kTaskPriority = 5U;
    static constexpr std::uint32_t kMaxReconnectAttempts = 5U;
    static constexpr std::uint32_t kBaseBackoffMs = 10000U;    // 10 s
    static constexpr std::uint32_t kMaxBackoffMs = 300000U;    // 5 min
    // SIM7600E PWRKEY pulse durations (GPIO HIGH).
    static constexpr std::uint32_t kPwrkeyPowerOffMs = 3500U;
    static constexpr std::uint32_t kPwrkeyPowerOnMs  = 2000U;
    static constexpr std::uint32_t kModemShutdownWaitMs = 2000U;
    static constexpr std::uint32_t kModemBootWaitMs    = 5000U;
};

}  // namespace air360
