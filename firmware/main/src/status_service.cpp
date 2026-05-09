#include "air360/status_service.hpp"

#include <algorithm>
#include <cctype>
#include <cinttypes>
#include <cstdio>
#include <ctime>
#include <vector>
#include <string>
#include <utility>

#include "air360/sensors/sensor_driver.hpp"
#include "air360/sensors/sensor_types.hpp"
#include "air360/string_utils.hpp"
#include "air360/tuning.hpp"
#include "air360/web_ui.hpp"
#include "esp_heap_caps.h"
#include "esp_timer.h"

namespace air360 {

namespace {

const char* boolString(bool value) {
    return value ? "true" : "false";
}

const char* configLoadSourceKey(ConfigLoadSource source) {
    switch (source) {
        case ConfigLoadSource::kNvsPrimary:
            return "nvs_primary";
        case ConfigLoadSource::kNvsBackup:
            return "nvs_backup";
        case ConfigLoadSource::kDefaults:
        default:
            return "defaults";
    }
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

std::string formatDelayFromNow(std::uint64_t target_uptime_ms, std::uint64_t now_uptime_ms) {
    if (target_uptime_ms == 0U) {
        return "not scheduled";
    }

    if (target_uptime_ms <= now_uptime_ms) {
        return "now";
    }

    return formatUptimeCompact(target_uptime_ms - now_uptime_ms);
}

std::string formatFloat(float value, int precision) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.*f", precision, static_cast<double>(value));
    return buffer;
}

std::string formatBytesCompact(std::size_t bytes) {
    char buffer[32];
    if (bytes >= (1024U * 1024U)) {
        std::snprintf(
            buffer,
            sizeof(buffer),
            "%.2f MB",
            static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else if (bytes >= 1024U) {
        std::snprintf(buffer, sizeof(buffer), "%.1f KB", static_cast<double>(bytes) / 1024.0);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%zu B", bytes);
    }
    return buffer;
}

std::string prettyPrintJson(std::string_view json) {
    std::string formatted;
    formatted.reserve(json.size() + json.size() / 2U);

    constexpr char kIndentUnit[] = "  ";
    int indent_level = 0;
    bool in_string = false;
    bool escaped = false;

    const auto appendIndent = [&formatted, &indent_level, &kIndentUnit]() {
        for (int level = 0; level < indent_level; ++level) {
            formatted += kIndentUnit;
        }
    };

    for (const char ch : json) {
        if (in_string) {
            formatted.push_back(ch);
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == '"') {
                in_string = false;
            }
            continue;
        }

        switch (ch) {
            case '"':
                in_string = true;
                formatted.push_back(ch);
                break;
            case '{':
            case '[':
                formatted.push_back(ch);
                formatted.push_back('\n');
                ++indent_level;
                appendIndent();
                break;
            case '}':
            case ']':
                formatted.push_back('\n');
                if (indent_level > 0) {
                    --indent_level;
                }
                appendIndent();
                formatted.push_back(ch);
                break;
            case ',':
                formatted.push_back(ch);
                formatted.push_back('\n');
                appendIndent();
                break;
            case ':':
                formatted += ": ";
                break;
            case ' ':
            case '\n':
            case '\r':
            case '\t':
                break;
            default:
                formatted.push_back(ch);
                break;
        }
    }

    return formatted;
}

std::string measurementListHtml(const SensorMeasurement& measurement) {
    if (measurement.empty()) {
        return "";
    }

    std::string html;
    html.reserve(64U + static_cast<std::size_t>(measurement.value_count) * 128U);
    html += "<div class='readings-grid'>";
    for (std::size_t index = 0; index < measurement.value_count; ++index) {
        const SensorValue& v = measurement.values[index];
        html += renderTemplate(WebTemplateKey::kReading, {
            {"LABEL", sensorValueKindLabel(v.kind)},
            {"VALUE", formatFloat(v.value, sensorValueKindPrecision(v.kind))},
            {"UNIT",  sensorValueKindUnit(v.kind)},
        });
    }
    html += "</div>";
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
    std::string json;
    json.reserve(64U + static_cast<std::size_t>(measurement.value_count) * 128U);
    json += "[";
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
    std::string network_mode;
    std::string uplink_stat;
    std::string uptime;
    std::uint32_t boot_count = 0U;
    std::string connection_block_html;
    std::size_t sensor_count = 0U;
    std::string backend_block_html;
    std::string sensor_block_html;
};

struct RuntimeDiagnosticsSnapshot {
    std::size_t total_heap_bytes = 0U;
    std::size_t free_heap_bytes = 0U;
    std::size_t min_free_heap_bytes = 0U;
    std::size_t largest_heap_block_bytes = 0U;
    std::size_t free_internal_heap_bytes = 0U;
    std::size_t min_free_internal_heap_bytes = 0U;
    std::size_t largest_internal_heap_block_bytes = 0U;
    std::size_t sensor_task_stack_free_bytes = 0U;
    std::size_t upload_task_stack_free_bytes = 0U;
    std::size_t cellular_task_stack_free_bytes = 0U;
    std::size_t queue_pending_count = 0U;
    std::size_t queue_inflight_count = 0U;
    std::uint32_t queue_dropped_count = 0U;
};

struct StatusServiceRenderSnapshot {
    DeviceConfig config{};
    NetworkState network_state{};
    CellularState cellular_state{};
    std::uint32_t boot_count = 0U;
    bool nvs_ready = false;
    bool watchdog_armed = false;
    bool config_loaded_from_storage = false;
    bool wrote_default_config = false;
    ConfigLoadRuntimeStatus device_config_load{};
    ConfigLoadRuntimeStatus cellular_config_load{};
    ConfigLoadRuntimeStatus sensor_config_load{};
    ConfigLoadRuntimeStatus backend_config_load{};
    bool web_server_started = false;
    esp_reset_reason_t reset_reason = ESP_RST_UNKNOWN;
    std::vector<SensorRuntimeInfo> sensors;
    MeasurementStoreSnapshot measurement_store;
    UploadManagerRuntimeSnapshot upload;
    std::size_t sensor_task_stack_free_bytes = 0U;
    std::size_t upload_task_stack_free_bytes = 0U;
    std::size_t cellular_task_stack_free_bytes = 0U;
    bool has_ble_state = false;
    BleState ble_state{};
};

enum class HealthStatus : std::uint8_t {
    kHealthy = 0U,
    kBooting,
    kDegraded,
    kOffline,
    kFault,
    kSetupRequired,
};

// Per-check lifecycle. A check that has never reached kOk is kPending while
// inside its warmup window and kFailed once the deadline elapses; a check that
// reached kOk and later regressed is kStale. Disabled or non-applicable checks
// are kSkipped and do not influence the aggregate.
enum class CheckState : std::uint8_t {
    kSkipped = 0U,
    kPending,
    kOk,
    kStale,
    kFailed,
};

struct HealthCheckViewModel {
    std::string key;
    std::string label;
    CheckState state = CheckState::kSkipped;
    std::string summary;
    std::string pill_text;
};

struct HealthViewModel {
    HealthStatus status = HealthStatus::kSetupRequired;
    std::string summary;
    std::vector<HealthCheckViewModel> checks;
};

const char* healthStatusKey(HealthStatus status) {
    switch (status) {
        case HealthStatus::kHealthy:
            return "healthy";
        case HealthStatus::kBooting:
            return "booting";
        case HealthStatus::kDegraded:
            return "degraded";
        case HealthStatus::kOffline:
            return "offline";
        case HealthStatus::kFault:
            return "fault";
        case HealthStatus::kSetupRequired:
        default:
            return "setup_required";
    }
}

const char* checkStateKey(CheckState state) {
    switch (state) {
        case CheckState::kSkipped:
            return "skipped";
        case CheckState::kPending:
            return "pending";
        case CheckState::kOk:
            return "ok";
        case CheckState::kStale:
            return "stale";
        case CheckState::kFailed:
        default:
            return "failed";
    }
}

// Worst-of aggregator across a set of per-item check states. Used to collapse
// per-sensor / per-backend states into a single check.
CheckState worseCheckState(CheckState a, CheckState b) {
    auto rank = [](CheckState s) -> int {
        switch (s) {
            case CheckState::kSkipped:
                return 0;
            case CheckState::kOk:
                return 1;
            case CheckState::kPending:
                return 2;
            case CheckState::kStale:
                return 3;
            case CheckState::kFailed:
            default:
                return 4;
        }
    };
    return rank(a) >= rank(b) ? a : b;
}

std::uint64_t sensorFreshnessThresholdMs(std::uint32_t poll_interval_ms) {
    // Never mark a sensor stale sooner than 15 s; otherwise fast pollers would
    // flap during short scheduler jitter or transient bus retries.
    constexpr std::uint64_t kMinimumFreshnessThresholdMs = 15000ULL;
    if (poll_interval_ms == 0U) {
        return kMinimumFreshnessThresholdMs;
    }

    const std::uint64_t threshold = static_cast<std::uint64_t>(poll_interval_ms) * 3ULL;
    return threshold > kMinimumFreshnessThresholdMs ? threshold : kMinimumFreshnessThresholdMs;
}

std::uint64_t sensorWarmupDeadlineMs(const SensorRuntimeInfo& sensor) {
    // Give a freshly-initialized sensor at least 15 s to produce its first
    // sample, or 2 polling intervals — whichever is longer. Sensors that have
    // not reached kInitialized yet inherit a small device-uptime grace period
    // from the caller.
    constexpr std::uint64_t kMinFirstSampleWaitMs = 15000ULL;
    const std::uint64_t poll_window =
        static_cast<std::uint64_t>(sensor.poll_interval_ms) * 2ULL;
    const std::uint64_t window =
        poll_window > kMinFirstSampleWaitMs ? poll_window : kMinFirstSampleWaitMs;
    return sensor.initialized_at_uptime_ms + window;
}

CheckState evaluateSensorCheck(
    const SensorRuntimeInfo& sensor,
    const MeasurementRuntimeInfo* measurement_runtime,
    std::uint64_t now_uptime_ms) {
    if (!sensor.enabled) {
        return CheckState::kSkipped;
    }

    switch (sensor.state) {
        case SensorRuntimeState::kAbsent:
        case SensorRuntimeState::kUnsupported:
        case SensorRuntimeState::kFailed:
        case SensorRuntimeState::kError:
            return CheckState::kFailed;
        case SensorRuntimeState::kDisabled:
            return CheckState::kSkipped;
        case SensorRuntimeState::kConfigured:
        case SensorRuntimeState::kInitialized:
        case SensorRuntimeState::kPolling:
        default:
            break;
    }

    const bool have_sample = measurement_runtime != nullptr &&
                             measurement_runtime->last_sample_time_ms != 0U &&
                             measurement_runtime->last_sample_time_ms <= now_uptime_ms;
    if (have_sample) {
        const std::uint64_t sample_age_ms =
            now_uptime_ms - measurement_runtime->last_sample_time_ms;
        if (sample_age_ms <= sensorFreshnessThresholdMs(sensor.poll_interval_ms)) {
            return CheckState::kOk;
        }
        return CheckState::kStale;
    }

    // No sample yet. If the sensor has been initialized and the warmup window
    // has elapsed, treat it as failed; otherwise it is still pending.
    if (sensor.initialized_at_uptime_ms != 0U &&
        now_uptime_ms > sensorWarmupDeadlineMs(sensor)) {
        return CheckState::kFailed;
    }
    return CheckState::kPending;
}

bool backendResultIsBad(UploadResultClass result) {
    switch (result) {
        case UploadResultClass::kConfigError:
        case UploadResultClass::kUnsupported:
        case UploadResultClass::kTransportError:
        case UploadResultClass::kHttpError:
        case UploadResultClass::kNoNetwork:
            return true;
        case UploadResultClass::kUnknown:
        case UploadResultClass::kNoData:
        case UploadResultClass::kSuccess:
        default:
            return false;
    }
}

CheckState evaluateBackendCheck(
    const BackendStatusSnapshot& backend,
    std::uint32_t upload_interval_ms,
    bool uplink_available,
    std::uint64_t now_uptime_ms) {
    if (!backend.enabled) {
        return CheckState::kSkipped;
    }

    if (backend.state == BackendRuntimeState::kError) {
        return CheckState::kFailed;
    }

    const bool ever_succeeded = backend.last_success_uptime_ms != 0U;
    const bool ever_attempted = backend.last_attempt_uptime_ms != 0U;
    const bool last_bad = backendResultIsBad(backend.last_result);

    if (ever_attempted) {
        if (last_bad) {
            return ever_succeeded ? CheckState::kStale : CheckState::kFailed;
        }
        return CheckState::kOk;
    }

    // Never attempted yet. Compute warmup deadline: enabled time + one upload
    // interval + a small grace. While uplink is missing the upload manager
    // cannot try, so warmup also waits on it implicitly.
    constexpr std::uint64_t kFirstAttemptGraceMs = 5000ULL;
    const std::uint64_t deadline =
        backend.enabled_at_uptime_ms +
        static_cast<std::uint64_t>(upload_interval_ms) +
        kFirstAttemptGraceMs;
    if (!uplink_available) {
        return CheckState::kPending;
    }
    if (backend.enabled_at_uptime_ms != 0U && now_uptime_ms > deadline) {
        return CheckState::kFailed;
    }
    return CheckState::kPending;
}

const MeasurementRuntimeInfo* measurementRuntimeForSensor(
    const MeasurementStoreSnapshot& measurement_store,
    std::uint32_t sensor_id) {
    for (const auto& info : measurement_store.measurements) {
        if (info.sensor_id == sensor_id) {
            return &info;
        }
    }

    return nullptr;
}

CheckState evaluateUplinkCheck(
    const NetworkState& network_state,
    std::uint64_t now_uptime_ms) {
    if (!network_state.station_config_present) {
        // The setup_required top-level state covers this; reflect it as skipped
        // so the uplink check does not inflate the aggregate.
        return CheckState::kSkipped;
    }
    if (network_state.station_connected) {
        return CheckState::kOk;
    }

    // Warmup grace: until uptime exceeds one full connect timeout we treat
    // the missing uplink as still-pending rather than a regression.
    const std::uint64_t warmup_deadline_ms =
        static_cast<std::uint64_t>(tuning::network::kConnectTimeoutMs);
    if (now_uptime_ms <= warmup_deadline_ms) {
        return CheckState::kPending;
    }
    return CheckState::kStale;
}

CheckState evaluateTimeCheck(
    const NetworkState& network_state,
    std::uint64_t now_uptime_ms) {
    if (!network_state.station_config_present) {
        return CheckState::kSkipped;
    }
    if (network_state.time_synchronized) {
        return CheckState::kOk;
    }
    // Time can only sync once the station is connected, so anchor warmup to
    // station_connected_at. While the station is still down, time stays
    // pending unless it has been previously synchronized in this boot.
    constexpr std::uint64_t kSntpWarmupMs = 30000ULL;
    if (network_state.station_connected_at_uptime_ms != 0U) {
        const std::uint64_t deadline =
            network_state.station_connected_at_uptime_ms + kSntpWarmupMs;
        if (now_uptime_ms <= deadline) {
            return CheckState::kPending;
        }
    } else {
        // Station never connected yet — defer to uplink check.
        return CheckState::kPending;
    }

    return network_state.last_time_sync_unix_ms != 0
               ? CheckState::kStale
               : CheckState::kFailed;
}

HealthViewModel buildHealthViewModel(
    const NetworkState& network_state,
    const std::vector<SensorRuntimeInfo>& sensors,
    const std::vector<BackendStatusSnapshot>& backends,
    const MeasurementStoreSnapshot& measurement_store,
    std::uint32_t upload_interval_ms) {
    const std::uint64_t now_uptime_ms = uptimeMilliseconds();
    const bool setup_required = !network_state.station_config_present;
    const bool setup_ap_recovery =
        network_state.mode == NetworkMode::kSetupAp && network_state.station_config_present;
    const bool uplink_available = network_state.station_connected;

    const CheckState uplink_state = evaluateUplinkCheck(network_state, now_uptime_ms);
    const CheckState time_state = evaluateTimeCheck(network_state, now_uptime_ms);

    CheckState sensors_state = CheckState::kSkipped;
    std::size_t enabled_sensor_count = 0U;
    std::size_t pending_sensor_count = 0U;
    std::size_t stale_sensor_count = 0U;
    std::size_t failed_sensor_count = 0U;
    for (const auto& sensor : sensors) {
        if (!sensor.enabled) {
            continue;
        }
        ++enabled_sensor_count;
        const MeasurementRuntimeInfo* runtime =
            measurementRuntimeForSensor(measurement_store, sensor.id);
        const CheckState s = evaluateSensorCheck(sensor, runtime, now_uptime_ms);
        sensors_state = worseCheckState(sensors_state, s);
        switch (s) {
            case CheckState::kPending:
                ++pending_sensor_count;
                break;
            case CheckState::kStale:
                ++stale_sensor_count;
                break;
            case CheckState::kFailed:
                ++failed_sensor_count;
                break;
            case CheckState::kOk:
            case CheckState::kSkipped:
            default:
                break;
        }
    }
    if (enabled_sensor_count > 0U && sensors_state == CheckState::kSkipped) {
        // All enabled sensors landed in kSkipped (e.g. driver state kDisabled
        // for an enabled record). Surface that as kOk so the aggregate is
        // honest about the user-facing intent.
        sensors_state = CheckState::kOk;
    }

    CheckState backends_state = CheckState::kSkipped;
    std::size_t enabled_backend_count = 0U;
    std::size_t pending_backend_count = 0U;
    std::size_t stale_backend_count = 0U;
    std::size_t failed_backend_count = 0U;
    for (const auto& backend : backends) {
        if (!backend.enabled) {
            continue;
        }
        ++enabled_backend_count;
        const CheckState s = evaluateBackendCheck(
            backend, upload_interval_ms, uplink_available, now_uptime_ms);
        backends_state = worseCheckState(backends_state, s);
        switch (s) {
            case CheckState::kPending:
                ++pending_backend_count;
                break;
            case CheckState::kStale:
                ++stale_backend_count;
                break;
            case CheckState::kFailed:
                ++failed_backend_count;
                break;
            case CheckState::kOk:
            case CheckState::kSkipped:
            default:
                break;
        }
    }

    auto sensor_summary = [&]() -> std::string {
        if (enabled_sensor_count == 0U) {
            return "No enabled sensors.";
        }
        if (sensors_state == CheckState::kOk) {
            return "All enabled sensors are reporting.";
        }
        if (failed_sensor_count > 0U) {
            return std::to_string(failed_sensor_count) + " sensor(s) failed to report.";
        }
        if (stale_sensor_count > 0U) {
            return std::to_string(stale_sensor_count) + " sensor(s) stopped reporting.";
        }
        return std::to_string(pending_sensor_count) + " sensor(s) warming up.";
    };

    auto backend_summary = [&]() -> std::string {
        if (enabled_backend_count == 0U) {
            return "No enabled backends.";
        }
        if (backends_state == CheckState::kOk) {
            return "All enabled backends look healthy.";
        }
        if (failed_backend_count > 0U) {
            return std::to_string(failed_backend_count) + " backend(s) failing.";
        }
        if (stale_backend_count > 0U) {
            return std::to_string(stale_backend_count) + " backend(s) regressed.";
        }
        return std::to_string(pending_backend_count) + " backend(s) waiting for first upload.";
    };

    auto sensor_pill = [&]() -> std::string {
        if (enabled_sensor_count == 0U || sensors_state == CheckState::kOk) {
            return "sensors ok";
        }
        if (failed_sensor_count > 0U) {
            return std::to_string(failed_sensor_count) + " sensor failed";
        }
        if (stale_sensor_count > 0U) {
            return std::to_string(stale_sensor_count) + " sensor stale";
        }
        return std::to_string(pending_sensor_count) + " sensor warming";
    };

    auto backend_pill = [&]() -> std::string {
        if (enabled_backend_count == 0U || backends_state == CheckState::kOk) {
            return "backends ok";
        }
        if (failed_backend_count > 0U) {
            return std::to_string(failed_backend_count) + " backend failed";
        }
        if (stale_backend_count > 0U) {
            return std::to_string(stale_backend_count) + " backend stale";
        }
        return std::to_string(pending_backend_count) + " backend warming";
    };

    auto uplink_summary = [&]() -> std::string {
        if (setup_required) {
            return "Station setup is not complete.";
        }
        if (setup_ap_recovery) {
            return network_state.setup_ap_retry_active
                       ? "Setup AP fallback is active and station retry is scheduled."
                       : "Setup AP fallback is active while station recovery is pending.";
        }
        switch (uplink_state) {
            case CheckState::kOk:
                return "Station uplink connected.";
            case CheckState::kPending:
                return "Station uplink connecting.";
            case CheckState::kStale:
            case CheckState::kFailed:
            default:
                return "Station uplink unavailable.";
        }
    };

    auto uplink_pill = [&]() -> std::string {
        switch (uplink_state) {
            case CheckState::kSkipped:
                return "uplink setup";
            case CheckState::kOk:
                return "uplink ok";
            case CheckState::kPending:
                return "uplink connecting";
            case CheckState::kStale:
            case CheckState::kFailed:
            default:
                return "uplink down";
        }
    };

    auto time_summary = [&]() -> std::string {
        switch (time_state) {
            case CheckState::kOk:
                return "Valid time available.";
            case CheckState::kPending:
                return network_state.time_sync_error.empty()
                           ? "Time sync in progress."
                           : network_state.time_sync_error;
            case CheckState::kStale:
                return network_state.time_sync_error.empty()
                           ? "Time sync was lost."
                           : network_state.time_sync_error;
            case CheckState::kSkipped:
                return "Time sync deferred until setup completes.";
            case CheckState::kFailed:
            default:
                return network_state.time_sync_error.empty()
                           ? "Time sync failed."
                           : network_state.time_sync_error;
        }
    };

    auto time_pill = [&]() -> std::string {
        switch (time_state) {
            case CheckState::kSkipped:
                return "time setup";
            case CheckState::kOk:
                return "time ok";
            case CheckState::kPending:
                return "time syncing";
            case CheckState::kStale:
                return "time stale";
            case CheckState::kFailed:
            default:
                return "time missing";
        }
    };

    HealthViewModel model;
    model.checks = {
        {
            "time_synced",
            "Time synced",
            time_state,
            time_summary(),
            time_pill(),
        },
        {
            "sensors_reporting",
            "Sensors reporting",
            sensors_state,
            sensor_summary(),
            sensor_pill(),
        },
        {
            "uplink_available",
            "Uplink available",
            uplink_state,
            uplink_summary(),
            uplink_pill(),
        },
        {
            "backends_healthy",
            "Backends healthy",
            backends_state,
            backend_summary(),
            backend_pill(),
        },
    };

    if (setup_required) {
        model.status = HealthStatus::kSetupRequired;
        model.summary = "Device is in setup mode. Configure station Wi-Fi to enter normal operation.";
        return model;
    }

    if (setup_ap_recovery) {
        model.status = HealthStatus::kOffline;
        model.summary = network_state.setup_ap_retry_active
                            ? "Setup AP is active while the device keeps retrying upstream Wi-Fi."
                            : "Setup AP is active after station failure.";
        return model;
    }

    // Aggregate worst severity across applicable checks.
    CheckState worst = CheckState::kSkipped;
    for (const auto& check : model.checks) {
        worst = worseCheckState(worst, check.state);
    }

    // If we have lost the uplink past warmup and time has nothing to lean on,
    // surface that as a single "offline" top-level status — it's the message
    // the operator needs to see first.
    const bool uplink_down_steady =
        uplink_state == CheckState::kStale || uplink_state == CheckState::kFailed;
    const bool time_down_steady =
        time_state == CheckState::kStale || time_state == CheckState::kFailed;
    if (uplink_down_steady && time_down_steady) {
        model.status = HealthStatus::kOffline;
        model.summary = "Station uplink is unavailable and valid time is not ready yet.";
        return model;
    }
    if (uplink_down_steady) {
        model.status = HealthStatus::kOffline;
        model.summary = "Station uplink is unavailable.";
        return model;
    }

    switch (worst) {
        case CheckState::kFailed:
            model.status = HealthStatus::kFault;
            model.summary = "One or more components require attention.";
            break;
        case CheckState::kStale:
            model.status = HealthStatus::kDegraded;
            model.summary = "A previously healthy component stopped responding.";
            break;
        case CheckState::kPending:
            model.status = HealthStatus::kBooting;
            model.summary = "Device is starting up — components are still warming up.";
            break;
        case CheckState::kOk:
        case CheckState::kSkipped:
        default:
            model.status = HealthStatus::kHealthy;
            model.summary =
                "Time is synced, enabled sensors are reporting, and enabled backends look healthy.";
            break;
    }

    return model;
}

std::string renderBackendOverviewBlock(
    const std::vector<BackendStatusSnapshot>& backends,
    std::uint32_t /*upload_interval_ms*/) {
    const bool any_enabled = std::any_of(
        backends.begin(), backends.end(),
        [](const BackendStatusSnapshot& b) { return b.enabled; });
    if (!any_enabled) {
        return "<p class='muted'>No backends enabled.</p>";
    }

    std::string html;
    html.reserve(64U + backends.size() * 512U);
    html += "<div class='stack-10'>";
    for (const auto& backend : backends) {
        if (!backend.enabled) {
            continue;
        }

        std::string status_chips;
        std::string last_attempt;

        if (backend.last_http_status > 0) {
            const bool ok = backend.last_http_status >= 200 && backend.last_http_status < 300;
            status_chips  = "<span class='chip ";
            status_chips += ok ? "ok" : "err";
            status_chips += "'><span class='dot'></span>HTTP ";
            status_chips += std::to_string(backend.last_http_status);
            status_chips += "</span>";
            if (backend.last_response_time_ms > 0) {
                status_chips += "<span class='chip'>resp ";
                status_chips += std::to_string(backend.last_response_time_ms);
                status_chips += " ms</span>";
            }
            last_attempt = "last &middot; ";
            last_attempt += htmlEscape(formatTimeForDisplay(
                backend.last_attempt_unix_ms,
                backend.last_attempt_uptime_ms));
        } else {
            status_chips = "<span class='chip'>no data yet</span>";
        }

        html += renderTemplate(WebTemplateKey::kOverviewBackendItem, {
            {"DISPLAY_NAME",  htmlEscape(backend.display_name)},
            {"STATUS_CHIPS",  status_chips},
            {"LAST_ATTEMPT",  last_attempt},
        });
    }
    html += "</div>";
    return html;
}

std::string renderSensorOverviewBlock(
    const std::vector<SensorRuntimeInfo>& sensors,
    const MeasurementStoreSnapshot& measurement_store) {
    if (sensors.empty()) {
        return "<p class='muted overview-empty'>No sensors configured yet.</p>";
    }

    std::string html;
    html.reserve(64U + sensors.size() * 1024U);
    const std::uint64_t now_uptime_ms = uptimeMilliseconds();
    for (const auto& sensor : sensors) {
        const MeasurementRuntimeInfo* measurement_runtime =
            measurementRuntimeForSensor(measurement_store, sensor.id);
        const SensorMeasurement empty_measurement{};
        const SensorMeasurement& measurement =
            measurement_runtime != nullptr ? measurement_runtime->measurement : empty_measurement;
        const std::size_t queued_sample_count =
            measurement_runtime != nullptr ? measurement_runtime->queued_sample_count : 0U;

        // State chip
        const char* state_key   = sensorRuntimeStateKey(sensor.state);
        const char* chip_color  = "";
        bool        chip_dot    = false;
        switch (sensor.state) {
            case SensorRuntimeState::kPolling:
                chip_color = " ok";  chip_dot = true;  break;
            case SensorRuntimeState::kConfigured:
            case SensorRuntimeState::kInitialized:
                chip_color = " warn"; chip_dot = true; break;
            case SensorRuntimeState::kAbsent:
            case SensorRuntimeState::kFailed:
            case SensorRuntimeState::kError:
            case SensorRuntimeState::kUnsupported:
                chip_color = " err"; chip_dot = true;  break;
            default: break;
        }
        std::string state_chip = "<span class='chip";
        state_chip += chip_color;
        state_chip += "'>";
        if (chip_dot) state_chip += "<span class='dot'></span>";
        state_chip += state_key;
        state_chip += "</span>";

        // Binding meta
        std::string binding_meta = htmlEscape(sensor.binding_summary);
        binding_meta += " &middot; poll ";
        binding_meta += std::to_string(sensor.poll_interval_ms);
        binding_meta += " ms";

        // Error / diagnostic block
        std::string last_error_block;
        last_error_block.reserve(256U);
        if (!sensor.last_error.empty()) {
            last_error_block += "<p class='muted'>";
            last_error_block += htmlEscape(sensor.last_error);
            last_error_block += "</p>";
        }
        if (sensor.failures > 0U) {
            last_error_block += "<p class='muted'>Failures: ";
            last_error_block += std::to_string(sensor.failures);
            if (sensor.soft_fails > 0U) {
                last_error_block += "; soft fails: ";
                last_error_block += std::to_string(sensor.soft_fails);
                last_error_block += "/";
                last_error_block += std::to_string(kSensorPollFailureReinitThreshold);
            }
            if (sensor.next_retry_ms > 0U) {
                last_error_block += "; retry in ";
                last_error_block += htmlEscape(formatDelayFromNow(sensor.next_retry_ms, now_uptime_ms));
            }
            last_error_block += "</p>";
        }
        if (queued_sample_count > 0U) {
            last_error_block += "<p class='muted'>Queued: ";
            last_error_block += std::to_string(queued_sample_count);
            last_error_block += " samples</p>";
        }

        html += renderTemplate(WebTemplateKey::kOverviewSensorItem, {
            {"DISPLAY_NAME",    htmlEscape(sensor.type_name)},
            {"STATE_CHIP",      state_chip},
            {"BINDING_META",    binding_meta},
            {"READINGS_BLOCK",  measurementListHtml(measurement)},
            {"LAST_ERROR_BLOCK", last_error_block},
        });
    }
    return html;
}

std::string renderConnectionBlock(
    const NetworkState& network_state,
    const CellularState& cellular_state,
    bool has_ble_state,
    const BleState& ble_state) {
    std::string html;
    html.reserve(1024U);
    const std::uint64_t now_uptime_ms = uptimeMilliseconds();

    // Device time row
    html += renderTemplate(WebTemplateKey::kSectionRow, {
        {"LABEL",      "Device time"},
        {"VALUE_HTML", htmlEscape(currentUtcDateTimeLabel())},
    });

    // Wi-Fi row
    std::string wifi_val;
    if (!network_state.station_ssid.empty()) {
        wifi_val += "<span class='chip'>";
        wifi_val += htmlEscape(network_state.station_ssid);
        wifi_val += "</span>";
    }
    if (network_state.station_connected) {
        wifi_val += "<span class='chip accent'>";
        wifi_val += htmlEscape(network_state.ip_address.empty() ? "connected" : network_state.ip_address);
        wifi_val += "</span>";
    } else {
        wifi_val += "<span class='chip err'>not connected</span>";
    }
    html += renderTemplate(WebTemplateKey::kSectionRow, {
        {"LABEL",      "Wi-Fi"},
        {"VALUE_HTML", wifi_val},
    });

    // Wi-Fi recovery row — only when retrying
    if (network_state.reconnect_backoff_active) {
        std::string val = "<span class='chip warn'>retry ";
        val += std::to_string(network_state.reconnect_attempt_count);
        val += " in ";
        val += htmlEscape(formatDelayFromNow(
            network_state.next_reconnect_uptime_ms, now_uptime_ms));
        val += "</span>";
        html += renderTemplate(WebTemplateKey::kSectionRow, {
            {"LABEL",      "Wi-Fi recovery"},
            {"VALUE_HTML", val},
        });
    } else if (network_state.setup_ap_retry_active) {
        std::string val = "<span class='chip warn'>setup AP retry in ";
        val += htmlEscape(formatDelayFromNow(
            network_state.next_setup_ap_retry_uptime_ms, now_uptime_ms));
        val += "</span>";
        html += renderTemplate(WebTemplateKey::kSectionRow, {
            {"LABEL",      "Wi-Fi recovery"},
            {"VALUE_HTML", val},
        });
    }

    // Wi-Fi error row — only when present
    if (!network_state.last_error.empty()) {
        html += renderTemplate(WebTemplateKey::kSectionRow, {
            {"LABEL",      "Wi-Fi error"},
            {"VALUE_HTML", "<span class='chip err'>" + htmlEscape(network_state.last_error) + "</span>"},
        });
    }

    // Cellular row — only if enabled
    if (cellular_state.enabled) {
        std::string cell_val;
        if (cellular_state.ppp_connected) {
            cell_val += "<span class='chip ok'>";
            cell_val += htmlEscape(
                cellular_state.ip_address.empty() ? "connected" : cellular_state.ip_address);
            cell_val += "</span>";
        } else {
            cell_val += "<span class='chip err'>not connected</span>";
        }
        if (cellular_state.rssi_dbm != 0) {
            cell_val += "<span class='chip accent'>";
            cell_val += std::to_string(cellular_state.rssi_dbm);
            cell_val += " dBm</span>";
        }
        if (cellular_state.ppp_connected && !cellular_state.connectivity_check_skipped) {
            const bool ok = cellular_state.connectivity_ok;
            cell_val += "<span class='chip ";
            cell_val += ok ? "ok'><span class='dot'></span>ping ok" : "err'><span class='dot'></span>ping failed";
            cell_val += "</span>";
        }
        html += renderTemplate(WebTemplateKey::kSectionRow, {
            {"LABEL",      "Cellular"},
            {"VALUE_HTML", cell_val},
        });
    }

    // BLE row — only if enabled
    if (has_ble_state && ble_state.enabled) {
        std::string ble_val;
        if (ble_state.running) {
            ble_val  = "<span class='chip ok'><span class='dot'></span>Active</span>";
            ble_val += "<span class='mono-meta'>BTHome v2 &middot; ";
            ble_val += std::to_string(ble_state.adv_interval_ms);
            ble_val += " ms</span>";
        } else {
            ble_val  = "<span class='chip'>Starting&hellip;</span>";
            ble_val += "<span class='mono-meta'>BTHome v2</span>";
        }
        html += renderTemplate(WebTemplateKey::kSectionRow, {
            {"LABEL",      "BLE"},
            {"VALUE_HTML", ble_val},
        });
    }

    return html;
}

RuntimeOverviewViewModel buildRuntimeOverviewViewModel(
    const NetworkState& network_state,
    const CellularState& cellular_state,
    std::uint32_t boot_count,
    const std::vector<SensorRuntimeInfo>& sensors,
    const MeasurementStoreSnapshot& measurement_store,
    const UploadManagerRuntimeSnapshot& upload,
    bool has_ble_state,
    const BleState& ble_state) {
    RuntimeOverviewViewModel model;
    const HealthViewModel health = buildHealthViewModel(
        network_state, sensors, upload.backends, measurement_store, upload.upload_interval_ms);

    // Pill class + label per top-level status: kBooting/kSetupRequired use the
    // neutral palettes; kDegraded uses warn; kOffline/kFault are red.
    auto pill_class_for = [](HealthStatus status) -> const char* {
        switch (status) {
            case HealthStatus::kHealthy:
                return "chip ok";
            case HealthStatus::kBooting:
                return "chip accent";
            case HealthStatus::kDegraded:
                return "chip warn";
            case HealthStatus::kSetupRequired:
                return "chip warn";
            case HealthStatus::kOffline:
            case HealthStatus::kFault:
            default:
                return "chip err";
        }
    };
    auto pill_label_for = [](HealthStatus status) -> const char* {
        switch (status) {
            case HealthStatus::kHealthy:
                return "Healthy";
            case HealthStatus::kBooting:
                return "Starting up";
            case HealthStatus::kDegraded:
                return "Degraded";
            case HealthStatus::kOffline:
                return "Offline";
            case HealthStatus::kFault:
                return "Fault";
            case HealthStatus::kSetupRequired:
            default:
                return "Setup";
        }
    };
    model.health_status_pill_html = "<span class='";
    model.health_status_pill_html += pill_class_for(health.status);
    model.health_status_pill_html += "'>";
    model.health_status_pill_html += "<span class='dot'></span>";
    model.health_status_pill_html += pill_label_for(health.status);
    model.health_status_pill_html += "</span>";

    // Uplink stat: cellular is primary when enabled; Wi-Fi is fallback/debug only.
    if (cellular_state.enabled) {
        model.uplink_stat = cellular_state.ppp_connected ? "cellular" : "cellular (connecting)";
    } else if (network_state.station_connected) {
        model.uplink_stat = "wifi";
    } else {
        model.uplink_stat = "offline";
    }

    model.network_mode = networkModeString(network_state.mode);
    model.uptime = formatUptimeCompact(uptimeMilliseconds());
    model.boot_count = boot_count;
    model.connection_block_html = renderConnectionBlock(
        network_state, cellular_state, has_ble_state, ble_state);
    model.sensor_count = sensors.size();
    model.backend_block_html =
        renderBackendOverviewBlock(upload.backends, upload.upload_interval_ms);
    model.sensor_block_html = renderSensorOverviewBlock(sensors, measurement_store);

    return model;
}

RuntimeDiagnosticsSnapshot buildRuntimeDiagnosticsSnapshot(
    const StatusServiceRenderSnapshot& render_snapshot) {
    RuntimeDiagnosticsSnapshot snapshot;
    snapshot.total_heap_bytes = heap_caps_get_total_size(MALLOC_CAP_8BIT);
    snapshot.free_heap_bytes = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    snapshot.min_free_heap_bytes = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    snapshot.largest_heap_block_bytes = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);

    constexpr std::uint32_t kInternal8BitCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    snapshot.free_internal_heap_bytes = heap_caps_get_free_size(kInternal8BitCaps);
    snapshot.min_free_internal_heap_bytes = heap_caps_get_minimum_free_size(kInternal8BitCaps);
    snapshot.largest_internal_heap_block_bytes =
        heap_caps_get_largest_free_block(kInternal8BitCaps);
    snapshot.queue_pending_count = render_snapshot.measurement_store.pending_count;
    snapshot.queue_inflight_count = render_snapshot.upload.inflight_sample_count;
    snapshot.queue_dropped_count = render_snapshot.measurement_store.dropped_sample_count;
    snapshot.sensor_task_stack_free_bytes = render_snapshot.sensor_task_stack_free_bytes;
    snapshot.upload_task_stack_free_bytes = render_snapshot.upload_task_stack_free_bytes;
    snapshot.cellular_task_stack_free_bytes = render_snapshot.cellular_task_stack_free_bytes;

    return snapshot;
}



std::string renderConfigLoadStatusJson(const ConfigLoadRuntimeStatus& status) {
    std::string json;
    json.reserve(256U);
    json += "{";
    json += "\"load_source\":\"";
    json += configLoadSourceKey(status.load_source);
    json += "\",\"nvs_primary_load_count\":";
    json += std::to_string(status.counters.nvs_primary);
    json += ",\"nvs_backup_load_count\":";
    json += std::to_string(status.counters.nvs_backup);
    json += ",\"defaults_load_count\":";
    json += std::to_string(status.counters.defaults);
    json += ",\"wrote_defaults\":";
    json += boolString(status.wrote_defaults);
    json += ",\"last_error\":\"";
    json += jsonEscape(esp_err_to_name(status.last_error));
    json += "\"}";
    return json;
}

std::string buildStatusJsonDocument(
    const BuildInfo& build_info,
    const StatusServiceRenderSnapshot& render_snapshot) {
    const HealthViewModel health = buildHealthViewModel(
        render_snapshot.network_state,
        render_snapshot.sensors,
        render_snapshot.upload.backends,
        render_snapshot.measurement_store,
        render_snapshot.upload.upload_interval_ms);
    const RuntimeDiagnosticsSnapshot diagnostics =
        buildRuntimeDiagnosticsSnapshot(render_snapshot);

    std::string json;
    json.reserve(8192);
    json += "{";
    json += "\"project_name\":\"" + jsonEscape(build_info.project_name) + "\",";
    json += "\"project_version\":\"" + jsonEscape(build_info.project_version) + "\",";
    json += "\"idf_version\":\"" + jsonEscape(build_info.idf_version) + "\",";
    json += "\"board_name\":\"" + jsonEscape(build_info.board_name) + "\",";
    json += "\"chip_name\":\"" + jsonEscape(build_info.chip_name) + "\",";
    json += "\"chip_revision\":\"" + jsonEscape(build_info.chip_revision) + "\",";
    json += "\"chip_type\":\"" + jsonEscape(build_info.chip_type) + "\",";
    json += "\"chip_features\":\"" + jsonEscape(build_info.chip_features) + "\",";
    json += "\"crystal_frequency\":\"" + jsonEscape(build_info.crystal_frequency) + "\",";
    json += "\"current_datetime\":\"" + jsonEscape(currentUtcDateTimeLabel()) + "\",";
    json += "\"compile_date\":\"" + jsonEscape(build_info.compile_date) + "\",";
    json += "\"compile_time\":\"" + jsonEscape(build_info.compile_time) + "\",";
    json += "\"device_id\":\"" + jsonEscape(build_info.device_id) + "\",";
    json += "\"short_device_id\":\"" + jsonEscape(build_info.short_device_id) + "\",";
    json += "\"esp_mac_id\":\"" + jsonEscape(build_info.esp_mac_id) + "\",";
    json += "\"device_name\":\"" + jsonEscape(render_snapshot.config.device_name) + "\",";
    json += "\"wifi_station_ssid\":\"" + jsonEscape(render_snapshot.config.wifi_sta_ssid) + "\",";
    json += "\"setup_ap_ssid\":\"" + jsonEscape(render_snapshot.config.lab_ap_ssid) + "\",";
    json += "\"boot_count\":" + std::to_string(render_snapshot.boot_count) + ",";
    json += "\"uptime_ms\":" + std::to_string(uptimeMilliseconds()) + ",";
    json += "\"reset_reason\":" +
            std::to_string(static_cast<int>(render_snapshot.reset_reason)) + ",";
    json += "\"reset_reason_label\":\"";
    json += jsonEscape(resetReasonLabel(render_snapshot.reset_reason));
    json += "\",";
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
        json += checkStateKey(check.state);
        json += "\",\"summary\":\"";
        json += jsonEscape(check.summary);
        json += "\"}";
    }
    json += "},";
    json += "\"nvs_ready\":";
    json += boolString(render_snapshot.nvs_ready);
    json += ",\"watchdog_armed\":";
    json += boolString(render_snapshot.watchdog_armed);
    json += ",\"config_loaded_from_storage\":";
    json += boolString(render_snapshot.config_loaded_from_storage);
    json += ",\"wrote_default_config\":";
    json += boolString(render_snapshot.wrote_default_config);
    json += ",\"config\":{";
    json += "\"device\":";
    json += renderConfigLoadStatusJson(render_snapshot.device_config_load);
    json += ",\"cellular\":";
    json += renderConfigLoadStatusJson(render_snapshot.cellular_config_load);
    json += ",\"sensors\":";
    json += renderConfigLoadStatusJson(render_snapshot.sensor_config_load);
    json += ",\"backends\":";
    json += renderConfigLoadStatusJson(render_snapshot.backend_config_load);
    json += "}";
    json += ",\"web_server_started\":";
    json += boolString(render_snapshot.web_server_started);
    json += ",\"http_port\":" + std::to_string(render_snapshot.config.http_port) + ",";
    json += "\"lab_ap_enabled\":";
    json += boolString(render_snapshot.config.lab_ap_enabled != 0U);
    json += ",\"local_auth_enabled\":";
    json += boolString(render_snapshot.config.local_auth_enabled != 0U);
    json += ",\"network_mode\":\"";
    json += networkModeString(render_snapshot.network_state.mode);
    json += "\",\"station_config_present\":";
    json += boolString(render_snapshot.network_state.station_config_present);
    json += ",\"station_connect_attempted\":";
    json += boolString(render_snapshot.network_state.station_connect_attempted);
    json += ",\"station_connected\":";
    json += boolString(render_snapshot.network_state.station_connected);
    json += ",\"lab_ap_active\":";
    json += boolString(render_snapshot.network_state.lab_ap_active);
    json += ",\"time_sync_attempted\":";
    json += boolString(render_snapshot.network_state.time_sync_attempted);
    json += ",\"time_synchronized\":";
    json += boolString(render_snapshot.network_state.time_synchronized);
    json += ",\"last_time_sync_unix_ms\":";
    json += std::to_string(render_snapshot.network_state.last_time_sync_unix_ms);
    json += ",\"active_station_ssid\":\"";
    json += jsonEscape(render_snapshot.network_state.station_ssid);
    json += "\",";
    json += "\"active_setup_ap_ssid\":\"";
    json += jsonEscape(render_snapshot.network_state.lab_ap_ssid);
    json += "\",";
    json += "\"lab_ap_ip\":\"" + jsonEscape(render_snapshot.network_state.ip_address) + "\",";
    json += "\"last_error\":\"" + jsonEscape(render_snapshot.network_state.last_error) + "\",";
    json += "\"last_disconnect_reason\":";
    json += std::to_string(render_snapshot.network_state.last_disconnect_reason);
    json += ",\"last_disconnect_reason_label\":\"";
    json += jsonEscape(render_snapshot.network_state.last_disconnect_reason_label);
    json += "\",\"wifi_reconnect_backoff_active\":";
    json += boolString(render_snapshot.network_state.reconnect_backoff_active);
    json += ",\"wifi_reconnect_attempt_count\":";
    json += std::to_string(render_snapshot.network_state.reconnect_attempt_count);
    json += ",\"wifi_next_reconnect_uptime_ms\":";
    json += std::to_string(render_snapshot.network_state.next_reconnect_uptime_ms);
    json += ",\"wifi_setup_ap_retry_active\":";
    json += boolString(render_snapshot.network_state.setup_ap_retry_active);
    json += ",\"wifi_next_setup_ap_retry_uptime_ms\":";
    json += std::to_string(render_snapshot.network_state.next_setup_ap_retry_uptime_ms);
    json += ",";
    json += "\"time_sync_error\":\"" + jsonEscape(render_snapshot.network_state.time_sync_error) +
            "\",";
    json += "\"cellular\":{";
    json += "\"enabled\":";
    json += boolString(render_snapshot.cellular_state.enabled);
    json += ",\"ppp_connected\":";
    json += boolString(render_snapshot.cellular_state.ppp_connected);
    json += ",\"ip_address\":\"" + jsonEscape(render_snapshot.cellular_state.ip_address) + "\",";
    json += "\"connectivity_ok\":";
    json += boolString(render_snapshot.cellular_state.connectivity_ok);
    json += ",\"connectivity_check_skipped\":";
    json += boolString(render_snapshot.cellular_state.connectivity_check_skipped);
    json += ",\"reconnect_attempts\":";
    json += std::to_string(render_snapshot.cellular_state.reconnect_attempts);
    json += ",\"consecutive_failures\":";
    json += std::to_string(render_snapshot.cellular_state.consecutive_failures);
    json += ",\"next_reconnect_uptime_ms\":";
    json += std::to_string(render_snapshot.cellular_state.next_reconnect_uptime_ms);
    json += ",\"pwrkey_cycles_total\":";
    json += std::to_string(render_snapshot.cellular_state.pwrkey_cycles_total);
    json += ",\"last_pwrkey_ms_ago\":";
    if (render_snapshot.cellular_state.last_pwrkey_uptime_ms == 0U) {
        json += "0";
    } else {
        const std::uint64_t now_ms = uptimeMilliseconds();
        const std::uint64_t age_ms =
            now_ms > render_snapshot.cellular_state.last_pwrkey_uptime_ms
                ? now_ms - render_snapshot.cellular_state.last_pwrkey_uptime_ms
                : 0U;
        json += std::to_string(age_ms);
    }
    json += ",\"last_error\":\"" + jsonEscape(render_snapshot.cellular_state.last_error) + "\"";
    json += "},";
    json += "\"configured_sensors_count\":" + std::to_string(render_snapshot.sensors.size()) + ",";
    json += "\"enabled_backends_count\":";
    json += std::to_string(render_snapshot.upload.enabled_count);
    json += ",\"degraded_backends_count\":";
    json += std::to_string(render_snapshot.upload.degraded_count);
    json += ",\"upload_interval_ms\":";
    json += std::to_string(render_snapshot.upload.upload_interval_ms);
    json += ",\"last_upload_attempt_uptime_ms\":";
    json += std::to_string(render_snapshot.upload.last_overall_attempt_uptime_ms);
    json += ",\"last_upload_attempt_unix_ms\":";
    json += std::to_string(render_snapshot.upload.last_overall_attempt_unix_ms);
    json += ",\"diagnostics\":{";
    json += "\"heap_total_bytes\":";
    json += std::to_string(diagnostics.total_heap_bytes);
    json += ",";
    json += "\"heap_free_bytes\":";
    json += std::to_string(diagnostics.free_heap_bytes);
    json += ",\"heap_min_free_bytes\":";
    json += std::to_string(diagnostics.min_free_heap_bytes);
    json += ",\"heap_largest_block_bytes\":";
    json += std::to_string(diagnostics.largest_heap_block_bytes);
    json += ",\"internal_heap_free_bytes\":";
    json += std::to_string(diagnostics.free_internal_heap_bytes);
    json += ",\"internal_heap_min_free_bytes\":";
    json += std::to_string(diagnostics.min_free_internal_heap_bytes);
    json += ",\"internal_heap_largest_block_bytes\":";
    json += std::to_string(diagnostics.largest_internal_heap_block_bytes);
    json += ",\"sensor_task_stack_high_watermark_bytes\":";
    json += std::to_string(diagnostics.sensor_task_stack_free_bytes);
    json += ",\"upload_task_stack_high_watermark_bytes\":";
    json += std::to_string(diagnostics.upload_task_stack_free_bytes);
    json += ",\"cellular_task_stack_high_watermark_bytes\":";
    json += std::to_string(diagnostics.cellular_task_stack_free_bytes);
    json += ",\"measurement_queue_pending_count\":";
    json += std::to_string(diagnostics.queue_pending_count);
    json += ",\"measurement_queue_inflight_count\":";
    json += std::to_string(diagnostics.queue_inflight_count);
    json += ",\"measurement_queue_dropped_count\":";
    json += std::to_string(diagnostics.queue_dropped_count);
    json += "}";
    json += ",\"backends\":[";
    for (std::size_t index = 0; index < render_snapshot.upload.backends.size(); ++index) {
        const auto& backend = render_snapshot.upload.backends[index];
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
        json += ",\"best_effort\":";
        json += boolString(backend.best_effort);
        json += ",\"missed_sample_count\":";
        json += std::to_string(backend.missed_sample_count);
        json += ",\"best_effort_since_uptime_ms\":";
        json += std::to_string(backend.best_effort_since_uptime_ms);
        json += ",\"next_retry_uptime_ms\":";
        json += std::to_string(backend.next_retry_uptime_ms);
        json += ",\"last_error\":\"";
        json += jsonEscape(backend.last_error);
        json += "\"}";
    }
    json += "],";
    json += "\"sensors\":[";
    for (std::size_t index = 0; index < render_snapshot.sensors.size(); ++index) {
        const auto& sensor = render_snapshot.sensors[index];
        const MeasurementRuntimeInfo* measurement_runtime =
            measurementRuntimeForSensor(render_snapshot.measurement_store, sensor.id);
        const MeasurementRuntimeInfo empty_measurement_runtime{};
        const MeasurementRuntimeInfo& runtime =
            measurement_runtime != nullptr ? *measurement_runtime : empty_measurement_runtime;
        if (index > 0U) {
            json += ",";
        }
        json += "{";
        json += "\"id\":" + std::to_string(sensor.id) + ",";
        json += "\"enabled\":";
        json += boolString(sensor.enabled);
        json += ",\"sensor_type\":\"" + jsonEscape(sensor.type_key) + "\",";
        json += "\"sensor_name\":\"" + jsonEscape(sensor.type_name) + "\",";
        json += "\"transport_kind\":\"" + jsonEscape(transportKindKey(sensor.transport_kind)) +
                "\",";
        json += "\"binding\":\"" + jsonEscape(sensor.binding_summary) + "\",";
        json += "\"poll_interval_ms\":" + std::to_string(sensor.poll_interval_ms) + ",";
        json += "\"status\":\"" + jsonEscape(sensorRuntimeStateKey(sensor.state)) + "\",";
        json += "\"failures\":" + std::to_string(sensor.failures) + ",";
        json += "\"soft_fails\":" + std::to_string(sensor.soft_fails) + ",";
        json += "\"next_retry_ms\":" + std::to_string(sensor.next_retry_ms) + ",";
        json += "\"last_sample_time_ms\":" + std::to_string(runtime.last_sample_time_ms) + ",";
        json += "\"queued_sample_count\":" + std::to_string(runtime.queued_sample_count) + ",";
        json += "\"measurements\":";
        json += measurementArrayJson(runtime.measurement);
        json += ",";
        json += "\"temperature_c\":";
        json += jsonNumberOrNull(runtime.measurement, SensorValueKind::kTemperatureC);
        json += ",\"humidity_percent\":";
        json += jsonNumberOrNull(runtime.measurement, SensorValueKind::kHumidityPercent);
        json += ",\"pressure_hpa\":";
        json += jsonNumberOrNull(runtime.measurement, SensorValueKind::kPressureHpa);
        json += ",\"gas_resistance_ohms\":";
        json += jsonNumberOrNull(runtime.measurement, SensorValueKind::kGasResistanceOhms);
        json += ",\"illuminance_lux\":";
        json += jsonNumberOrNull(runtime.measurement, SensorValueKind::kIlluminanceLux);
        json += ",";
        json += "\"last_error\":\"" + jsonEscape(sensor.last_error) + "\"";
        json += "}";
    }
    json += "]}";
    return json;
}

}  // namespace

StatusService::StatusService(BuildInfo build_info) : build_info_(std::move(build_info)) {
    mutex_ = xSemaphoreCreateMutexStatic(&mutex_buffer_);
}

void StatusService::lock() const {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
    }
}

void StatusService::unlock() const {
    if (mutex_ != nullptr) {
        xSemaphoreGive(mutex_);
    }
}

void StatusService::markNvsReady(bool ready) {
    lock();
    nvs_ready_ = ready;
    unlock();
}

void StatusService::markWatchdogArmed(bool armed) {
    lock();
    watchdog_armed_ = armed;
    unlock();
}

void StatusService::setConfig(
    const DeviceConfig& config,
    bool loaded_from_storage,
    bool wrote_defaults) {
    lock();
    config_ = config;
    config_loaded_from_storage_ = loaded_from_storage;
    wrote_default_config_ = wrote_defaults;
    unlock();
}

void StatusService::recordConfigLoad(
    ConfigRepositoryKind repository,
    ConfigLoadSource source,
    esp_err_t result,
    bool wrote_defaults) {
    lock();
    ConfigLoadRuntimeStatus* status = nullptr;
    switch (repository) {
        case ConfigRepositoryKind::kDevice:
            status = &device_config_load_;
            break;
        case ConfigRepositoryKind::kCellular:
            status = &cellular_config_load_;
            break;
        case ConfigRepositoryKind::kSensors:
            status = &sensor_config_load_;
            break;
        case ConfigRepositoryKind::kBackends:
            status = &backend_config_load_;
            break;
        default:
            break;
    }

    if (status != nullptr) {
        status->load_source = source;
        status->last_error = result;
        status->wrote_defaults = wrote_defaults;
        switch (source) {
            case ConfigLoadSource::kNvsPrimary:
                ++status->counters.nvs_primary;
                break;
            case ConfigLoadSource::kNvsBackup:
                ++status->counters.nvs_backup;
                break;
            case ConfigLoadSource::kDefaults:
            default:
                ++status->counters.defaults;
                break;
        }
    }
    unlock();
}

void StatusService::setBootCount(std::uint32_t boot_count) {
    lock();
    boot_count_ = boot_count;
    unlock();
}

void StatusService::setNetworkState(const NetworkState& state) {
    lock();
    network_state_ = state;
    unlock();
}

void StatusService::setCellularState(const CellularState& state) {
    lock();
    cellular_state_ = state;
    unlock();
}

void StatusService::setCellularManager(const CellularManager& cellular_manager) {
    lock();
    cellular_manager_ = &cellular_manager;
    unlock();
}

void StatusService::setSensors(const SensorManager& sensor_manager) {
    lock();
    sensor_manager_ = &sensor_manager;
    unlock();
}

void StatusService::setMeasurements(const MeasurementStore& measurement_store) {
    lock();
    measurement_store_ = &measurement_store;
    unlock();
}

void StatusService::setUploads(const UploadManager& upload_manager) {
    lock();
    upload_manager_ = &upload_manager;
    unlock();
}

void StatusService::setWebServerStarted(bool started) {
    lock();
    web_server_started_ = started;
    unlock();
}

void StatusService::setBleAdvertiser(const BleAdvertiser& ble) {
    lock();
    ble_advertiser_ = &ble;
    unlock();
}

std::string StatusService::renderRootHtml() const {
    StatusServiceRenderSnapshot render_snapshot;
    const SensorManager* sensor_manager = nullptr;
    const MeasurementStore* measurement_store = nullptr;
    const UploadManager* upload_manager = nullptr;
    const BleAdvertiser* ble_advertiser = nullptr;

    lock();
    render_snapshot.config = config_;
    render_snapshot.network_state = network_state_;
    render_snapshot.cellular_state = cellular_state_;
    render_snapshot.boot_count = boot_count_;
    render_snapshot.nvs_ready = nvs_ready_;
    render_snapshot.watchdog_armed = watchdog_armed_;
    render_snapshot.config_loaded_from_storage = config_loaded_from_storage_;
    render_snapshot.wrote_default_config = wrote_default_config_;
    render_snapshot.device_config_load = device_config_load_;
    render_snapshot.cellular_config_load = cellular_config_load_;
    render_snapshot.sensor_config_load = sensor_config_load_;
    render_snapshot.backend_config_load = backend_config_load_;
    render_snapshot.web_server_started = web_server_started_;
    render_snapshot.reset_reason = reset_reason_;
    sensor_manager = sensor_manager_;
    measurement_store = measurement_store_;
    upload_manager = upload_manager_;
    ble_advertiser = ble_advertiser_;
    unlock();

    if (sensor_manager != nullptr) {
        render_snapshot.sensors = sensor_manager->sensors();
    }
    if (measurement_store != nullptr) {
        render_snapshot.measurement_store = measurement_store->snapshot();
    }
    if (upload_manager != nullptr) {
        render_snapshot.upload = upload_manager->runtimeSnapshot();
    }
    if (ble_advertiser != nullptr) {
        render_snapshot.has_ble_state = true;
        render_snapshot.ble_state = ble_advertiser->state();
    }

    const RuntimeOverviewViewModel model = buildRuntimeOverviewViewModel(
        render_snapshot.network_state,
        render_snapshot.cellular_state,
        render_snapshot.boot_count,
        render_snapshot.sensors,
        render_snapshot.measurement_store,
        render_snapshot.upload,
        render_snapshot.has_ble_state,
        render_snapshot.ble_state);

    const std::string body = renderPageTemplate(
        WebTemplateKey::kHome,
        WebTemplateBindings{
            {"NETWORK_MODE", htmlEscape(model.network_mode)},
            {"UPLINK_STAT", htmlEscape(model.uplink_stat)},
            {"UPTIME", htmlEscape(model.uptime)},
            {"BOOT_COUNT", std::to_string(model.boot_count)},
            {"CONNECTION_BLOCK", model.connection_block_html},
            {"SENSOR_COUNT", std::to_string(model.sensor_count)},
            {"BACKEND_BLOCK", model.backend_block_html},
            {"SENSOR_BLOCK", model.sensor_block_html},
        });

    return renderPageDocument(
        WebPageKey::kHome,
        "Air 360 Runtime Overview",
        "Runtime Overview",
        model.health_status_pill_html,
        body,
        true);
}

std::string StatusService::renderDiagnosticsHtml(std::string_view log_contents) const {
    StatusServiceRenderSnapshot render_snapshot;
    const CellularManager* cellular_manager = nullptr;
    const SensorManager* sensor_manager = nullptr;
    const MeasurementStore* measurement_store = nullptr;
    const UploadManager* upload_manager = nullptr;
    const BleAdvertiser* ble_advertiser = nullptr;

    lock();
    render_snapshot.config = config_;
    render_snapshot.network_state = network_state_;
    render_snapshot.cellular_state = cellular_state_;
    render_snapshot.boot_count = boot_count_;
    render_snapshot.nvs_ready = nvs_ready_;
    render_snapshot.watchdog_armed = watchdog_armed_;
    render_snapshot.config_loaded_from_storage = config_loaded_from_storage_;
    render_snapshot.wrote_default_config = wrote_default_config_;
    render_snapshot.device_config_load = device_config_load_;
    render_snapshot.cellular_config_load = cellular_config_load_;
    render_snapshot.sensor_config_load = sensor_config_load_;
    render_snapshot.backend_config_load = backend_config_load_;
    render_snapshot.web_server_started = web_server_started_;
    render_snapshot.reset_reason = reset_reason_;
    cellular_manager = cellular_manager_;
    sensor_manager = sensor_manager_;
    measurement_store = measurement_store_;
    upload_manager = upload_manager_;
    ble_advertiser = ble_advertiser_;
    unlock();

    if (sensor_manager != nullptr) {
        render_snapshot.sensors = sensor_manager->sensors();
        render_snapshot.sensor_task_stack_free_bytes =
            sensor_manager->taskStackHighWaterMarkBytes();
    }
    if (measurement_store != nullptr) {
        render_snapshot.measurement_store = measurement_store->snapshot();
    }
    if (upload_manager != nullptr) {
        render_snapshot.upload = upload_manager->runtimeSnapshot();
        render_snapshot.upload_task_stack_free_bytes =
            upload_manager->taskStackHighWaterMarkBytes();
    }
    if (cellular_manager != nullptr) {
        render_snapshot.cellular_task_stack_free_bytes =
            cellular_manager->taskStackHighWaterMarkBytes();
    }
    if (ble_advertiser != nullptr) {
        render_snapshot.has_ble_state = true;
        render_snapshot.ble_state = ble_advertiser->state();
    }

    const RuntimeDiagnosticsSnapshot diagnostics =
        buildRuntimeDiagnosticsSnapshot(render_snapshot);
    const std::string status_json =
        prettyPrintJson(buildStatusJsonDocument(build_info_, render_snapshot));

    const std::string body = renderPageTemplate(
        WebTemplateKey::kDiagnostics,
        WebTemplateBindings{
            {"TOTAL_HEAP", htmlEscape(formatBytesCompact(diagnostics.total_heap_bytes))},
            {"FREE_HEAP", htmlEscape(formatBytesCompact(diagnostics.free_heap_bytes))},
            {"MIN_HEAP", htmlEscape(formatBytesCompact(diagnostics.min_free_heap_bytes))},
            {"LARGEST_BLOCK", htmlEscape(formatBytesCompact(diagnostics.largest_heap_block_bytes))},
            {"LOG_CONTENTS", htmlEscape(log_contents)},
            {"STATUS_JSON_DUMP", htmlEscape(status_json)},
        });

    return renderPageDocument(
        WebPageKey::kDiagnostics,
        "Air 360 Diagnostics",
        "Diagnostics",
        "",
        body,
        true);
}

std::string StatusService::renderStatusJson() const {
    StatusServiceRenderSnapshot render_snapshot;
    const CellularManager* cellular_manager = nullptr;
    const SensorManager* sensor_manager = nullptr;
    const MeasurementStore* measurement_store = nullptr;
    const UploadManager* upload_manager = nullptr;
    const BleAdvertiser* ble_advertiser = nullptr;

    lock();
    render_snapshot.config = config_;
    render_snapshot.network_state = network_state_;
    render_snapshot.cellular_state = cellular_state_;
    render_snapshot.boot_count = boot_count_;
    render_snapshot.nvs_ready = nvs_ready_;
    render_snapshot.watchdog_armed = watchdog_armed_;
    render_snapshot.config_loaded_from_storage = config_loaded_from_storage_;
    render_snapshot.wrote_default_config = wrote_default_config_;
    render_snapshot.device_config_load = device_config_load_;
    render_snapshot.cellular_config_load = cellular_config_load_;
    render_snapshot.sensor_config_load = sensor_config_load_;
    render_snapshot.backend_config_load = backend_config_load_;
    render_snapshot.web_server_started = web_server_started_;
    render_snapshot.reset_reason = reset_reason_;
    cellular_manager = cellular_manager_;
    sensor_manager = sensor_manager_;
    measurement_store = measurement_store_;
    upload_manager = upload_manager_;
    ble_advertiser = ble_advertiser_;
    unlock();

    if (sensor_manager != nullptr) {
        render_snapshot.sensors = sensor_manager->sensors();
        render_snapshot.sensor_task_stack_free_bytes =
            sensor_manager->taskStackHighWaterMarkBytes();
    }
    if (measurement_store != nullptr) {
        render_snapshot.measurement_store = measurement_store->snapshot();
    }
    if (upload_manager != nullptr) {
        render_snapshot.upload = upload_manager->runtimeSnapshot();
        render_snapshot.upload_task_stack_free_bytes =
            upload_manager->taskStackHighWaterMarkBytes();
    }
    if (cellular_manager != nullptr) {
        render_snapshot.cellular_task_stack_free_bytes =
            cellular_manager->taskStackHighWaterMarkBytes();
    }
    if (ble_advertiser != nullptr) {
        render_snapshot.has_ble_state = true;
        render_snapshot.ble_state = ble_advertiser->state();
    }

    return buildStatusJsonDocument(build_info_, render_snapshot);
}

NetworkState StatusService::networkState() const {
    lock();
    const NetworkState snapshot = network_state_;
    unlock();
    return snapshot;
}

const BuildInfo& StatusService::buildInfo() const {
    return build_info_;
}

}  // namespace air360
