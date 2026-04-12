#pragma once

#include <cstdint>
#include <string>

namespace air360 {

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
// Phase 2 (current): connectivity check after PPP is established.
// Phase 1 (pending): modem power-on, AT init, PDP context, PPP session.
//
// Thread-safety: state() is read-only and safe to call from any task.
// Mutating methods (onPppConnected, onPppDisconnected) must be called from
// the cellular task only (Phase 1).
class CellularManager {
  public:
    const CellularState& state() const;

    // Called by the cellular task (Phase 1) once the PPP session is up and
    // the netif IP is known.  Runs the connectivity check synchronously and
    // updates state accordingly.
    //   ip_address  — dotted-decimal IP string assigned to the PPP netif
    //   check_host  — target for ICMP ping; null or empty = skip check
    void onPppConnected(const char* ip_address, const char* check_host);

    // Called by the cellular task (Phase 1) when the PPP session drops.
    //   reason — short description for the log and last_error field; may be null
    void onPppDisconnected(const char* reason);

  private:
    CellularState state_;
};

}  // namespace air360
