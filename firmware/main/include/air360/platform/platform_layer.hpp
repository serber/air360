#pragma once

#include "air360/build_info.hpp"
#include "air360/config_repository.hpp"
#include "air360/uploads/air360_api_credentials.hpp"
#include "esp_err.h"

namespace air360 {

class StatusService;

// Owns identity and persistent device-level configuration: build info,
// NVS-backed device config, and the Air360 upload secret repository.
// Booted first; nothing here depends on networking or sensors being ready.
class PlatformLayer {
  public:
    PlatformLayer();
    PlatformLayer(const PlatformLayer&) = delete;
    PlatformLayer& operator=(const PlatformLayer&) = delete;
    PlatformLayer(PlatformLayer&&) = delete;
    PlatformLayer& operator=(PlatformLayer&&) = delete;

    void boot(StatusService& status_service);

    BuildInfo&                     buildInfo()         { return build_info_; }
    const BuildInfo&               buildInfo()   const { return build_info_; }
    ConfigRepository&              deviceConfigRepo()  { return config_repository_; }
    DeviceConfig&                  deviceConfig()      { return config_; }
    const DeviceConfig&            deviceConfig() const { return config_; }
    Air360ApiCredentialRepository& air360Credentials() { return air360_credentials_; }

  private:
    BuildInfo                     build_info_;
    ConfigRepository              config_repository_;
    DeviceConfig                  config_;
    Air360ApiCredentialRepository air360_credentials_;
};

}  // namespace air360
