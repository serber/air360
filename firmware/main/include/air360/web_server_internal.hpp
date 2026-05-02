#pragma once

#include <cstdint>
#include <string>

#include "air360/build_info.hpp"
#include "air360/cellular_config_repository.hpp"
#include "air360/config_repository.hpp"
#include "air360/network_manager.hpp"
#include "air360/sensors/sensor_config_repository.hpp"
#include "air360/sensors/sensor_manager.hpp"
#include "air360/sensors/sensor_registry.hpp"
#include "air360/uploads/backend_config.hpp"
#include "air360/uploads/measurement_store.hpp"
#include "air360/uploads/upload_manager.hpp"
#include "air360/web_form.hpp"
#include "esp_err.h"
#include "esp_http_server.h"

namespace air360::web {

inline constexpr std::uint32_t kMinSensorPollIntervalMs = air360::kMinSensorPollIntervalMs;
inline constexpr std::uint32_t kMaxSensorPollIntervalMs = air360::kMaxSensorPollIntervalMs;
// Shared with web_server.cpp so that logHttpHandlerWatermark() can compare
// the FreeRTOS high-water mark against the configured httpd task stack.
inline constexpr std::size_t kHttpdStackBytes = 16384U;

esp_err_t readRequestBody(httpd_req_t* request, std::string& out_body);
esp_err_t sendRequestBodyTooLarge(httpd_req_t* request);
esp_err_t sendHtmlResponse(httpd_req_t* request, const std::string& html);
// Logs the FreeRTOS httpd task stack high-water mark at WARNING when usage
// exceeds 50 %, 70 %, or 90 % of kHttpdStackBytes. Call once per handler
// entry to catch accumulated worst-case usage across prior requests.
void logHttpHandlerWatermark();

bool parseUnsignedLong(const std::string& input, unsigned long& value, int base = 10);
bool parseSignedLong(const std::string& input, long& value);
bool parseI2cAddress(const std::string& input, std::uint8_t& value);
TransportKind inferredTransportKind(const SensorDescriptor& descriptor);
bool validateSensorCategorySelection(
    const SensorConfigList& sensor_config_list,
    const SensorRecord& record,
    std::string& error);
bool validateConfigForm(
    const std::string& device_name,
    const std::string& wifi_ssid,
    const std::string& wifi_password,
    const std::string& sntp_server,
    bool sta_use_static_ip,
    const std::string& sta_ip,
    const std::string& sta_netmask,
    const std::string& sta_gateway,
    const std::string& sta_dns,
    bool cellular_enabled,
    const std::string& cellular_apn,
    const std::string& cellular_username,
    const std::string& cellular_password,
    const std::string& cellular_sim_pin,
    const std::string& cellular_connectivity_check_host,
    unsigned long cellular_wifi_debug_window_s,
    std::string& error);

std::string renderConfigPage(
    const DeviceConfig& config,
    const CellularConfig& cellular_config,
    const NetworkState& network_state,
    const NetworkManager& network_manager,
    const std::string& notice,
    bool error_notice);
std::string renderBackendsPage(
    const BackendConfigList& backend_config_list,
    const UploadManager& upload_manager,
    const BuildInfo& build_info,
    const std::string& air360_upload_secret_preview,
    const std::string& notice,
    bool error_notice);
std::string renderSensorsPage(
    const SensorConfigList& sensor_config_list,
    const SensorManager& sensor_manager,
    const MeasurementStore& measurement_store,
    bool has_pending_changes,
    const std::string& notice,
    bool error_notice);

}  // namespace air360::web
