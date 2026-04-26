#pragma once

#include <cstdint>

#include "air360/cellular_config_repository.hpp"
#include "air360/config_repository.hpp"
#include "air360/network_manager.hpp"
#include "air360/status_service.hpp"
#include "air360/sensors/sensor_config_repository.hpp"
#include "air360/sensors/sensor_manager.hpp"
#include "air360/uploads/measurement_store.hpp"
#include "air360/uploads/backend_config_repository.hpp"
#include "air360/uploads/upload_manager.hpp"
#include "esp_err.h"
#include "esp_http_server.h"

namespace air360 {

class WebServer {
  public:
    WebServer() = default;
    WebServer(const WebServer&) = delete;
    WebServer& operator=(const WebServer&) = delete;
    WebServer(WebServer&&) = delete;
    WebServer& operator=(WebServer&&) = delete;

    [[nodiscard]] esp_err_t start(
        StatusService& status_service,
        NetworkManager& network_manager,
        ConfigRepository& config_repository,
        DeviceConfig& config,
        SensorConfigRepository& sensor_config_repository,
        SensorConfigList& sensor_config_list,
        SensorManager& sensor_manager,
        MeasurementStore& measurement_store,
        BackendConfigRepository& backend_config_repository,
        BackendConfigList& backend_config_list,
        UploadManager& upload_manager,
        CellularConfigRepository& cellular_config_repository,
        CellularConfig& cellular_config,
        std::uint16_t port);
    void stop();

  private:
    static esp_err_t handleAsset(httpd_req_t* request);
    static esp_err_t handleFavicon(httpd_req_t* request);
    static esp_err_t handleRoot(httpd_req_t* request);
    static esp_err_t handleDiagnostics(httpd_req_t* request);
    static esp_err_t handleLogsData(httpd_req_t* request);
    static esp_err_t handleWifiScan(httpd_req_t* request);
    static esp_err_t handleWifiScanRefresh(httpd_req_t* request);
    static esp_err_t handleConfig(httpd_req_t* request);
    static esp_err_t handleCheckSntp(httpd_req_t* request);
    static esp_err_t handleSensors(httpd_req_t* request);
    static esp_err_t handleBackends(httpd_req_t* request);

    httpd_handle_t handle_ = nullptr;
    StatusService* status_service_ = nullptr;
    NetworkManager* network_manager_ = nullptr;
    ConfigRepository* config_repository_ = nullptr;
    DeviceConfig* config_ = nullptr;
    SensorConfigRepository* sensor_config_repository_ = nullptr;
    SensorConfigList* sensor_config_list_ = nullptr;
    SensorManager* sensor_manager_ = nullptr;
    MeasurementStore* measurement_store_ = nullptr;
    BackendConfigRepository* backend_config_repository_ = nullptr;
    BackendConfigList* backend_config_list_ = nullptr;
    UploadManager* upload_manager_ = nullptr;
    CellularConfigRepository* cellular_config_repository_ = nullptr;
    CellularConfig* cellular_config_ = nullptr;
    SensorConfigList staged_sensor_config_{};
    bool has_pending_sensor_changes_ = false;
};

}  // namespace air360
