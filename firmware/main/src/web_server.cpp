#include "air360/web_server.hpp"

#include <cinttypes>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

#include "air360/sensors/sensor_config_repository.hpp"
#include "air360/sensors/sensor_manager.hpp"
#include "air360/sensors/sensor_registry.hpp"
#include "air360/sensors/sensor_types.hpp"
#include "air360/uploads/backend_registry.hpp"
#include "air360/uploads/upload_manager.hpp"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#ifndef CONFIG_AIR360_GPS_DEFAULT_UART_PORT
#define CONFIG_AIR360_GPS_DEFAULT_UART_PORT 1
#endif

#ifndef CONFIG_AIR360_GPS_DEFAULT_RX_GPIO
#define CONFIG_AIR360_GPS_DEFAULT_RX_GPIO 44
#endif

#ifndef CONFIG_AIR360_GPS_DEFAULT_TX_GPIO
#define CONFIG_AIR360_GPS_DEFAULT_TX_GPIO 43
#endif

#ifndef CONFIG_AIR360_GPS_DEFAULT_BAUD_RATE
#define CONFIG_AIR360_GPS_DEFAULT_BAUD_RATE 9600
#endif

#ifndef CONFIG_AIR360_GPIO_SENSOR_PIN_0
#define CONFIG_AIR360_GPIO_SENSOR_PIN_0 4
#endif

#ifndef CONFIG_AIR360_GPIO_SENSOR_PIN_1
#define CONFIG_AIR360_GPIO_SENSOR_PIN_1 5
#endif

#ifndef CONFIG_AIR360_GPIO_SENSOR_PIN_2
#define CONFIG_AIR360_GPIO_SENSOR_PIN_2 6
#endif

#ifndef CONFIG_AIR360_I2C0_SDA_GPIO
#define CONFIG_AIR360_I2C0_SDA_GPIO 8
#endif

#ifndef CONFIG_AIR360_I2C0_SCL_GPIO
#define CONFIG_AIR360_I2C0_SCL_GPIO 9
#endif

namespace air360 {

namespace {

constexpr char kTag[] = "air360.web";

void copyString(char* destination, std::size_t destination_size, const std::string& source) {
    if (destination_size == 0U) {
        return;
    }

    std::strncpy(destination, source.c_str(), destination_size - 1U);
    destination[destination_size - 1U] = '\0';
}

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

int decodeHex(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

std::string urlDecode(const std::string& input) {
    std::string decoded;
    decoded.reserve(input.size());

    for (std::size_t i = 0; i < input.size(); ++i) {
        const char ch = input[i];
        if (ch == '+') {
            decoded.push_back(' ');
            continue;
        }

        if (ch == '%' && (i + 2U) < input.size()) {
            const int hi = decodeHex(input[i + 1U]);
            const int lo = decodeHex(input[i + 2U]);
            if (hi >= 0 && lo >= 0) {
                decoded.push_back(static_cast<char>((hi << 4) | lo));
                i += 2U;
                continue;
            }
        }

        decoded.push_back(ch);
    }

    return decoded;
}

using FormFields = std::vector<std::pair<std::string, std::string>>;

FormFields parseFormBody(const std::string& body) {
    FormFields fields;
    std::size_t cursor = 0U;

    while (cursor <= body.size()) {
        const std::size_t delimiter = body.find('&', cursor);
        const std::size_t end = delimiter == std::string::npos ? body.size() : delimiter;
        const std::size_t equals = body.find('=', cursor);

        if (equals != std::string::npos && equals < end) {
            fields.emplace_back(
                urlDecode(body.substr(cursor, equals - cursor)),
                urlDecode(body.substr(equals + 1U, end - equals - 1U)));
        } else if (end > cursor) {
            fields.emplace_back(urlDecode(body.substr(cursor, end - cursor)), "");
        }

        if (delimiter == std::string::npos) {
            break;
        }
        cursor = delimiter + 1U;
    }

    return fields;
}

std::string findFormValue(const FormFields& fields, const char* key) {
    for (const auto& [name, value] : fields) {
        if (name == key) {
            return value;
        }
    }
    return "";
}

bool formHasKey(const FormFields& fields, const char* key) {
    for (const auto& [name, value] : fields) {
        if (name == key) {
            static_cast<void>(value);
            return true;
        }
    }
    return false;
}

esp_err_t readRequestBody(httpd_req_t* request, std::string& out_body) {
    out_body.clear();
    if (request->content_len <= 0) {
        return ESP_OK;
    }

    out_body.resize(static_cast<std::size_t>(request->content_len));
    int received_total = 0;
    while (received_total < request->content_len) {
        const int received = httpd_req_recv(
            request,
            out_body.data() + received_total,
            request->content_len - received_total);
        if (received <= 0) {
            return ESP_FAIL;
        }
        received_total += received;
    }

    return ESP_OK;
}

const char* networkModeLabel(NetworkMode mode) {
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

std::string renderConfigPage(
    const DeviceConfig& config,
    const NetworkState& network_state,
    const std::string& notice,
    bool error_notice) {
    std::string html;
    html.reserve(5000);
    html += "<!doctype html><html><head><meta charset='utf-8'>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>air360 config</title>";
    html += "<style>";
    html += "body{font-family:system-ui,sans-serif;margin:2rem;max-width:56rem;line-height:1.5}";
    html += "label{display:block;margin-top:1rem;font-weight:600}";
    html += "input{width:100%;max-width:30rem;padding:.6rem;border:1px solid #cbd5e1;border-radius:.4rem}";
    html += "button{margin-top:1.5rem;padding:.7rem 1.2rem;border:0;border-radius:.5rem;background:#0f766e;color:white;font-weight:700}";
    html += ".card{background:#f8fafc;border:1px solid #e2e8f0;border-radius:.75rem;padding:1rem 1.25rem}";
    html += ".notice{margin:1rem 0;padding:.8rem 1rem;border-radius:.5rem}";
    html += ".ok{background:#ecfdf5;color:#166534}.err{background:#fef2f2;color:#991b1b}";
    html += "code{background:#f1f5f9;padding:.1rem .35rem;border-radius:.25rem}";
    html += "</style></head><body>";
    html += "<h1>air360 Configuration</h1>";
    html += "<p>Mode: <code>";
    html += networkModeLabel(network_state.mode);
    html += "</code> · IP: <code>";
    html += htmlEscape(network_state.ip_address.empty() ? "unavailable" : network_state.ip_address);
    html += "</code></p>";
    if (!network_state.last_error.empty()) {
        html += "<p>Last network error: <code>";
        html += htmlEscape(network_state.last_error);
        html += "</code></p>";
    }

    if (!notice.empty()) {
        html += "<div class='notice ";
        html += error_notice ? "err" : "ok";
        html += "'>";
        html += htmlEscape(notice);
        html += "</div>";
    }

    html += "<div class='card'>";
    html += "<p>This Phase 2 form stores Wi-Fi credentials and basic local device settings. ";
    html += "After save, the firmware reboots and either joins Wi-Fi in station mode or falls back to setup AP mode.</p>";
    html += "<form method='POST' action='/config'>";
    html += "<label for='device_name'>Device name</label>";
    html += "<input id='device_name' name='device_name' maxlength='31' value='";
    html += htmlEscape(config.device_name);
    html += "'>";

    html += "<label for='wifi_ssid'>Wi-Fi SSID</label>";
    html += "<input id='wifi_ssid' name='wifi_ssid' maxlength='32' value='";
    html += htmlEscape(config.wifi_sta_ssid);
    html += "'>";

    html += "<label for='wifi_password'>Wi-Fi password</label>";
    html += "<input id='wifi_password' name='wifi_password' type='password' maxlength='63' value='";
    html += htmlEscape(config.wifi_sta_password);
    html += "'>";
    html += "<p>If Wi-Fi SSID is left empty, the device will reboot back into setup AP mode.</p>";

    html += "<label for='ap_ssid'>Setup AP name</label>";
    html += "<input id='ap_ssid' name='ap_ssid' maxlength='32' value='";
    html += htmlEscape(config.lab_ap_ssid);
    html += "'>";

    html += "<label for='ap_password'>Setup AP password</label>";
    html += "<input id='ap_password' name='ap_password' type='password' maxlength='63' value='";
    html += htmlEscape(config.lab_ap_password);
    html += "'>";

    html += "<label><input name='local_auth_enabled' type='checkbox' ";
    if (config.local_auth_enabled != 0U) {
        html += "checked ";
    }
    html += "style='width:auto;max-width:none;margin-right:.5rem'>Local auth placeholder (stored only, not enforced yet)</label>";
    html += "<button type='submit'>Save and reboot</button>";
    html += "</form></div>";
    html += "<p><a href='/'>Back to root</a> · <a href='/sensors'>Sensors</a> · <a href='/backends'>Backends</a> · <a href='/status'>JSON status</a></p>";
    html += "</body></html>";
    return html;
}

bool parseUnsignedLong(
    const std::string& input,
    unsigned long& value,
    int base = 10) {
    if (input.empty()) {
        return false;
    }

    char* end = nullptr;
    value = std::strtoul(input.c_str(), &end, base);
    return end != nullptr && *end == '\0';
}

bool parseSignedLong(const std::string& input, long& value) {
    if (input.empty()) {
        return false;
    }

    char* end = nullptr;
    value = std::strtol(input.c_str(), &end, 10);
    return end != nullptr && *end == '\0';
}

std::string formatMeasurementValue(const SensorValue& value) {
    char buffer[48];
    std::snprintf(
        buffer,
        sizeof(buffer),
        "%s %.*f",
        sensorValueKindLabel(value.kind),
        sensorValueKindPrecision(value.kind),
        static_cast<double>(value.value));
    std::string text = buffer;
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

TransportKind inferredTransportKind(const SensorDescriptor& descriptor) {
    if (descriptor.supports_i2c) {
        return TransportKind::kI2c;
    }
    if (descriptor.supports_uart) {
        return TransportKind::kUart;
    }
    if (descriptor.supports_gpio) {
        return TransportKind::kGpio;
    }
    if (descriptor.supports_analog) {
        return TransportKind::kAnalog;
    }
    return TransportKind::kUnknown;
}

std::string transportSummaryForRecord(const SensorRecord& record) {
    switch (record.transport_kind) {
        case TransportKind::kI2c: {
            char buffer[32];
            std::snprintf(
                buffer,
                sizeof(buffer),
                "I2C bus %u @ 0x%02x",
                static_cast<unsigned>(record.i2c_bus_id),
                static_cast<unsigned>(record.i2c_address));
            return buffer;
        }
        case TransportKind::kUart: {
            char buffer[64];
            std::snprintf(
                buffer,
                sizeof(buffer),
                "UART %u RX%d TX%d @ %u",
                static_cast<unsigned>(record.uart_port_id),
                static_cast<int>(record.uart_rx_gpio_pin),
                static_cast<int>(record.uart_tx_gpio_pin),
                static_cast<unsigned>(record.uart_baud_rate));
            return buffer;
        }
        case TransportKind::kGpio:
            return std::string("GPIO ") + std::to_string(static_cast<int>(record.analog_gpio_pin));
        case TransportKind::kAnalog:
            return std::string("GPIO ") + std::to_string(static_cast<int>(record.analog_gpio_pin));
        case TransportKind::kUnknown:
        default:
            return "Unknown";
    }
}

void appendBoardGpioOptions(std::string& html, std::int16_t selected_pin) {
    const int pins[] = {
        CONFIG_AIR360_GPIO_SENSOR_PIN_0,
        CONFIG_AIR360_GPIO_SENSOR_PIN_1,
        CONFIG_AIR360_GPIO_SENSOR_PIN_2,
    };

    for (const int pin : pins) {
        html += "<option value='";
        html += std::to_string(pin);
        html += "'";
        if (selected_pin == pin) {
            html += " selected";
        }
        html += ">GPIO ";
        html += std::to_string(pin);
        html += "</option>";
    }
}

std::int16_t defaultBoardGpioPin() {
    return static_cast<std::int16_t>(CONFIG_AIR360_GPIO_SENSOR_PIN_0);
}

std::string sensorDefaultsHint(const SensorDescriptor& descriptor) {
    switch (descriptor.type) {
        case SensorType::kBme280:
            return "Defaults: I2C bus 0 at address 0x77.";
        case SensorType::kBme680:
            return "Defaults: I2C bus 0 at address 0x77. Gas resistance is reported when the heater run is valid.";
        case SensorType::kSps30:
            return "Defaults: I2C bus 0 at address 0x69. Reports PM mass, number concentration, and typical particle size.";
        case SensorType::kEns160:
            return "Defaults: I2C bus 0, currently address 0x52. The driver also probes 0x53 as a fallback.";
        case SensorType::kGpsNmea: {
            std::string hint = "Defaults: fixed UART ";
            hint += std::to_string(CONFIG_AIR360_GPS_DEFAULT_UART_PORT);
            hint += " RX";
            hint += std::to_string(CONFIG_AIR360_GPS_DEFAULT_RX_GPIO);
            hint += " TX";
            hint += std::to_string(CONFIG_AIR360_GPS_DEFAULT_TX_GPIO);
            hint += " @ ";
            hint += std::to_string(CONFIG_AIR360_GPS_DEFAULT_BAUD_RATE);
            hint += " baud.";
            return hint;
        }
        case SensorType::kDht11:
        case SensorType::kDht22:
            return "Defaults: choose one of the board GPIO sensor slots (GPIO 4, 5, or 6).";
        case SensorType::kMe3No2:
            return "Defaults: analog input on one of the board sensor GPIO slots (GPIO 4, 5, or 6). Current driver reports raw ADC and calibrated voltage.";
        case SensorType::kUnknown:
        default:
            return "";
    }
}

std::string formatStatusTime(std::int64_t unix_ms, std::uint64_t uptime_ms) {
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

const BackendRecord* findBackendRecordForDescriptor(
    const BackendConfigList& config,
    const BackendDescriptor& descriptor) {
    return findBackendRecordByType(config, descriptor.type);
}

const BackendStatusSnapshot* findBackendStatusForDescriptor(
    const std::vector<BackendStatusSnapshot>& statuses,
    const BackendDescriptor& descriptor) {
    for (const auto& status : statuses) {
        if (status.backend_type == descriptor.type) {
            return &status;
        }
    }
    return nullptr;
}

std::string renderBackendsPage(
    const BackendConfigList& backend_config_list,
    const UploadManager& upload_manager,
    const std::string& notice,
    bool error_notice) {
    BackendRegistry registry;
    const std::vector<BackendStatusSnapshot> backend_statuses = upload_manager.backends();

    std::string html;
    html.reserve(10000);
    html += "<!doctype html><html><head><meta charset='utf-8'>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>air360 backends</title>";
    html += "<style>";
    html += "body{font-family:system-ui,sans-serif;margin:2rem;max-width:70rem;line-height:1.5}";
    html += "label{display:block;margin-top:.75rem;font-weight:600}";
    html += "button{margin-top:1rem;padding:.65rem 1rem;border:0;border-radius:.45rem;background:#0f766e;color:white;font-weight:700}";
    html += ".card{background:#f8fafc;border:1px solid #e2e8f0;border-radius:.75rem;padding:1rem 1.25rem;margin:1rem 0}";
    html += ".notice{margin:1rem 0;padding:.8rem 1rem;border-radius:.5rem}";
    html += ".ok{background:#ecfdf5;color:#166534}.err{background:#fef2f2;color:#991b1b}";
    html += "code{background:#f1f5f9;padding:.1rem .35rem;border-radius:.25rem}";
    html += "</style></head><body>";
    html += "<h1>air360 Backends</h1>";
    html += "<p>Enabled backends: <code>";
    html += std::to_string(upload_manager.enabledCount());
    html += "</code> · degraded: <code>";
    html += std::to_string(upload_manager.degradedCount());
    html += "</code></p>";

    if (!notice.empty()) {
        html += "<div class='notice ";
        html += error_notice ? "err" : "ok";
        html += "'>";
        html += htmlEscape(notice);
        html += "</div>";
    }

    html += "<form method='POST' action='/backends'>";
    html += "<label for='upload_interval_ms'>Upload interval (ms)</label>";
    html += "<input id='upload_interval_ms' name='upload_interval_ms' inputmode='numeric' value='";
    html += std::to_string(backend_config_list.upload_interval_ms);
    html += "' style='width:100%;max-width:18rem;padding:.55rem;border:1px solid #cbd5e1;border-radius:.35rem'>";
    html += "<p>Allowed range: <code>10000</code> to <code>300000</code> ms.</p>";
    for (std::size_t index = 0; index < registry.descriptorCount(); ++index) {
        const BackendDescriptor& descriptor = registry.descriptors()[index];
        const BackendRecord* record =
            findBackendRecordForDescriptor(backend_config_list, descriptor);
        const BackendStatusSnapshot* status =
            findBackendStatusForDescriptor(backend_statuses, descriptor);
        const bool enabled = record != nullptr && record->enabled != 0U;

        html += "<div class='card'>";
        html += "<h2>";
        html += htmlEscape(descriptor.display_name);
        html += "</h2>";
        html += "<p>Key: <code>";
        html += htmlEscape(descriptor.backend_key);
        html += "</code> · implemented: <code>";
        html += descriptor.implemented ? "true" : "false";
        html += "</code></p>";
        html += "<label><input type='checkbox' name='enabled_";
        html += htmlEscape(descriptor.backend_key);
        html += "' ";
        if (enabled) {
            html += "checked ";
        }
        html += "style='width:auto;max-width:none;margin-right:.5rem'>Enabled</label>";

        if (record != nullptr) {
            html += "<label for='endpoint_";
            html += htmlEscape(descriptor.backend_key);
            html += "'>Endpoint URL</label>";
            html += "<input id='endpoint_";
            html += htmlEscape(descriptor.backend_key);
            html += "' name='endpoint_";
            html += htmlEscape(descriptor.backend_key);
            html += "' value='";
            html += htmlEscape(record->endpoint_url);
            html += "' style='width:100%;max-width:42rem;padding:.55rem;border:1px solid #cbd5e1;border-radius:.35rem'>";

            if (descriptor.type == BackendType::kAir360Api) {
                html += "<label for='token_";
                html += htmlEscape(descriptor.backend_key);
                html += "'>Bearer token</label>";
                html += "<input id='token_";
                html += htmlEscape(descriptor.backend_key);
                html += "' name='token_";
                html += htmlEscape(descriptor.backend_key);
                html += "' value='";
                html += htmlEscape(record->bearer_token);
                html += "' style='width:100%;max-width:42rem;padding:.55rem;border:1px solid #cbd5e1;border-radius:.35rem'>";
            }
        }

        if (status != nullptr) {
            html += "<p>Status: <code>";
            html += htmlEscape(backendRuntimeStateKey(status->state));
            html += "</code> · Result: <code>";
            html += htmlEscape(uploadResultClassKey(status->last_result));
            html += "</code></p>";
            html += "<p>Last attempt: <code>";
            html += htmlEscape(
                formatStatusTime(
                    status->last_attempt_unix_ms,
                    status->last_attempt_uptime_ms));
            html += "</code></p>";
            html += "<p>Last success: <code>";
            html += htmlEscape(
                formatStatusTime(
                    status->last_success_unix_ms,
                    status->last_success_uptime_ms));
            html += "</code></p>";
            html += "<p>HTTP code: <code>";
            html += status->last_http_status > 0 ? std::to_string(status->last_http_status)
                                                 : std::string("n/a");
            html += "</code> · Response time: <code>";
            html += status->last_response_time_ms > 0
                        ? std::to_string(status->last_response_time_ms) + " ms"
                        : std::string("n/a");
            html += "</code> · Retry count: <code>";
            html += std::to_string(status->retry_count);
            html += "</code></p>";
            if (!status->last_error.empty()) {
                html += "<p>Last error: <code>";
                html += htmlEscape(status->last_error);
                html += "</code></p>";
            }
        } else {
            html += "<p>Status: <code>unavailable</code></p>";
        }

        html += "</div>";
    }
    html += "<button type='submit'>Save backend selection</button>";
    html += "</form>";
    html += "<p><a href='/'>Back to root</a> · <a href='/config'>Device config</a> · <a href='/sensors'>Sensors</a> · <a href='/status'>JSON status</a></p>";
    html += "</body></html>";
    return html;
}

std::string renderSensorsPage(
    const SensorConfigList& sensor_config_list,
    const SensorManager& sensor_manager,
    bool has_pending_changes,
    const std::string& notice,
    bool error_notice) {
    SensorRegistry registry;

    std::string html;
    html.reserve(12000);
    html += "<!doctype html><html><head><meta charset='utf-8'>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>air360 sensors</title>";
    html += "<style>";
    html += "body{font-family:system-ui,sans-serif;margin:2rem;max-width:70rem;line-height:1.5}";
    html += "label{display:block;margin-top:.75rem;font-weight:600}";
    html += "input,select{width:100%;max-width:28rem;padding:.55rem;border:1px solid #cbd5e1;border-radius:.35rem}";
    html += "button{margin-top:1rem;padding:.65rem 1rem;border:0;border-radius:.45rem;background:#0f766e;color:white;font-weight:700}";
    html += ".secondary{background:#334155}.danger{background:#b91c1c}";
    html += ".card{background:#f8fafc;border:1px solid #e2e8f0;border-radius:.75rem;padding:1rem 1.25rem;margin:1rem 0}";
    html += ".notice{margin:1rem 0;padding:.8rem 1rem;border-radius:.5rem}";
    html += ".ok{background:#ecfdf5;color:#166534}.err{background:#fef2f2;color:#991b1b}";
    html += "code{background:#f1f5f9;padding:.1rem .35rem;border-radius:.25rem}";
    html += "</style></head><body>";
    html += "<h1>air360 Sensors</h1>";
    html += "<p>Configured sensors: <code>";
    html += std::to_string(sensor_config_list.sensor_count);
    html += "</code> / <code>";
    html += std::to_string(kMaxConfiguredSensors);
    html += "</code></p>";
    html += "<p>Sensor edits are staged in memory only. Use <code>Apply and reboot</code> to persist them.</p>";

    if (!notice.empty()) {
        html += "<div class='notice ";
        html += error_notice ? "err" : "ok";
        html += "'>";
        html += htmlEscape(notice);
        html += "</div>";
    }

    html += "<div class='card'>";
    html += "<h2>Pending sensor changes</h2>";
    html += "<p>Status: <code>";
    html += has_pending_changes ? "pending" : "clean";
    html += "</code></p>";
    html += "<form method='POST' action='/sensors'>";
    html += "<input type='hidden' name='action' value='apply'>";
    html += "<button type='submit'>Apply and reboot</button>";
    html += "</form>";
    html += "<form method='POST' action='/sensors'>";
    html += "<input type='hidden' name='action' value='discard'>";
    html += "<button class='secondary' type='submit'>Discard pending changes</button>";
    html += "</form>";
    html += "</div>";

    const auto runtime_sensors = sensor_manager.sensors();
    if (sensor_config_list.sensor_count == 0U) {
        html += "<p>No sensors configured yet.</p>";
    } else {
        for (std::size_t index = 0; index < sensor_config_list.sensor_count; ++index) {
            const SensorRecord& record = sensor_config_list.sensors[index];
            const SensorDescriptor* descriptor = registry.findByType(record.sensor_type);
            const SensorRuntimeInfo* runtime_info = nullptr;
            for (const auto& sensor : runtime_sensors) {
                if (sensor.id == record.id) {
                    runtime_info = &sensor;
                    break;
                }
            }

            html += "<div class='card'>";
            html += "<h2>";
            html += htmlEscape(record.display_name[0] != '\0' ? record.display_name : "Sensor");
            html += "</h2>";
            html += "<p>Runtime state: <code>";
            html += htmlEscape(
                runtime_info != nullptr ? sensorRuntimeStateKey(runtime_info->state)
                                        : "unknown");
            html += "</code>";
            if (runtime_info != nullptr && !runtime_info->last_error.empty()) {
                html += " · ";
                html += htmlEscape(runtime_info->last_error);
            }
            html += "</p>";
            if (runtime_info != nullptr && !runtime_info->measurement.empty()) {
                html += "<p>Latest reading: <code>";
                html += htmlEscape(measurementSummary(runtime_info->measurement));
                html += "</code></p>";
            }
            html += "<form method='POST' action='/sensors'>";
            html += "<input type='hidden' name='action' value='update'>";
            html += "<input type='hidden' name='sensor_id' value='";
            html += std::to_string(record.id);
            html += "'>";
            html += "<label for='display_name_";
            html += std::to_string(record.id);
            html += "'>Display name</label>";
            html += "<input id='display_name_";
            html += std::to_string(record.id);
            html += "' name='display_name' maxlength='31' value='";
            html += htmlEscape(record.display_name);
            html += "'>";
            html += "<label for='sensor_type_";
            html += std::to_string(record.id);
            html += "'>Sensor type</label>";
            html += "<select id='sensor_type_";
            html += std::to_string(record.id);
            html += "' name='sensor_type'>";
            for (std::size_t descriptor_index = 0; descriptor_index < registry.descriptorCount();
                 ++descriptor_index) {
                const SensorDescriptor& descriptor = registry.descriptors()[descriptor_index];
                html += "<option value='";
                html += htmlEscape(descriptor.type_key);
                html += "'";
                if (descriptor.type == record.sensor_type) {
                    html += " selected";
                }
                html += ">";
                html += htmlEscape(descriptor.display_name);
                html += "</option>";
            }
            html += "</select>";
            html += "<p>Transport: <code>";
            html += htmlEscape(transportSummaryForRecord(record));
            html += "</code></p>";
            if (descriptor != nullptr) {
                const std::string hint = sensorDefaultsHint(*descriptor);
                if (!hint.empty()) {
                    html += "<p>";
                    html += htmlEscape(hint);
                    html += "</p>";
                }
            }
            html += "<label for='poll_interval_ms_";
            html += std::to_string(record.id);
            html += "'>Poll interval (ms)</label>";
            html += "<input id='poll_interval_ms_";
            html += std::to_string(record.id);
            html += "' name='poll_interval_ms' inputmode='numeric' value='";
            html += std::to_string(record.poll_interval_ms);
            html += "'>";
            if (record.transport_kind == TransportKind::kGpio ||
                record.transport_kind == TransportKind::kAnalog) {
                html += "<label for='analog_gpio_pin_";
                html += std::to_string(record.id);
                html += "'>Sensor pin</label>";
                html += "<select id='analog_gpio_pin_";
                html += std::to_string(record.id);
                html += "' name='analog_gpio_pin'>";
                appendBoardGpioOptions(html, record.analog_gpio_pin);
                html += "</select>";
            }
            html += "<label><input name='enabled' type='checkbox' ";
            if (record.enabled != 0U) {
                html += "checked ";
            }
            html += "style='width:auto;max-width:none;margin-right:.5rem'>Enabled</label>";
            html += "<button type='submit'>Stage sensor changes</button>";
            html += "</form>";
            html += "<form method='POST' action='/sensors'>";
            html += "<input type='hidden' name='action' value='delete'>";
            html += "<input type='hidden' name='sensor_id' value='";
            html += std::to_string(record.id);
            html += "'>";
            html += "<button class='danger' type='submit'>Stage sensor deletion</button>";
            html += "</form>";
            html += "</div>";
        }
    }

    html += "<div class='card'>";
    html += "<h2>Add sensor</h2>";
    html += "<form method='POST' action='/sensors'>";
    html += "<input type='hidden' name='action' value='add'>";
    html += "<label for='sensor_type_add'>Sensor type</label>";
    html += "<select id='sensor_type_add' name='sensor_type'>";
    for (std::size_t descriptor_index = 0; descriptor_index < registry.descriptorCount();
         ++descriptor_index) {
        const SensorDescriptor& descriptor = registry.descriptors()[descriptor_index];
        html += "<option value='";
        html += htmlEscape(descriptor.type_key);
        html += "'>";
        html += htmlEscape(descriptor.display_name);
        html += "</option>";
    }
    html += "</select>";
    html += "<label for='display_name_add'>Display name</label>";
    html += "<input id='display_name_add' name='display_name' maxlength='31' value=''>";
    html += "<p>Transport is inferred from sensor type.</p>";
    html += "<p>Type defaults:</p><ul>";
    for (std::size_t descriptor_index = 0; descriptor_index < registry.descriptorCount();
         ++descriptor_index) {
        const SensorDescriptor& descriptor = registry.descriptors()[descriptor_index];
        const std::string hint = sensorDefaultsHint(descriptor);
        if (hint.empty()) {
            continue;
        }
        html += "<li><strong>";
        html += htmlEscape(descriptor.display_name);
        html += ":</strong> ";
        html += htmlEscape(hint);
        html += "</li>";
    }
    html += "</ul>";
    html += "<label for='poll_interval_ms_add'>Poll interval (ms)</label>";
    html += "<input id='poll_interval_ms_add' name='poll_interval_ms' inputmode='numeric' value='10000'>";
    html += "<label for='analog_gpio_pin_add'>Sensor pin (GPIO 4, 5, or 6)</label>";
    html += "<select id='analog_gpio_pin_add' name='analog_gpio_pin'>";
    appendBoardGpioOptions(html, CONFIG_AIR360_GPIO_SENSOR_PIN_0);
    html += "</select>";
    html += "<label><input name='enabled' type='checkbox' checked style='width:auto;max-width:none;margin-right:.5rem'>Enabled</label>";
    html += "<button type='submit'>Stage new sensor</button>";
    html += "</form>";
    html += "</div>";
    html += "<p><a href='/'>Back to root</a> · <a href='/config'>Device config</a> · <a href='/backends'>Backends</a> · <a href='/status'>JSON status</a></p>";
    html += "</body></html>";
    return html;
}

bool validateConfigForm(
    const std::string& device_name,
    const std::string& wifi_ssid,
    const std::string& wifi_password,
    const std::string& ap_ssid,
    const std::string& ap_password,
    std::string& error) {
    if (device_name.empty()) {
        error = "Device name must not be empty.";
        return false;
    }
    if (device_name.size() > 31U) {
        error = "Device name is too long.";
        return false;
    }
    if (wifi_ssid.size() > 32U) {
        error = "Wi-Fi SSID is too long.";
        return false;
    }
    if (wifi_password.size() > 63U) {
        error = "Wi-Fi password is too long.";
        return false;
    }
    if (!wifi_password.empty() && wifi_password.size() < 8U) {
        error = "Wi-Fi password must be empty or at least 8 characters.";
        return false;
    }
    if (ap_ssid.empty()) {
        error = "Setup AP name must not be empty.";
        return false;
    }
    if (ap_ssid.size() > 32U) {
        error = "Setup AP name is too long.";
        return false;
    }
    if (ap_password.size() > 63U) {
        error = "Setup AP password is too long.";
        return false;
    }
    if (!ap_password.empty() && ap_password.size() < 8U) {
        error = "Setup AP password must be empty or at least 8 characters.";
        return false;
    }

    error.clear();
    return true;
}

void restartCallback(void* arg) {
    static_cast<void>(arg);
    esp_restart();
}

void scheduleRestart() {
    static esp_timer_handle_t restart_timer = nullptr;
    if (restart_timer == nullptr) {
        esp_timer_create_args_t args{};
        args.callback = &restartCallback;
        args.name = "air360_reboot";
        ESP_ERROR_CHECK(esp_timer_create(&args, &restart_timer));
    }

    const esp_err_t stop_err = esp_timer_stop(restart_timer);
    if (stop_err != ESP_OK && stop_err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(kTag, "Failed to stop restart timer: %s", esp_err_to_name(stop_err));
    }

    ESP_ERROR_CHECK(esp_timer_start_once(restart_timer, 400000));
}

}  // namespace

esp_err_t WebServer::start(
    StatusService& status_service,
    ConfigRepository& config_repository,
    DeviceConfig& config,
    SensorConfigRepository& sensor_config_repository,
    SensorConfigList& sensor_config_list,
    SensorManager& sensor_manager,
    BackendConfigRepository& backend_config_repository,
    BackendConfigList& backend_config_list,
    UploadManager& upload_manager,
    std::uint16_t port) {
    if (handle_ != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    status_service_ = &status_service;
    config_repository_ = &config_repository;
    config_ = &config;
    sensor_config_repository_ = &sensor_config_repository;
    sensor_config_list_ = &sensor_config_list;
    sensor_manager_ = &sensor_manager;
    backend_config_repository_ = &backend_config_repository;
    backend_config_list_ = &backend_config_list;
    upload_manager_ = &upload_manager;
    staged_sensor_config_ = sensor_config_list;
    has_pending_sensor_changes_ = false;

    httpd_config_t config_httpd = HTTPD_DEFAULT_CONFIG();
    config_httpd.server_port = port;

    esp_err_t err = httpd_start(&handle_, &config_httpd);
    if (err != ESP_OK) {
        return err;
    }

    httpd_uri_t root_uri{};
    root_uri.uri = "/";
    root_uri.method = HTTP_GET;
    root_uri.handler = &WebServer::handleRoot;
    root_uri.user_ctx = this;
    err = httpd_register_uri_handler(handle_, &root_uri);
    if (err != ESP_OK) {
        stop();
        return err;
    }

    httpd_uri_t status_uri{};
    status_uri.uri = "/status";
    status_uri.method = HTTP_GET;
    status_uri.handler = &WebServer::handleStatus;
    status_uri.user_ctx = this;
    err = httpd_register_uri_handler(handle_, &status_uri);
    if (err != ESP_OK) {
        stop();
        return err;
    }

    httpd_uri_t config_get_uri{};
    config_get_uri.uri = "/config";
    config_get_uri.method = HTTP_GET;
    config_get_uri.handler = &WebServer::handleConfig;
    config_get_uri.user_ctx = this;
    err = httpd_register_uri_handler(handle_, &config_get_uri);
    if (err != ESP_OK) {
        stop();
        return err;
    }

    httpd_uri_t config_post_uri{};
    config_post_uri.uri = "/config";
    config_post_uri.method = HTTP_POST;
    config_post_uri.handler = &WebServer::handleConfig;
    config_post_uri.user_ctx = this;
    err = httpd_register_uri_handler(handle_, &config_post_uri);
    if (err != ESP_OK) {
        stop();
        return err;
    }

    httpd_uri_t sensors_get_uri{};
    sensors_get_uri.uri = "/sensors";
    sensors_get_uri.method = HTTP_GET;
    sensors_get_uri.handler = &WebServer::handleSensors;
    sensors_get_uri.user_ctx = this;
    err = httpd_register_uri_handler(handle_, &sensors_get_uri);
    if (err != ESP_OK) {
        stop();
        return err;
    }

    httpd_uri_t sensors_post_uri{};
    sensors_post_uri.uri = "/sensors";
    sensors_post_uri.method = HTTP_POST;
    sensors_post_uri.handler = &WebServer::handleSensors;
    sensors_post_uri.user_ctx = this;
    err = httpd_register_uri_handler(handle_, &sensors_post_uri);
    if (err != ESP_OK) {
        stop();
        return err;
    }

    httpd_uri_t backends_get_uri{};
    backends_get_uri.uri = "/backends";
    backends_get_uri.method = HTTP_GET;
    backends_get_uri.handler = &WebServer::handleBackends;
    backends_get_uri.user_ctx = this;
    err = httpd_register_uri_handler(handle_, &backends_get_uri);
    if (err != ESP_OK) {
        stop();
        return err;
    }

    httpd_uri_t backends_post_uri{};
    backends_post_uri.uri = "/backends";
    backends_post_uri.method = HTTP_POST;
    backends_post_uri.handler = &WebServer::handleBackends;
    backends_post_uri.user_ctx = this;
    err = httpd_register_uri_handler(handle_, &backends_post_uri);
    if (err != ESP_OK) {
        stop();
        return err;
    }

    ESP_LOGI(kTag, "HTTP server listening on port %" PRIu16, port);
    return ESP_OK;
}

esp_err_t WebServer::handleSensors(httpd_req_t* request) {
    auto* server = static_cast<WebServer*>(request->user_ctx);
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");

    if (request->method == HTTP_GET) {
        const std::string html = renderSensorsPage(
            server->staged_sensor_config_,
            *server->sensor_manager_,
            server->has_pending_sensor_changes_,
            "",
            false);
        return httpd_resp_send(request, html.c_str(), html.size());
    }

    std::string body;
    if (readRequestBody(request, body) != ESP_OK) {
        const std::string html = renderSensorsPage(
            server->staged_sensor_config_,
            *server->sensor_manager_,
            server->has_pending_sensor_changes_,
            "Failed to read form body.",
            true);
        return httpd_resp_send(request, html.c_str(), html.size());
    }

    const FormFields fields = parseFormBody(body);
    const std::string action = findFormValue(fields, "action");
    SensorConfigList updated = server->staged_sensor_config_;
    SensorRegistry registry;

    if (action == "apply") {
        const esp_err_t save_err = server->sensor_config_repository_->save(server->staged_sensor_config_);
        if (save_err != ESP_OK) {
            const std::string html = renderSensorsPage(
                server->staged_sensor_config_,
                *server->sensor_manager_,
                server->has_pending_sensor_changes_,
                std::string("Failed to save sensor configuration: ") + esp_err_to_name(save_err),
                true);
            return httpd_resp_send(request, html.c_str(), html.size());
        }

        *server->sensor_config_list_ = server->staged_sensor_config_;
        server->has_pending_sensor_changes_ = false;
        const std::string html = renderSensorsPage(
            server->staged_sensor_config_,
            *server->sensor_manager_,
            server->has_pending_sensor_changes_,
            "Sensor configuration saved. Device is rebooting now.",
            false);
        esp_err_t response_err = httpd_resp_send(request, html.c_str(), html.size());
        if (response_err == ESP_OK) {
            scheduleRestart();
        }
        return response_err;
    } else if (action == "discard") {
        server->staged_sensor_config_ = *server->sensor_config_list_;
        server->has_pending_sensor_changes_ = false;
        const std::string html = renderSensorsPage(
            server->staged_sensor_config_,
            *server->sensor_manager_,
            server->has_pending_sensor_changes_,
            "Pending sensor changes discarded.",
            false);
        return httpd_resp_send(request, html.c_str(), html.size());
    } else if (action == "delete") {
        unsigned long sensor_id = 0UL;
        if (!parseUnsignedLong(findFormValue(fields, "sensor_id"), sensor_id) ||
            !eraseSensorRecordById(updated, static_cast<std::uint32_t>(sensor_id))) {
            const std::string html = renderSensorsPage(
                server->staged_sensor_config_,
                *server->sensor_manager_,
                server->has_pending_sensor_changes_,
                "Failed to delete sensor: invalid sensor id.",
                true);
            return httpd_resp_send(request, html.c_str(), html.size());
        }
    } else if (action == "add" || action == "update") {
        const std::string sensor_type_value = findFormValue(fields, "sensor_type");
        const SensorDescriptor* descriptor = registry.findByTypeKey(sensor_type_value);
        if (descriptor == nullptr) {
            const std::string html = renderSensorsPage(
                server->staged_sensor_config_,
                *server->sensor_manager_,
                server->has_pending_sensor_changes_,
                "Unsupported sensor type.",
                true);
            return httpd_resp_send(request, html.c_str(), html.size());
        }

        unsigned long poll_interval_ms = 0UL;
        if (!parseUnsignedLong(findFormValue(fields, "poll_interval_ms"), poll_interval_ms)) {
            const std::string html = renderSensorsPage(
                server->staged_sensor_config_,
                *server->sensor_manager_,
                server->has_pending_sensor_changes_,
                "Invalid numeric sensor fields.",
                true);
            return httpd_resp_send(request, html.c_str(), html.size());
        }

        const std::string analog_gpio_pin_value = findFormValue(fields, "analog_gpio_pin");
        std::int16_t analog_pin = -1;
        long parsed_signed = -1;
        if (!analog_gpio_pin_value.empty() &&
            !parseSignedLong(analog_gpio_pin_value, parsed_signed)) {
            const std::string html = renderSensorsPage(
                server->staged_sensor_config_,
                *server->sensor_manager_,
                server->has_pending_sensor_changes_,
                "Sensor pin must be a valid integer.",
                true);
            return httpd_resp_send(request, html.c_str(), html.size());
        }
        analog_pin = static_cast<std::int16_t>(parsed_signed);

        SensorRecord record{};
        const SensorRecord* existing = nullptr;
        if (action == "update") {
            unsigned long sensor_id = 0UL;
            if (!parseUnsignedLong(findFormValue(fields, "sensor_id"), sensor_id)) {
                const std::string html = renderSensorsPage(
                    server->staged_sensor_config_,
                    *server->sensor_manager_,
                    server->has_pending_sensor_changes_,
                    "Invalid sensor id.",
                    true);
                return httpd_resp_send(request, html.c_str(), html.size());
            }

            existing = findSensorRecordById(updated, static_cast<std::uint32_t>(sensor_id));
            if (existing == nullptr) {
                const std::string html = renderSensorsPage(
                    server->staged_sensor_config_,
                    *server->sensor_manager_,
                    server->has_pending_sensor_changes_,
                    "Sensor not found.",
                    true);
                return httpd_resp_send(request, html.c_str(), html.size());
            }

            record = *existing;
            record.id = static_cast<std::uint32_t>(sensor_id);
        }

        const bool type_changed = existing == nullptr || existing->sensor_type != descriptor->type;
        if (type_changed) {
            const std::uint32_t preserved_id = record.id;
            const std::uint8_t preserved_enabled = formHasKey(fields, "enabled") ? 1U : 0U;
            SensorRecord rebuilt{};
            rebuilt.id = preserved_id;
            rebuilt.enabled = preserved_enabled;
            rebuilt.uart_baud_rate = CONFIG_AIR360_GPS_DEFAULT_BAUD_RATE;
            rebuilt.analog_gpio_pin = defaultBoardGpioPin();
            record = rebuilt;
        }

        record.enabled = formHasKey(fields, "enabled") ? 1U : 0U;
        record.sensor_type = descriptor->type;
        record.poll_interval_ms = static_cast<std::uint32_t>(poll_interval_ms);
        copyString(
            record.display_name,
            sizeof(record.display_name),
            findFormValue(fields, "display_name"));

        record.transport_kind = inferredTransportKind(*descriptor);
        switch (record.transport_kind) {
            case TransportKind::kI2c:
                if (type_changed) {
                    record.i2c_bus_id = descriptor->default_i2c_bus_id;
                    record.i2c_address = descriptor->default_i2c_address;
                }
                break;
            case TransportKind::kUart:
                record.uart_port_id = CONFIG_AIR360_GPS_DEFAULT_UART_PORT;
                record.uart_rx_gpio_pin = CONFIG_AIR360_GPS_DEFAULT_RX_GPIO;
                record.uart_tx_gpio_pin = CONFIG_AIR360_GPS_DEFAULT_TX_GPIO;
                if (record.uart_baud_rate == 0U) {
                    record.uart_baud_rate = CONFIG_AIR360_GPS_DEFAULT_BAUD_RATE;
                }
                break;
            case TransportKind::kGpio:
            case TransportKind::kAnalog:
                if (!analog_gpio_pin_value.empty()) {
                    record.analog_gpio_pin = analog_pin;
                } else if (type_changed || record.analog_gpio_pin < 0) {
                    record.analog_gpio_pin = defaultBoardGpioPin();
                }
                break;
            case TransportKind::kUnknown:
            default:
                break;
        }

        if (action == "add") {
            if (updated.sensor_count >= kMaxConfiguredSensors) {
                const std::string html = renderSensorsPage(
                    server->staged_sensor_config_,
                    *server->sensor_manager_,
                    server->has_pending_sensor_changes_,
                    "Sensor list is full.",
                    true);
                return httpd_resp_send(request, html.c_str(), html.size());
            }
            record.id = updated.next_sensor_id++;
            updated.sensors[updated.sensor_count++] = record;
        } else {
            *findSensorRecordById(updated, record.id) = record;
        }

        std::string validation_error;
        if (!registry.validateRecord(record, validation_error)) {
            const std::string html = renderSensorsPage(
                server->staged_sensor_config_,
                *server->sensor_manager_,
                server->has_pending_sensor_changes_,
                validation_error,
                true);
            return httpd_resp_send(request, html.c_str(), html.size());
        }
    } else {
        const std::string html = renderSensorsPage(
            server->staged_sensor_config_,
            *server->sensor_manager_,
            server->has_pending_sensor_changes_,
            "Unsupported sensor action.",
            true);
        return httpd_resp_send(request, html.c_str(), html.size());
    }

    server->staged_sensor_config_ = updated;
    server->has_pending_sensor_changes_ = true;
    const std::string html = renderSensorsPage(
        server->staged_sensor_config_,
        *server->sensor_manager_,
        server->has_pending_sensor_changes_,
        action == "delete" ? "Sensor deletion staged." : "Sensor changes staged in memory.",
        false);
    return httpd_resp_send(request, html.c_str(), html.size());
}

esp_err_t WebServer::handleBackends(httpd_req_t* request) {
    auto* server = static_cast<WebServer*>(request->user_ctx);
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");

    if (request->method == HTTP_GET) {
        const std::string html = renderBackendsPage(
            *server->backend_config_list_,
            *server->upload_manager_,
            "",
            false);
        return httpd_resp_send(request, html.c_str(), html.size());
    }

    std::string body;
    if (readRequestBody(request, body) != ESP_OK) {
        const std::string html = renderBackendsPage(
            *server->backend_config_list_,
            *server->upload_manager_,
            "Failed to read form body.",
            true);
        return httpd_resp_send(request, html.c_str(), html.size());
    }

    const FormFields fields = parseFormBody(body);
    BackendRegistry registry;
    BackendConfigList updated = *server->backend_config_list_;

    const std::string upload_interval_value = findFormValue(fields, "upload_interval_ms");
    unsigned long upload_interval_ms = 0UL;
    if (!parseUnsignedLong(upload_interval_value, upload_interval_ms) ||
        upload_interval_ms < 10000UL ||
        upload_interval_ms > 300000UL) {
        const std::string html = renderBackendsPage(
            *server->backend_config_list_,
            *server->upload_manager_,
            "Upload interval must be between 10000 ms and 300000 ms.",
            true);
        return httpd_resp_send(request, html.c_str(), html.size());
    }
    updated.upload_interval_ms = static_cast<std::uint32_t>(upload_interval_ms);

    for (std::size_t index = 0; index < registry.descriptorCount(); ++index) {
        const BackendDescriptor& descriptor = registry.descriptors()[index];
        BackendRecord* record = findBackendRecordByType(updated, descriptor.type);
        if (record == nullptr) {
            const std::string html = renderBackendsPage(
                *server->backend_config_list_,
                *server->upload_manager_,
                "Backend configuration is incomplete.",
                true);
            return httpd_resp_send(request, html.c_str(), html.size());
        }

        const std::string checkbox_name = std::string("enabled_") + descriptor.backend_key;
        record->enabled = formHasKey(fields, checkbox_name.c_str()) ? 1U : 0U;

        const std::string endpoint_name = std::string("endpoint_") + descriptor.backend_key;
        copyString(
            record->endpoint_url,
            sizeof(record->endpoint_url),
            findFormValue(fields, endpoint_name.c_str()));

        if (descriptor.type == BackendType::kAir360Api) {
            const std::string token_name = std::string("token_") + descriptor.backend_key;
            copyString(
                record->bearer_token,
                sizeof(record->bearer_token),
                findFormValue(fields, token_name.c_str()));
        }
    }

    const esp_err_t save_err = server->backend_config_repository_->save(updated);
    if (save_err != ESP_OK) {
        const std::string html = renderBackendsPage(
            *server->backend_config_list_,
            *server->upload_manager_,
            std::string("Failed to save backend configuration: ") + esp_err_to_name(save_err),
            true);
        return httpd_resp_send(request, html.c_str(), html.size());
    }

    *server->backend_config_list_ = updated;
    server->upload_manager_->applyConfig(updated);
    server->status_service_->setUploads(*server->upload_manager_);

    const std::string html = renderBackendsPage(
        *server->backend_config_list_,
        *server->upload_manager_,
        "Backend selection saved.",
        false);
    return httpd_resp_send(request, html.c_str(), html.size());
}

void WebServer::stop() {
    if (handle_ != nullptr) {
        httpd_stop(handle_);
        handle_ = nullptr;
    }
}

esp_err_t WebServer::handleRoot(httpd_req_t* request) {
    auto* server = static_cast<WebServer*>(request->user_ctx);
    if (server->status_service_->networkState().mode == NetworkMode::kSetupAp) {
        httpd_resp_set_status(request, "302 Found");
        httpd_resp_set_hdr(request, "Location", "/config");
        return httpd_resp_send(request, nullptr, 0);
    }

    const std::string html = server->status_service_->renderRootHtml();
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, html.c_str(), html.size());
}

esp_err_t WebServer::handleStatus(httpd_req_t* request) {
    auto* server = static_cast<WebServer*>(request->user_ctx);
    const std::string json = server->status_service_->renderStatusJson();
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, json.c_str(), json.size());
}

esp_err_t WebServer::handleConfig(httpd_req_t* request) {
    auto* server = static_cast<WebServer*>(request->user_ctx);
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");

    if (request->method == HTTP_GET) {
        const std::string html = renderConfigPage(
            *server->config_,
            server->status_service_->networkState(),
            "",
            false);
        return httpd_resp_send(request, html.c_str(), html.size());
    }

    std::string body;
    if (readRequestBody(request, body) != ESP_OK) {
        const std::string html = renderConfigPage(
            *server->config_,
            server->status_service_->networkState(),
            "Failed to read form body.",
            true);
        return httpd_resp_send(request, html.c_str(), html.size());
    }

    const FormFields fields = parseFormBody(body);
    const std::string device_name = findFormValue(fields, "device_name");
    const std::string wifi_ssid = findFormValue(fields, "wifi_ssid");
    const std::string wifi_password = findFormValue(fields, "wifi_password");
    const std::string ap_ssid = findFormValue(fields, "ap_ssid");
    const std::string ap_password = findFormValue(fields, "ap_password");

    std::string validation_error;
    if (!validateConfigForm(
            device_name,
            wifi_ssid,
            wifi_password,
            ap_ssid,
            ap_password,
            validation_error)) {
        DeviceConfig preview = *server->config_;
        copyString(preview.device_name, sizeof(preview.device_name), device_name);
        copyString(preview.wifi_sta_ssid, sizeof(preview.wifi_sta_ssid), wifi_ssid);
        copyString(preview.wifi_sta_password, sizeof(preview.wifi_sta_password), wifi_password);
        copyString(preview.lab_ap_ssid, sizeof(preview.lab_ap_ssid), ap_ssid);
        copyString(preview.lab_ap_password, sizeof(preview.lab_ap_password), ap_password);
        preview.local_auth_enabled = formHasKey(fields, "local_auth_enabled") ? 1U : 0U;

        const std::string html = renderConfigPage(
            preview,
            server->status_service_->networkState(),
            validation_error,
            true);
        return httpd_resp_send(request, html.c_str(), html.size());
    }

    DeviceConfig updated = *server->config_;
    updated.magic = kDeviceConfigMagic;
    updated.schema_version = kDeviceConfigSchemaVersion;
    updated.record_size = static_cast<std::uint16_t>(sizeof(DeviceConfig));
    updated.local_auth_enabled = formHasKey(fields, "local_auth_enabled") ? 1U : 0U;
    copyString(updated.device_name, sizeof(updated.device_name), device_name);
    copyString(updated.wifi_sta_ssid, sizeof(updated.wifi_sta_ssid), wifi_ssid);
    copyString(updated.wifi_sta_password, sizeof(updated.wifi_sta_password), wifi_password);
    copyString(updated.lab_ap_ssid, sizeof(updated.lab_ap_ssid), ap_ssid);
    copyString(updated.lab_ap_password, sizeof(updated.lab_ap_password), ap_password);

    const esp_err_t save_err = server->config_repository_->save(updated);
    if (save_err != ESP_OK) {
        const std::string html = renderConfigPage(
            updated,
            server->status_service_->networkState(),
            std::string("Failed to save configuration: ") + esp_err_to_name(save_err),
            true);
        return httpd_resp_send(request, html.c_str(), html.size());
    }

    *server->config_ = updated;
    server->status_service_->setConfig(updated, true, false);
    const std::string html = renderConfigPage(
        updated,
        server->status_service_->networkState(),
        "Configuration saved. Device is rebooting now.",
        false);
    esp_err_t response_err = httpd_resp_send(request, html.c_str(), html.size());
    if (response_err == ESP_OK) {
        scheduleRestart();
    }
    return response_err;
}

}  // namespace air360
