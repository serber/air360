#pragma once

#include "air360/cellular_config_repository.hpp"
#include "air360/cellular_manager.hpp"
#include "air360/network_manager.hpp"
#include "esp_timer.h"

namespace air360 {

class StatusService;
class PlatformLayer;

// Owns everything responsible for the device uplink: Wi-Fi, cellular, and the
// debug-window timer that schedules a Wi-Fi shutdown when cellular is the
// primary uplink. Layered above PlatformLayer (reads device + Air360 secrets)
// and below DataLayer.
class NetworkLayer {
  public:
    NetworkLayer();
    NetworkLayer(const NetworkLayer&) = delete;
    NetworkLayer& operator=(const NetworkLayer&) = delete;
    NetworkLayer(NetworkLayer&&) = delete;
    NetworkLayer& operator=(NetworkLayer&&) = delete;

    // Boot step 4b/9: load cellular config, init and start cellular manager.
    // Wi-Fi is intentionally deferred to bootWifi() so that sensor and backend
    // configs can be loaded in between, matching the pre-refactor boot order.
    void bootCellular(PlatformLayer& platform, StatusService& status_service);

    // Boot step 7/9: resolve network mode (station / setup AP / cellular debug
    // window) based on the loaded device + cellular configs.
    void bootWifi(PlatformLayer& platform, StatusService& status_service);

    NetworkManager&               networkManager()      { return network_manager_; }
    const NetworkManager&         networkManager() const { return network_manager_; }
    CellularManager&              cellularManager()     { return cellular_manager_; }
    const CellularManager&        cellularManager() const { return cellular_manager_; }
    CellularConfigRepository&     cellularConfigRepo()  { return cellular_config_repository_; }
    CellularConfig&               cellularConfig()      { return cellular_config_; }
    const CellularConfig&         cellularConfig() const { return cellular_config_; }

  private:
    NetworkManager           network_manager_;
    CellularConfigRepository cellular_config_repository_;
    CellularConfig           cellular_config_;
    CellularManager          cellular_manager_;
    esp_timer_handle_t       debug_window_timer_ = nullptr;
};

}  // namespace air360
