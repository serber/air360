#pragma once

#include <cstdint>
#include <string>

namespace air360 {

class NetworkManager;  // forward declaration — avoids circular include

// Runtime state of the cellular modem.
// Populated progressively as modem bring-up advances (Phase 1+).
// connectivity_ok / connectivity_check_skipped are set after the post-PPP
// connectivity check (Phase 2).
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
    std::string rat;        // radio access technology reported by modem, e.g. "LTE"
    int rssi_dbm = 0;
    std::string last_error;
};

// Manages the SIM7600E modem lifecycle.
//
// Phase 1 (pending): modem power-on, AT init, PDP context, PPP session.
// Phase 2 (done):    connectivity check after PPP establishes.
// Phase 4 (done):    wires PPP events into NetworkManager::uplinkStatus().
//
// Thread-safety: state() is read-only and safe to call from any task.
// Mutating methods (onPppConnected, onPppDisconnected) must be called from
// the cellular task only (Phase 1).
class CellularManager {
  public:
    // Phase 4: registers the NetworkManager that receives PPP bearer updates.
    // Must be called before the cellular task is started (Phase 1).
    void init(NetworkManager& network_manager);

    const CellularState& state() const;

    // Called by the cellular task (Phase 1) once the PPP session is up and
    // the netif IP is known.  Runs the connectivity check, updates state, and
    // calls NetworkManager::setCellularStatus(true, ip) so uplinkStatus()
    // reflects the active bearer.
    //   ip_address  — dotted-decimal IP string assigned to the PPP netif
    //   check_host  — ICMP ping target; null or empty = skip check
    void onPppConnected(const char* ip_address, const char* check_host);

    // Called by the cellular task (Phase 1) when the PPP session drops.
    // Calls NetworkManager::setCellularStatus(false, nullptr).
    //   reason — short description logged and stored in last_error; may be null
    void onPppDisconnected(const char* reason);

  private:
    CellularState state_;
    NetworkManager* network_manager_ = nullptr;
};

}  // namespace air360
