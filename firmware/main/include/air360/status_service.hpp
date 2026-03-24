#pragma once

#include <cstdint>
#include <string>

#include "air360/build_info.hpp"
#include "air360/config_repository.hpp"
#include "air360/network_manager.hpp"
#include "esp_system.h"

namespace air360 {

class StatusService {
  public:
    explicit StatusService(BuildInfo build_info);

    void markNvsReady(bool ready);
    void markWatchdogArmed(bool armed);
    void setConfig(
        const DeviceConfig& config,
        bool loaded_from_storage,
        bool wrote_defaults);
    void setBootCount(std::uint32_t boot_count);
    void setNetworkState(const NetworkState& state);
    void setWebServerStarted(bool started);

    std::string renderRootHtml() const;
    std::string renderStatusJson() const;
    const NetworkState& networkState() const;

  private:
    BuildInfo build_info_;
    DeviceConfig config_{};
    NetworkState network_state_{};
    std::uint32_t boot_count_ = 0;
    bool nvs_ready_ = false;
    bool watchdog_armed_ = false;
    bool config_loaded_from_storage_ = false;
    bool wrote_default_config_ = false;
    bool web_server_started_ = false;
    esp_reset_reason_t reset_reason_ = esp_reset_reason();
};

}  // namespace air360
