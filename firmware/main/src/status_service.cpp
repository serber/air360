#include "air360/status_service.hpp"

#include <cctype>
#include <cinttypes>
#include <cstdio>
#include <ctime>
#include <vector>
#include <string>
#include <utility>

#include "air360/sensors/sensor_types.hpp"
#include "air360/web_ui.hpp"
#include "esp_timer.h"

namespace air360 {

namespace {

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

std::string formatUptimeCompact(std::uint64_t uptime_ms) {
    const std::uint64_t total_seconds = uptime_ms / 1000ULL;
    const std::uint64_t days = total_seconds / 86400ULL;
    const std::uint64_t hours = (total_seconds % 86400ULL) / 3600ULL;
    const std::uint64_t minutes = (total_seconds % 3600ULL) / 60ULL;
    const std::uint64_t seconds = total_seconds % 60ULL;

    if (days > 0U) {
        return std::to_string(days) + "d " + std::to_string(hours) + "h";
    }
    if (hours > 0U) {
        return std::to_string(hours) + "h " + std::to_string(minutes) + "m";
    }
    if (minutes > 0U) {
        return std::to_string(minutes) + "m " + std::to_string(seconds) + "s";
    }
    return std::to_string(seconds) + "s";
}

std::string formatFloat(float value, int precision) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.*f", precision, static_cast<double>(value));
    return buffer;
}

std::string formatMacForDisplay(std::string_view mac) {
    if (mac.size() != 12U) {
        return std::string(mac);
    }

    std::string formatted;
    formatted.reserve(17U);
    for (std::size_t index = 0; index < mac.size(); ++index) {
        const unsigned char ch = static_cast<unsigned char>(mac[index]);
        formatted.push_back(static_cast<char>(std::toupper(ch)));
        if ((index % 2U) == 1U && (index + 1U) < mac.size()) {
            formatted.push_back('-');
        }
    }

    return formatted;
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

std::string measurementListHtml(const SensorMeasurement& measurement) {
    if (measurement.empty()) {
        return "";
    }

    std::string html = "<ul class='list'>";
    for (std::size_t index = 0; index < measurement.value_count; ++index) {
        html += "<li>";
        html += htmlEscape(formatMeasurementValue(measurement.values[index]));
        html += "</li>";
    }
    html += "</ul>";
    return html;
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

std::string currentUtcDateTimeLabel() {
    const std::time_t now = std::time(nullptr);
    if (now <= 0) {
        return "unavailable";
    }

    std::tm utc{};
    gmtime_r(&now, &utc);

    char buffer[64];
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S UTC", &utc) == 0U) {
        return "unavailable";
    }

    return buffer;
}

const char* resetReasonLabel(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_UNKNOWN:
            return "unknown";
        case ESP_RST_POWERON:
            return "poweron";
        case ESP_RST_EXT:
            return "ext";
        case ESP_RST_SW:
            return "sw";
        case ESP_RST_PANIC:
            return "panic";
        case ESP_RST_INT_WDT:
            return "int_wdt";
        case ESP_RST_TASK_WDT:
            return "task_wdt";
        case ESP_RST_WDT:
            return "wdt";
        case ESP_RST_DEEPSLEEP:
            return "deepsleep";
        case ESP_RST_BROWNOUT:
            return "brownout";
        case ESP_RST_SDIO:
            return "sdio";
        case ESP_RST_USB:
            return "usb";
        case ESP_RST_JTAG:
            return "jtag";
        case ESP_RST_EFUSE:
            return "efuse";
        case ESP_RST_PWR_GLITCH:
            return "power_glitch";
        case ESP_RST_CPU_LOCKUP:
            return "cpu_lockup";
        default:
            return "unknown";
    }
}

struct RuntimeOverviewViewModel {
    std::string health_status_pill_html;
    std::string health_summary_html;
    std::string health_checks_html;
    std::string network_mode;
    std::string device_name;
    std::size_t sensor_count = 0U;
    std::size_t backend_count = 0U;
    std::string uptime;
    std::uint32_t boot_count = 0U;
    std::string board_name;
    std::string chip_name;
    std::string chip_revision;
    std::string chip_type;
    std::string chip_features;
    std::string crystal_frequency;
    std::string current_datetime;
    std::string reset_reason_label;
    std::string chip_id;
    std::string short_chip_id;
    std::string esp_mac_id;
    std::string ip_address;
    std::string network_error_html;
    std::size_t degraded_backend_count = 0U;
    std::string cellular_block_html;
    std::string backend_block_html;
    std::string sensor_block_html;
};

enum class HealthStatus : std::uint8_t {
    kHealthy = 0U,
    kDegraded,
    kOffline,
    kSetupRequired,
};

struct HealthCheckViewModel {
    std::string key;
    std::string label;
    bool ok = false;
    std::string summary;
    std::string pill_text;
};

struct HealthViewModel {
    HealthStatus status = HealthStatus::kSetupRequired;
    std::string summary;
    std::vector<HealthCheckViewModel> checks;
};

MeasurementRuntimeInfo measurementRuntimeForSensor(
    const MeasurementStore* measurement_store,
    std::uint32_t sensor_id);

const char* healthStatusKey(HealthStatus status) {
    switch (status) {
        case HealthStatus::kHealthy:
            return "healthy";
        case HealthStatus::kDegraded:
            return "degraded";
        case HealthStatus::kOffline:
            return "offline";
        case HealthStatus::kSetupRequired:
        default:
            return "setup_required";
    }
}

const char* healthStatusLabel(HealthStatus status) {
    switch (status) {
        case HealthStatus::kHealthy:
            return "Healthy";
        case HealthStatus::kDegraded:
            return "Degraded";
        case HealthStatus::kOffline:
            return "Offline";
        case HealthStatus::kSetupRequired:
        default:
            return "Setup Required";
    }
}

const char* healthStatusPillClass(HealthStatus status) {
    return status == HealthStatus::kHealthy ? "pill pill--ok" : "pill pill--danger";
}

std::uint64_t sensorFreshnessThresholdMs(std::uint32_t poll_interval_ms) {
    constexpr std::uint64_t kMinimumFreshnessThresholdMs = 15000ULL;
    if (poll_interval_ms == 0U) {
        return kMinimumFreshnessThresholdMs;
    }

    const std::uint64_t threshold = static_cast<std::uint64_t>(poll_interval_ms) * 3ULL;
    return threshold > kMinimumFreshnessThresholdMs ? threshold : kMinimumFreshnessThresholdMs;
}

bool sensorIsReporting(
    const SensorRuntimeInfo& sensor,
    const MeasurementRuntimeInfo& measurement_runtime,
    std::uint64_t now_uptime_ms) {
    if (!sensor.enabled) {
        return true;
    }

    switch (sensor.state) {
        case SensorRuntimeState::kConfigured:
        case SensorRuntimeState::kInitialized:
        case SensorRuntimeState::kPolling:
            break;
        case SensorRuntimeState::kDisabled:
        case SensorRuntimeState::kAbsent:
        case SensorRuntimeState::kUnsupported:
        case SensorRuntimeState::kError:
        default:
            return false;
    }

    if (measurement_runtime.last_sample_time_ms == 0U ||
        measurement_runtime.last_sample_time_ms > now_uptime_ms) {
        return false;
    }

    const std::uint64_t sample_age_ms = now_uptime_ms - measurement_runtime.last_sample_time_ms;
    return sample_age_ms <= sensorFreshnessThresholdMs(sensor.poll_interval_ms);
}

bool backendIsHealthy(const BackendStatusSnapshot& backend) {
    if (!backend.enabled) {
        return true;
    }

    if (backend.state == BackendRuntimeState::kError ||
        backend.state == BackendRuntimeState::kNotImplemented) {
        return false;
    }

    switch (backend.last_result) {
        case UploadResultClass::kConfigError:
        case UploadResultClass::kUnsupported:
        case UploadResultClass::kTransportError:
        case UploadResultClass::kHttpError:
        case UploadResultClass::kNoNetwork:
            return false;
        case UploadResultClass::kUnknown:
        case UploadResultClass::kNoData:
        case UploadResultClass::kSuccess:
        default:
            return true;
    }
}

std::string renderHealthChecksHtml(const std::vector<HealthCheckViewModel>& checks) {
    std::string html;
    for (const auto& check : checks) {
        html += "<span class='pill'>";
        html += htmlEscape(check.pill_text);
        html += "</span>";
    }
    return html;
}

HealthViewModel buildHealthViewModel(
    const NetworkState& network_state,
    const std::vector<SensorRuntimeInfo>& sensors,
    const std::vector<BackendStatusSnapshot>& backends,
    const MeasurementStore* measurement_store) {
    const std::uint64_t now_uptime_ms = uptimeMilliseconds();
    const bool setup_required =
        network_state.mode == NetworkMode::kSetupAp || !network_state.station_config_present;
    const bool uplink_available = network_state.station_connected;
    const bool time_synced = network_state.time_synchronized;

    std::size_t enabled_sensor_count = 0U;
    std::size_t failing_sensor_count = 0U;
    for (const auto& sensor : sensors) {
        if (!sensor.enabled) {
            continue;
        }

        ++enabled_sensor_count;
        const MeasurementRuntimeInfo measurement_runtime =
            measurementRuntimeForSensor(measurement_store, sensor.id);
        if (!sensorIsReporting(sensor, measurement_runtime, now_uptime_ms)) {
            ++failing_sensor_count;
        }
    }

    std::size_t enabled_backend_count = 0U;
    std::size_t failing_backend_count = 0U;
    for (const auto& backend : backends) {
        if (!backend.enabled) {
            continue;
        }

        ++enabled_backend_count;
        if (!backendIsHealthy(backend)) {
            ++failing_backend_count;
        }
    }

    HealthViewModel model;
    model.checks = {
        {
            "time_synced",
            "Time synced",
            time_synced,
            time_synced ? "Valid time available."
                        : (network_state.time_sync_error.empty() ? "Valid time not available yet."
                                                                 : network_state.time_sync_error),
            time_synced ? "time ok" : "time missing",
        },
        {
            "sensors_reporting",
            "Sensors reporting",
            failing_sensor_count == 0U,
            enabled_sensor_count == 0U
                ? "No enabled sensors."
                : (failing_sensor_count == 0U
                       ? "All enabled sensors are reporting."
                       : std::to_string(failing_sensor_count) + " sensor(s) are not reporting."),
            failing_sensor_count == 0U ? "sensors ok"
                                       : std::to_string(failing_sensor_count) + " sensor issue",
        },
        {
            "uplink_available",
            "Uplink available",
            uplink_available,
            setup_required ? "Station setup is not complete."
                           : (uplink_available ? "Station uplink connected."
                                               : "Station uplink unavailable."),
            uplink_available ? "uplink ok" : "uplink down",
        },
        {
            "backends_healthy",
            "Backends healthy",
            failing_backend_count == 0U,
            enabled_backend_count == 0U
                ? "No enabled backends."
                : (failing_backend_count == 0U
                       ? "All enabled backends look healthy."
                       : std::to_string(failing_backend_count) + " backend(s) need attention."),
            failing_backend_count == 0U ? "backends ok"
                                        : std::to_string(failing_backend_count) + " backend issue",
        },
    };

    if (setup_required) {
        model.status = HealthStatus::kSetupRequired;
        model.summary = "Device is in setup mode. Configure station Wi-Fi to enter normal operation.";
    } else if (!uplink_available || !time_synced) {
        model.status = HealthStatus::kOffline;
        if (!uplink_available && !time_synced) {
            model.summary = "Station uplink is unavailable and valid time is not ready yet.";
        } else if (!uplink_available) {
            model.summary = "Station uplink is unavailable.";
        } else {
            model.summary = "Valid time is not ready yet.";
        }
    } else if (failing_sensor_count > 0U || failing_backend_count > 0U) {
        model.status = HealthStatus::kDegraded;
        if (failing_sensor_count > 0U && failing_backend_count > 0U) {
            model.summary = std::to_string(failing_sensor_count) + " sensor(s) and " +
                            std::to_string(failing_backend_count) +
                            " backend(s) need attention.";
        } else if (failing_sensor_count > 0U) {
            model.summary =
                std::to_string(failing_sensor_count) + " sensor(s) are not reporting.";
        } else {
            model.summary =
                std::to_string(failing_backend_count) + " backend(s) need attention.";
        }
    } else {
        model.status = HealthStatus::kHealthy;
        model.summary =
            "Time is synced, enabled sensors are reporting, and enabled backends look healthy.";
    }

    return model;
}

MeasurementRuntimeInfo measurementRuntimeForSensor(
    const MeasurementStore* measurement_store,
    std::uint32_t sensor_id) {
    if (measurement_store == nullptr) {
        MeasurementRuntimeInfo info;
        info.sensor_id = sensor_id;
        return info;
    }

    return measurement_store->runtimeInfoForSensor(sensor_id);
}

std::string renderBackendOverviewBlock(
    const std::vector<BackendStatusSnapshot>& backends,
    std::uint32_t upload_interval_ms) {
    std::string html;
    if (backends.empty()) {
        return "<p class='muted'>No backends configured yet.</p>";
    }

    html += "<div class='list'>";
    for (const auto& backend : backends) {
        std::string details_block;
        details_block += "<span class='pill'>interval ";
        details_block += std::to_string(upload_interval_ms);
        details_block += " ms</span>";
        if (backend.enabled) {
            details_block += "<span class='pill'>last attempt ";
            details_block += htmlEscape(formatTimeForDisplay(
                backend.last_attempt_unix_ms,
                backend.last_attempt_uptime_ms));
            details_block += "</span>";
            details_block += "<span class='pill'>HTTP ";
            details_block += backend.last_http_status > 0 ? std::to_string(backend.last_http_status)
                                                          : std::string("n/a");
            details_block += "</span>";
            details_block += "<span class='pill'>response ";
            details_block += backend.last_response_time_ms > 0
                                 ? std::to_string(backend.last_response_time_ms) + " ms"
                                 : std::string("n/a");
            details_block += "</span>";
        }

        html += renderTemplate(
            WebTemplateKey::kOverviewBackendItem,
            WebTemplateBindings{
                {"DISPLAY_NAME", htmlEscape(backend.display_name)},
                {"STATUS_KEY", backend.enabled ? "enabled" : "disabled"},
                {"DETAILS_BLOCK", details_block},
            });
    }
    html += "</div>";
    return html;
}

std::string renderSensorOverviewBlock(
    const std::vector<SensorRuntimeInfo>& sensors,
    const MeasurementStore* measurement_store) {
    std::string html;
    if (sensors.empty()) {
        return "<p class='muted'>No sensors configured yet.</p>";
    }

    html += "<div class='list'>";
    for (const auto& sensor : sensors) {
        const MeasurementRuntimeInfo measurement_runtime =
            measurementRuntimeForSensor(measurement_store, sensor.id);
        const std::string readings_block = measurementListHtml(measurement_runtime.measurement);

        std::string last_error_block;
        if (!sensor.last_error.empty()) {
            last_error_block += "<p class='muted'>";
            last_error_block += htmlEscape(sensor.last_error);
            last_error_block += "</p>";
        }

        html += renderTemplate(
            WebTemplateKey::kOverviewSensorItem,
            WebTemplateBindings{
                {"DISPLAY_NAME", htmlEscape(sensor.type_name)},
                {"TYPE_KEY", htmlEscape(sensor.type_key)},
                {"BINDING_SUMMARY", htmlEscape(sensor.binding_summary)},
                {"STATE_KEY", htmlEscape(sensorRuntimeStateKey(sensor.state))},
                {"POLL_INTERVAL_MS", std::to_string(sensor.poll_interval_ms)},
                {"QUEUED_SAMPLE_COUNT", std::to_string(measurement_runtime.queued_sample_count)},
                {"READINGS_BLOCK", readings_block},
                {"LAST_ERROR_BLOCK", last_error_block},
            });
    }
    html += "</div>";
    return html;
}

std::string renderCellularOverviewBlock(const CellularState& state) {
    if (!state.enabled) {
        return "<p class='muted'>Cellular uplink is disabled.</p>";
    }

    std::string details_block;
    if (state.ppp_connected) {
        if (!state.ip_address.empty()) {
            details_block += "<span class='pill'>IP ";
            details_block += htmlEscape(state.ip_address);
            details_block += "</span>";
        }
        if (!state.connectivity_check_skipped) {
            details_block += "<span class='pill'>connectivity ";
            details_block += state.connectivity_ok ? "ok" : "failed";
            details_block += "</span>";
        }
    }
    if (!state.last_error.empty()) {
        details_block += "<span class='pill'>error: ";
        details_block += htmlEscape(state.last_error);
        details_block += "</span>";
    }

    std::string html;
    html += "<div class='list'>";
    html += "<article class='list-card list-card--backend'>";
    html += "<div class='list-card__title'>SIM7600E</div>";
    html += "<div class='meta'>";
    html += "<span class='pill'>";
    html += state.ppp_connected ? "connected" : "disconnected";
    html += "</span>";
    html += details_block;
    html += "</div>";
    html += "</article>";
    html += "</div>";
    return html;
}

RuntimeOverviewViewModel buildRuntimeOverviewViewModel(
    const BuildInfo& build_info,
    const DeviceConfig& config,
    const NetworkState& network_state,
    const CellularState& cellular_state,
    std::uint32_t boot_count,
    esp_reset_reason_t reset_reason,
    bool config_loaded_from_storage,
    const std::vector<SensorRuntimeInfo>& sensors,
    const std::vector<BackendStatusSnapshot>& backends,
    const MeasurementStore* measurement_store,
    const UploadManager* upload_manager) {
    RuntimeOverviewViewModel model;
    const HealthViewModel health =
        buildHealthViewModel(network_state, sensors, backends, measurement_store);
    model.health_status_pill_html = "<span class='";
    model.health_status_pill_html += healthStatusPillClass(health.status);
    model.health_status_pill_html += "'>";
    model.health_status_pill_html += htmlEscape(healthStatusLabel(health.status));
    model.health_status_pill_html += "</span>";
    if (!health.summary.empty() && health.status != HealthStatus::kHealthy) {
        model.health_summary_html = "<p class='muted'>";
        model.health_summary_html += htmlEscape(health.summary);
        model.health_summary_html += "</p>";
    }
    model.health_checks_html = renderHealthChecksHtml(health.checks);
    model.network_mode = networkModeString(network_state.mode);
    model.device_name = config.device_name;
    model.sensor_count = sensors.size();
    model.backend_count = upload_manager != nullptr ? upload_manager->enabledCount() : 0U;
    model.uptime = formatUptimeCompact(uptimeMilliseconds());
    model.boot_count = boot_count;
    model.board_name = build_info.board_name;
    model.chip_name = build_info.chip_name;
    model.chip_revision = build_info.chip_revision;
    model.chip_type = build_info.chip_type.empty() ? "unavailable" : build_info.chip_type;
    model.chip_features =
        build_info.chip_features.empty() ? "unavailable" : build_info.chip_features;
    model.crystal_frequency =
        build_info.crystal_frequency.empty() ? "unavailable" : build_info.crystal_frequency;
    model.current_datetime = currentUtcDateTimeLabel();
    model.reset_reason_label = resetReasonLabel(reset_reason);
    model.chip_id = build_info.chip_id.empty() ? "unavailable" : build_info.chip_id;
    model.short_chip_id =
        build_info.short_chip_id.empty() ? "unavailable" : build_info.short_chip_id;
    model.esp_mac_id =
        build_info.esp_mac_id.empty() ? "unavailable" : formatMacForDisplay(build_info.esp_mac_id);
    model.ip_address = network_state.ip_address.empty() ? "unavailable" : network_state.ip_address;
    model.degraded_backend_count = upload_manager != nullptr ? upload_manager->degradedCount() : 0U;
    model.cellular_block_html = renderCellularOverviewBlock(cellular_state);
    model.backend_block_html = renderBackendOverviewBlock(
        backends,
        upload_manager != nullptr ? upload_manager->uploadIntervalMs() : 0U);
    model.sensor_block_html = renderSensorOverviewBlock(sensors, measurement_store);

    if (!network_state.last_error.empty()) {
        model.network_error_html += " · Last network error: <code>";
        model.network_error_html += htmlEscape(network_state.last_error);
        model.network_error_html += "</code>";
    }

    return model;
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

void StatusService::setCellularState(const CellularState& state) {
    cellular_state_ = state;
}

void StatusService::setSensors(const SensorManager& sensor_manager) {
    sensor_manager_ = &sensor_manager;
}

void StatusService::setMeasurements(const MeasurementStore& measurement_store) {
    measurement_store_ = &measurement_store;
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
    const RuntimeOverviewViewModel model = buildRuntimeOverviewViewModel(
        build_info_,
        config_,
        network_state_,
        cellular_state_,
        boot_count_,
        reset_reason_,
        config_loaded_from_storage_,
        sensors,
        backends,
        measurement_store_,
        upload_manager_);

    const std::string body = renderPageTemplate(
        WebTemplateKey::kHome,
        WebTemplateBindings{
            {"HEALTH_STATUS_PILL", model.health_status_pill_html},
            {"HEALTH_SUMMARY_BLOCK", model.health_summary_html},
            {"HEALTH_CHECKS", model.health_checks_html},
            {"NETWORK_MODE", htmlEscape(model.network_mode)},
            {"DEVICE_NAME", htmlEscape(model.device_name)},
            {"SENSOR_COUNT", std::to_string(model.sensor_count)},
            {"BACKEND_COUNT", std::to_string(model.backend_count)},
            {"UPTIME", htmlEscape(model.uptime)},
            {"BOOT_COUNT", std::to_string(model.boot_count)},
            {"BOARD_NAME", htmlEscape(model.board_name)},
            {"CHIP_NAME", htmlEscape(model.chip_name)},
            {"CHIP_REVISION", htmlEscape(model.chip_revision)},
            {"CHIP_TYPE", htmlEscape(model.chip_type)},
            {"CHIP_FEATURES", htmlEscape(model.chip_features)},
            {"CRYSTAL_FREQUENCY", htmlEscape(model.crystal_frequency)},
            {"CURRENT_DATETIME", htmlEscape(model.current_datetime)},
            {"RESET_REASON_LABEL", htmlEscape(model.reset_reason_label)},
            {"CHIP_ID", htmlEscape(model.chip_id)},
            {"SHORT_CHIP_ID", htmlEscape(model.short_chip_id)},
            {"ESP_MAC_ID", htmlEscape(model.esp_mac_id)},
            {"IP_ADDRESS", htmlEscape(model.ip_address)},
            {"NETWORK_ERROR", model.network_error_html},
            {"CELLULAR_BLOCK", model.cellular_block_html},
            {"DEGRADED_BACKEND_COUNT", std::to_string(model.degraded_backend_count)},
            {"BACKEND_BLOCK", model.backend_block_html},
            {"SENSOR_BLOCK", model.sensor_block_html},
        });

    return renderPageDocument(
        WebPageKey::kHome,
        "Air 360 runtime overview",
        "Runtime Overview",
        "Live device overview, sensor state, upload state, and runtime metadata.",
        body,
        true);
}

std::string StatusService::renderStatusJson() const {
    const std::vector<SensorRuntimeInfo> sensors =
        sensor_manager_ != nullptr ? sensor_manager_->sensors() : std::vector<SensorRuntimeInfo>{};
    const std::vector<BackendStatusSnapshot> backends =
        upload_manager_ != nullptr ? upload_manager_->backends()
                                   : std::vector<BackendStatusSnapshot>{};
    const HealthViewModel health =
        buildHealthViewModel(network_state_, sensors, backends, measurement_store_);

    std::string json;
    json.reserve(8192);
    json += "{";
    json += "\"project_name\":\"" + jsonEscape(build_info_.project_name) + "\",";
    json += "\"project_version\":\"" + jsonEscape(build_info_.project_version) + "\",";
    json += "\"idf_version\":\"" + jsonEscape(build_info_.idf_version) + "\",";
    json += "\"board_name\":\"" + jsonEscape(build_info_.board_name) + "\",";
    json += "\"chip_name\":\"" + jsonEscape(build_info_.chip_name) + "\",";
    json += "\"chip_revision\":\"" + jsonEscape(build_info_.chip_revision) + "\",";
    json += "\"chip_type\":\"" + jsonEscape(build_info_.chip_type) + "\",";
    json += "\"chip_features\":\"" + jsonEscape(build_info_.chip_features) + "\",";
    json += "\"crystal_frequency\":\"" + jsonEscape(build_info_.crystal_frequency) + "\",";
    json += "\"current_datetime\":\"" + jsonEscape(currentUtcDateTimeLabel()) + "\",";
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
    json += "\"reset_reason_label\":\"" + jsonEscape(resetReasonLabel(reset_reason_)) + "\",";
    json += "\"health_status\":\"" + jsonEscape(healthStatusKey(health.status)) + "\",";
    json += "\"health_summary\":\"" + jsonEscape(health.summary) + "\",";
    json += "\"health_checks\":{";
    for (std::size_t index = 0; index < health.checks.size(); ++index) {
        const auto& check = health.checks[index];
        if (index > 0U) {
            json += ",";
        }
        json += "\"";
        json += jsonEscape(check.key);
        json += "\":{";
        json += "\"state\":\"";
        json += check.ok ? "ok" : "attention";
        json += "\",\"summary\":\"";
        json += jsonEscape(check.summary);
        json += "\"}";
    }
    json += "},";
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
    json += ",\"time_sync_attempted\":";
    json += boolString(network_state_.time_sync_attempted);
    json += ",\"time_synchronized\":";
    json += boolString(network_state_.time_synchronized);
    json += ",\"last_time_sync_unix_ms\":";
    json += std::to_string(network_state_.last_time_sync_unix_ms);
    json += ",\"active_station_ssid\":\"" + jsonEscape(network_state_.station_ssid) + "\",";
    json += "\"active_setup_ap_ssid\":\"" + jsonEscape(network_state_.lab_ap_ssid) + "\",";
    json += "\"lab_ap_ip\":\"" + jsonEscape(network_state_.ip_address) + "\",";
    json += "\"last_error\":\"" + jsonEscape(network_state_.last_error) + "\",";
    json += "\"time_sync_error\":\"" + jsonEscape(network_state_.time_sync_error) + "\",";
    json += "\"cellular\":{";
    json += "\"enabled\":";
    json += boolString(cellular_state_.enabled);
    json += ",\"ppp_connected\":";
    json += boolString(cellular_state_.ppp_connected);
    json += ",\"ip_address\":\"" + jsonEscape(cellular_state_.ip_address) + "\",";
    json += "\"connectivity_ok\":";
    json += boolString(cellular_state_.connectivity_ok);
    json += ",\"connectivity_check_skipped\":";
    json += boolString(cellular_state_.connectivity_check_skipped);
    json += ",\"last_error\":\"" + jsonEscape(cellular_state_.last_error) + "\"";
    json += "},";
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
        const MeasurementRuntimeInfo measurement_runtime =
            measurementRuntimeForSensor(measurement_store_, sensor.id);
        if (index > 0U) {
            json += ",";
        }
        json += "{";
        json += "\"id\":" + std::to_string(sensor.id) + ",";
        json += "\"enabled\":";
        json += boolString(sensor.enabled);
        json += ",\"sensor_type\":\"" + jsonEscape(sensor.type_key) + "\",";
        json += "\"sensor_name\":\"" + jsonEscape(sensor.type_name) + "\",";
        json += "\"transport_kind\":\"" + jsonEscape(transportKindKey(sensor.transport_kind)) + "\",";
        json += "\"binding\":\"" + jsonEscape(sensor.binding_summary) + "\",";
        json += "\"poll_interval_ms\":" + std::to_string(sensor.poll_interval_ms) + ",";
        json += "\"status\":\"" + jsonEscape(sensorRuntimeStateKey(sensor.state)) + "\",";
        json += "\"last_sample_time_ms\":" +
                std::to_string(measurement_runtime.last_sample_time_ms) + ",";
        json += "\"queued_sample_count\":" +
                std::to_string(measurement_runtime.queued_sample_count) + ",";
        json += "\"measurements\":";
        json += measurementArrayJson(measurement_runtime.measurement);
        json += ",";
        json += "\"temperature_c\":";
        json += jsonNumberOrNull(measurement_runtime.measurement, SensorValueKind::kTemperatureC);
        json += ",\"humidity_percent\":";
        json += jsonNumberOrNull(
            measurement_runtime.measurement,
            SensorValueKind::kHumidityPercent);
        json += ",\"pressure_hpa\":";
        json += jsonNumberOrNull(measurement_runtime.measurement, SensorValueKind::kPressureHpa);
        json += ",\"gas_resistance_ohms\":";
        json += jsonNumberOrNull(
            measurement_runtime.measurement,
            SensorValueKind::kGasResistanceOhms);
        json += ",\"illuminance_lux\":";
        json += jsonNumberOrNull(
            measurement_runtime.measurement,
            SensorValueKind::kIlluminanceLux);
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

const BuildInfo& StatusService::buildInfo() const {
    return build_info_;
}

}  // namespace air360
