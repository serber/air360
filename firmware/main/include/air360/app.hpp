#pragma once

#include <cstdint>

#include "air360/ble_advertiser.hpp"
#include "air360/build_info.hpp"
#include "air360/cellular_config_repository.hpp"
#include "air360/cellular_manager.hpp"
#include "air360/config_repository.hpp"
#include "air360/network_manager.hpp"
#include "air360/sensors/sensor_config_repository.hpp"
#include "air360/sensors/sensor_manager.hpp"
#include "air360/status_service.hpp"
#include "air360/uploads/backend_config_repository.hpp"
#include "air360/uploads/measurement_store.hpp"
#include "air360/uploads/upload_manager.hpp"
#include "air360/web_server.hpp"
#include "esp_timer.h"

namespace air360 {

class App {
  public:
    App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;
    App(App&&) = delete;
    App& operator=(App&&) = delete;

    void run();

  private:
    BuildInfo build_info_;
    ConfigRepository config_repository_;
    DeviceConfig config_;
    StatusService status_service_;
    SensorConfigRepository sensor_config_repository_;
    SensorConfigList sensor_config_list_;
    SensorManager sensor_manager_;
    MeasurementStore measurement_store_;
    CellularConfigRepository cellular_config_repository_;
    CellularConfig cellular_config_;
    CellularManager cellular_manager_;
    BackendConfigRepository backend_config_repository_;
    BackendConfigList backend_config_list_;
    UploadManager upload_manager_;
    NetworkManager network_manager_;
    WebServer web_server_;
    esp_timer_handle_t debug_window_timer_ = nullptr;
    BleAdvertiser ble_advertiser_;
};

}  // namespace air360
