#pragma once

#include <cstdint>

#include "air360/config_repository.hpp"
#include "air360/status_service.hpp"
#include "air360/sensors/sensor_config_repository.hpp"
#include "air360/sensors/sensor_manager.hpp"
#include "esp_err.h"
#include "esp_http_server.h"

namespace air360 {

class WebServer {
  public:
    esp_err_t start(
        StatusService& status_service,
        ConfigRepository& config_repository,
        DeviceConfig& config,
        SensorConfigRepository& sensor_config_repository,
        SensorConfigList& sensor_config_list,
        SensorManager& sensor_manager,
        std::uint16_t port);
    void stop();

  private:
    static esp_err_t handleRoot(httpd_req_t* request);
    static esp_err_t handleStatus(httpd_req_t* request);
    static esp_err_t handleConfig(httpd_req_t* request);
    static esp_err_t handleSensors(httpd_req_t* request);
    static esp_err_t handleI2cScan(httpd_req_t* request);

    httpd_handle_t handle_ = nullptr;
    StatusService* status_service_ = nullptr;
    ConfigRepository* config_repository_ = nullptr;
    DeviceConfig* config_ = nullptr;
    SensorConfigRepository* sensor_config_repository_ = nullptr;
    SensorConfigList* sensor_config_list_ = nullptr;
    SensorManager* sensor_manager_ = nullptr;
};

}  // namespace air360
