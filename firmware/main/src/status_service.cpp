#include "air360/status_service.hpp"

#include <cinttypes>
#include <cstdio>
#include <ctime>
#include <string>
#include <utility>

#include "air360/sensors/sensor_types.hpp"
#include "esp_timer.h"

namespace air360 {

namespace {

std::string htmlEscape(const std::string& input) {
    std::string escaped;
    escaped.reserve(input.size());

    for (const char ch : input) {
        switch (ch) {
            case '&':
                escaped += "&amp;";
                break;
            case '<':
                escaped += "&lt;";
                break;
            case '>':
                escaped += "&gt;";
                break;
            case '"':
                escaped += "&quot;";
                break;
            default:
                escaped.push_back(ch);
                break;
        }
    }

    return escaped;
}

std::string jsonEscape(const std::string& input) {
    std::string escaped;
    escaped.reserve(input.size());

    for (const char ch : input) {
        switch (ch) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped.push_back(ch);
                break;
        }
    }

    return escaped;
}

const char* boolString(bool value) {
    return value ? "true" : "false";
}

const char* networkModeString(NetworkMode mode) {
    switch (mode) {
        case NetworkMode::kSetupAp:
            return "setup_ap";
        case NetworkMode::kStation:
            return "station";
        case NetworkMode::kOffline:
        default:
            return "offline";
    }
}

std::uint64_t uptimeMilliseconds() {
    return static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
}

std::string formatFloat(float value, int precision) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.*f", precision, static_cast<double>(value));
    return buffer;
}

std::string formatMeasurementValue(const SensorValue& value) {
    std::string text = sensorValueKindLabel(value.kind);
    text += " ";
    text += formatFloat(value.value, sensorValueKindPrecision(value.kind));
    const char* unit = sensorValueKindUnit(value.kind);
    if (unit[0] != '\0') {
        text += " ";
        text += unit;
    }
    return text;
}

std::string measurementSummary(const SensorMeasurement& measurement) {
    std::string summary;
    for (std::size_t index = 0; index < measurement.value_count; ++index) {
        if (!summary.empty()) {
            summary += " · ";
        }
        summary += formatMeasurementValue(measurement.values[index]);
    }
    return summary;
}

std::string jsonNumberOrNull(const SensorMeasurement& measurement, SensorValueKind kind) {
    const SensorValue* value = measurement.findValue(kind);
    if (value == nullptr) {
        return "null";
    }

    return formatFloat(value->value, sensorValueKindPrecision(kind));
}

std::string measurementArrayJson(const SensorMeasurement& measurement) {
    std::string json = "[";
    for (std::size_t index = 0; index < measurement.value_count; ++index) {
        if (index > 0U) {
            json += ",";
        }
        const SensorValue& value = measurement.values[index];
        json += "{";
        json += "\"kind\":\"";
        json += jsonEscape(sensorValueKindKey(value.kind));
        json += "\",\"label\":\"";
        json += jsonEscape(sensorValueKindLabel(value.kind));
        json += "\",\"unit\":\"";
        json += jsonEscape(sensorValueKindUnit(value.kind));
        json += "\",\"value\":";
        json += formatFloat(value.value, sensorValueKindPrecision(value.kind));
        json += "}";
    }
    json += "]";
    return json;
}

std::string formatTimeForDisplay(std::int64_t unix_ms, std::uint64_t uptime_ms) {
    if (unix_ms > 0) {
        const std::time_t seconds = static_cast<std::time_t>(unix_ms / 1000LL);
        std::tm utc{};
        gmtime_r(&seconds, &utc);

        char buffer[64];
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S UTC", &utc);
        return buffer;
    }

    if (uptime_ms > 0U) {
        return std::to_string(uptime_ms) + " ms uptime";
    }

    return "never";
}

}  // namespace

StatusService::StatusService(BuildInfo build_info) : build_info_(std::move(build_info)) {}

void StatusService::markNvsReady(bool ready) {
    nvs_ready_ = ready;
}

void StatusService::markWatchdogArmed(bool armed) {
    watchdog_armed_ = armed;
}

void StatusService::setConfig(
    const DeviceConfig& config,
    bool loaded_from_storage,
    bool wrote_defaults) {
    config_ = config;
    config_loaded_from_storage_ = loaded_from_storage;
    wrote_default_config_ = wrote_defaults;
}

void StatusService::setBootCount(std::uint32_t boot_count) {
    boot_count_ = boot_count;
}

void StatusService::setNetworkState(const NetworkState& state) {
    network_state_ = state;
}

void StatusService::setSensors(const SensorManager& sensor_manager) {
    sensor_manager_ = &sensor_manager;
}

void StatusService::setUploads(const UploadManager& upload_manager) {
    upload_manager_ = &upload_manager;
}

void StatusService::setWebServerStarted(bool started) {
    web_server_started_ = started;
}

std::string StatusService::renderRootHtml() const {
    const std::vector<SensorRuntimeInfo> sensors =
        sensor_manager_ != nullptr ? sensor_manager_->sensors() : std::vector<SensorRuntimeInfo>{};
    const std::vector<BackendStatusSnapshot> backends =
        upload_manager_ != nullptr ? upload_manager_->backends()
                                   : std::vector<BackendStatusSnapshot>{};

    std::string html;
    html.reserve(5500);
    html += "<!doctype html><html><head><meta charset='utf-8'>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>air360 runtime</title>";
    html += "<style>body{font-family:system-ui,sans-serif;margin:2rem;max-width:52rem;line-height:1.5}";
    html += "code{background:#f3f4f6;padding:.1rem .35rem;border-radius:.25rem}";
    html += "pre{background:#111827;color:#f9fafb;padding:1rem;border-radius:.5rem;overflow:auto}";
    html += "label{display:block;margin-top:1rem;font-weight:600}";
    html += "input{width:100%;max-width:28rem;padding:.55rem;border:1px solid #d1d5db;border-radius:.35rem}";
    html += "a{color:#0f766e}</style></head><body>";
    html += "<h1>air360 Runtime</h1>";
    html += "<p>Board: <code>" + htmlEscape(build_info_.board_name) + "</code></p>";
    html += "<p>Chip: <code>" + htmlEscape(build_info_.chip_name) + "</code> · revision <code>";
    html += htmlEscape(build_info_.chip_revision);
    html += "</code></p>";
    html += "<p>Device: <code>" + htmlEscape(config_.device_name) + "</code></p>";
    html += "<p>Chip ID: <code>";
    html += htmlEscape(build_info_.chip_id.empty() ? "unavailable" : build_info_.chip_id);
    html += "</code></p>";
    html += "<p>Short Chip ID: <code>";
    html += htmlEscape(
        build_info_.short_chip_id.empty() ? "unavailable" : build_info_.short_chip_id);
    html += "</code></p>";
    html += "<p>MAC ID: <code>";
    html += htmlEscape(build_info_.esp_mac_id.empty() ? "unavailable" : build_info_.esp_mac_id);
    html += "</code></p>";
    html += "<p>Mode: <code>";
    html += networkModeString(network_state_.mode);
    html += "</code></p>";
    html += "<p>Local UI: <a href='/config'>/config</a> · Sensors: <a href='/sensors'>/sensors</a> · Backends: <a href='/backends'>/backends</a> · JSON status: <a href='/status'>/status</a></p>";
    html += "<h2>Backends</h2>";
    html += "<p>Enabled backends: <code>";
    html += std::to_string(upload_manager_ != nullptr ? upload_manager_->enabledCount() : 0U);
    html += "</code> · degraded: <code>";
    html += std::to_string(upload_manager_ != nullptr ? upload_manager_->degradedCount() : 0U);
    html += "</code> · interval <code>";
    html += std::to_string(upload_manager_ != nullptr ? upload_manager_->uploadIntervalMs() : 0U);
    html += " ms</code></p>";
    if (backends.empty()) {
        html += "<p>No backends configured yet.</p>";
    } else {
        html += "<ul>";
        for (const auto& backend : backends) {
            html += "<li><strong>";
            html += htmlEscape(backend.display_name);
            html += "</strong> · <code>";
            html += htmlEscape(backendRuntimeStateKey(backend.state));
            html += "</code> · result <code>";
            html += htmlEscape(uploadResultClassKey(backend.last_result));
            html += "</code> · last attempt <code>";
            html += htmlEscape(
                formatTimeForDisplay(
                    backend.last_attempt_unix_ms,
                    backend.last_attempt_uptime_ms));
            html += "</code>";
            if (backend.last_http_status > 0) {
                html += " · HTTP <code>";
                html += std::to_string(backend.last_http_status);
                html += "</code>";
            }
            if (backend.last_response_time_ms > 0U) {
                html += " · ";
                html += std::to_string(backend.last_response_time_ms);
                html += " ms";
            }
            if (!backend.last_error.empty()) {
                html += " · ";
                html += htmlEscape(backend.last_error);
            }
            html += "</li>";
        }
        html += "</ul>";
    }
    html += "<h2>Sensors</h2>";
    html += "<p>Configured sensors: <code>";
    html += std::to_string(sensors.size());
    html += "</code></p>";
    if (sensors.empty()) {
        html += "<p>No sensors configured yet.</p>";
    } else {
        html += "<ul>";
        for (const auto& sensor : sensors) {
            html += "<li><strong>";
            html += htmlEscape(sensor.display_name);
            html += "</strong> · <code>";
            html += htmlEscape(sensor.type_key);
            html += "</code> · <code>";
            html += htmlEscape(sensor.binding_summary);
            html += "</code> · state <code>";
            html += htmlEscape(sensorRuntimeStateKey(sensor.state));
            html += "</code>";
            if (!sensor.last_error.empty()) {
                html += " · ";
                html += htmlEscape(sensor.last_error);
            }
            const std::string readings = measurementSummary(sensor.measurement);
            if (!readings.empty()) {
                html += " · ";
                html += htmlEscape(readings);
            }
            html += "</li>";
        }
        html += "</ul>";
    }
    html += "<pre>";
    html += "project: " + htmlEscape(build_info_.project_name) + "\n";
    html += "version: " + htmlEscape(build_info_.project_version) + "\n";
    html += "idf: " + htmlEscape(build_info_.idf_version) + "\n";
    html += "chip_name: " + htmlEscape(build_info_.chip_name) + "\n";
    html += "chip_revision: " + htmlEscape(build_info_.chip_revision) + "\n";
    html += "chip_id: " + htmlEscape(build_info_.chip_id) + "\n";
    html += "short_chip_id: " + htmlEscape(build_info_.short_chip_id) + "\n";
    html += "esp_mac_id: " + htmlEscape(build_info_.esp_mac_id) + "\n";
    html += "boot_count: " + std::to_string(boot_count_) + "\n";
    html += "config_source: ";
    html += (config_loaded_from_storage_ ? "stored" : "defaults");
    html += "\nlab_ap_active: ";
    html += (network_state_.lab_ap_active ? "true" : "false");
    html += "\nstation_ssid: ";
    html += htmlEscape(network_state_.station_ssid);
    html += "\nstation_connected: ";
    html += (network_state_.station_connected ? "true" : "false");
    html += "\nsetup_ap_ssid: ";
    html += htmlEscape(network_state_.lab_ap_ssid);
    html += "\nlab_ap_ip: ";
    html += htmlEscape(network_state_.ip_address);
    html += "\nlast_error: ";
    html += htmlEscape(network_state_.last_error);
    html += "\nconfig_http_port: ";
    html += std::to_string(config_.http_port);
    html += "\nwifi_configured: ";
    html += (config_.wifi_sta_ssid[0] != '\0' ? "true" : "false");
    html += "\nconfigured_sensors: ";
    html += std::to_string(sensors.size());
    html += "\nenabled_backends: ";
    html += std::to_string(upload_manager_ != nullptr ? upload_manager_->enabledCount() : 0U);
    html += "\ndegraded_backends: ";
    html += std::to_string(upload_manager_ != nullptr ? upload_manager_->degradedCount() : 0U);
    html += "\nupload_interval_ms: ";
    html += std::to_string(upload_manager_ != nullptr ? upload_manager_->uploadIntervalMs() : 0U);
    html += "\n";
    html += "</pre></body></html>";
    return html;
}

std::string StatusService::renderStatusJson() const {
    const std::vector<SensorRuntimeInfo> sensors =
        sensor_manager_ != nullptr ? sensor_manager_->sensors() : std::vector<SensorRuntimeInfo>{};
    const std::vector<BackendStatusSnapshot> backends =
        upload_manager_ != nullptr ? upload_manager_->backends()
                                   : std::vector<BackendStatusSnapshot>{};

    std::string json;
    json.reserve(8192);
    json += "{";
    json += "\"project_name\":\"" + jsonEscape(build_info_.project_name) + "\",";
    json += "\"project_version\":\"" + jsonEscape(build_info_.project_version) + "\",";
    json += "\"idf_version\":\"" + jsonEscape(build_info_.idf_version) + "\",";
    json += "\"board_name\":\"" + jsonEscape(build_info_.board_name) + "\",";
    json += "\"chip_name\":\"" + jsonEscape(build_info_.chip_name) + "\",";
    json += "\"chip_revision\":\"" + jsonEscape(build_info_.chip_revision) + "\",";
    json += "\"compile_date\":\"" + jsonEscape(build_info_.compile_date) + "\",";
    json += "\"compile_time\":\"" + jsonEscape(build_info_.compile_time) + "\",";
    json += "\"chip_id\":\"" + jsonEscape(build_info_.chip_id) + "\",";
    json += "\"short_chip_id\":\"" + jsonEscape(build_info_.short_chip_id) + "\",";
    json += "\"esp_mac_id\":\"" + jsonEscape(build_info_.esp_mac_id) + "\",";
    json += "\"device_name\":\"" + jsonEscape(config_.device_name) + "\",";
    json += "\"wifi_station_ssid\":\"" + jsonEscape(config_.wifi_sta_ssid) + "\",";
    json += "\"setup_ap_ssid\":\"" + jsonEscape(config_.lab_ap_ssid) + "\",";
    json += "\"boot_count\":" + std::to_string(boot_count_) + ",";
    json += "\"uptime_ms\":" + std::to_string(uptimeMilliseconds()) + ",";
    json += "\"reset_reason\":" + std::to_string(static_cast<int>(reset_reason_)) + ",";
    json += "\"nvs_ready\":";
    json += boolString(nvs_ready_);
    json += ",\"watchdog_armed\":";
    json += boolString(watchdog_armed_);
    json += ",\"config_loaded_from_storage\":";
    json += boolString(config_loaded_from_storage_);
    json += ",\"wrote_default_config\":";
    json += boolString(wrote_default_config_);
    json += ",\"web_server_started\":";
    json += boolString(web_server_started_);
    json += ",\"http_port\":" + std::to_string(config_.http_port) + ",";
    json += "\"lab_ap_enabled\":";
    json += boolString(config_.lab_ap_enabled != 0U);
    json += ",\"local_auth_enabled\":";
    json += boolString(config_.local_auth_enabled != 0U);
    json += ",\"network_mode\":\"";
    json += networkModeString(network_state_.mode);
    json += "\",\"station_config_present\":";
    json += boolString(network_state_.station_config_present);
    json += ",\"station_connect_attempted\":";
    json += boolString(network_state_.station_connect_attempted);
    json += ",\"station_connected\":";
    json += boolString(network_state_.station_connected);
    json += ",\"lab_ap_active\":";
    json += boolString(network_state_.lab_ap_active);
    json += ",\"active_station_ssid\":\"" + jsonEscape(network_state_.station_ssid) + "\",";
    json += "\"active_setup_ap_ssid\":\"" + jsonEscape(network_state_.lab_ap_ssid) + "\",";
    json += "\"lab_ap_ip\":\"" + jsonEscape(network_state_.ip_address) + "\",";
    json += "\"last_error\":\"" + jsonEscape(network_state_.last_error) + "\",";
    json += "\"configured_sensors_count\":" + std::to_string(sensors.size()) + ",";
    json += "\"enabled_backends_count\":";
    json += std::to_string(upload_manager_ != nullptr ? upload_manager_->enabledCount() : 0U);
    json += ",\"degraded_backends_count\":";
    json += std::to_string(upload_manager_ != nullptr ? upload_manager_->degradedCount() : 0U);
    json += ",\"upload_interval_ms\":";
    json += std::to_string(upload_manager_ != nullptr ? upload_manager_->uploadIntervalMs() : 0U);
    json += ",\"last_upload_attempt_uptime_ms\":";
    json += std::to_string(
        upload_manager_ != nullptr ? upload_manager_->lastOverallAttemptUptimeMs() : 0U);
    json += ",\"last_upload_attempt_unix_ms\":";
    json += std::to_string(
        upload_manager_ != nullptr ? upload_manager_->lastOverallAttemptUnixMs() : 0);
    json += ",\"backends\":[";
    for (std::size_t index = 0; index < backends.size(); ++index) {
        const auto& backend = backends[index];
        if (index > 0U) {
            json += ",";
        }
        json += "{";
        json += "\"id\":" + std::to_string(backend.id) + ",";
        json += "\"backend_key\":\"" + jsonEscape(backend.backend_key) + "\",";
        json += "\"display_name\":\"" + jsonEscape(backend.display_name) + "\",";
        json += "\"enabled\":";
        json += boolString(backend.enabled);
        json += ",\"configured\":";
        json += boolString(backend.configured);
        json += ",\"implemented\":";
        json += boolString(backend.implemented);
        json += ",\"state\":\"";
        json += jsonEscape(backendRuntimeStateKey(backend.state));
        json += "\",\"last_result\":\"";
        json += jsonEscape(uploadResultClassKey(backend.last_result));
        json += "\",\"last_attempt_uptime_ms\":";
        json += std::to_string(backend.last_attempt_uptime_ms);
        json += ",\"last_success_uptime_ms\":";
        json += std::to_string(backend.last_success_uptime_ms);
        json += ",\"last_attempt_unix_ms\":";
        json += std::to_string(backend.last_attempt_unix_ms);
        json += ",\"last_success_unix_ms\":";
        json += std::to_string(backend.last_success_unix_ms);
        json += ",\"last_http_status\":";
        json += std::to_string(backend.last_http_status);
        json += ",\"last_response_time_ms\":";
        json += std::to_string(backend.last_response_time_ms);
        json += ",\"retry_count\":";
        json += std::to_string(backend.retry_count);
        json += ",\"next_retry_uptime_ms\":";
        json += std::to_string(backend.next_retry_uptime_ms);
        json += ",\"last_error\":\"";
        json += jsonEscape(backend.last_error);
        json += "\"}";
    }
    json += "],";
    json += "\"sensors\":[";
    for (std::size_t index = 0; index < sensors.size(); ++index) {
        const auto& sensor = sensors[index];
        if (index > 0U) {
            json += ",";
        }
        json += "{";
        json += "\"id\":" + std::to_string(sensor.id) + ",";
        json += "\"enabled\":";
        json += boolString(sensor.enabled);
        json += ",\"sensor_type\":\"" + jsonEscape(sensor.type_key) + "\",";
        json += "\"sensor_name\":\"" + jsonEscape(sensor.type_name) + "\",";
        json += "\"display_name\":\"" + jsonEscape(sensor.display_name) + "\",";
        json += "\"transport_kind\":\"" + jsonEscape(transportKindKey(sensor.transport_kind)) + "\",";
        json += "\"binding\":\"" + jsonEscape(sensor.binding_summary) + "\",";
        json += "\"status\":\"" + jsonEscape(sensorRuntimeStateKey(sensor.state)) + "\",";
        json += "\"last_sample_time_ms\":" + std::to_string(sensor.last_sample_time_ms) + ",";
        json += "\"measurements\":";
        json += measurementArrayJson(sensor.measurement);
        json += ",";
        json += "\"temperature_c\":";
        json += jsonNumberOrNull(sensor.measurement, SensorValueKind::kTemperatureC);
        json += ",\"humidity_percent\":";
        json += jsonNumberOrNull(sensor.measurement, SensorValueKind::kHumidityPercent);
        json += ",\"pressure_hpa\":";
        json += jsonNumberOrNull(sensor.measurement, SensorValueKind::kPressureHpa);
        json += ",\"gas_resistance_ohms\":";
        json += jsonNumberOrNull(sensor.measurement, SensorValueKind::kGasResistanceOhms);
        json += ",";
        json += "\"last_error\":\"" + jsonEscape(sensor.last_error) + "\"";
        json += "}";
    }
    json += "]}";
    return json;
}

const NetworkState& StatusService::networkState() const {
    return network_state_;
}

}  // namespace air360
