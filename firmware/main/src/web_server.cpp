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
#include "air360/web_assets.hpp"
#include "air360/web_ui.hpp"
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
constexpr std::size_t kHttpServerStackSize = 10240U;
constexpr std::size_t kHttpServerMaxUriHandlers = 12U;

void copyString(char* destination, std::size_t destination_size, const std::string& source) {
    if (destination_size == 0U) {
        return;
    }

    std::strncpy(destination, source.c_str(), destination_size - 1U);
    destination[destination_size - 1U] = '\0';
}

std::string boundedCString(const char* value, std::size_t capacity) {
    if (value == nullptr || capacity == 0U) {
        return "";
    }

    std::size_t length = 0U;
    while (length < capacity && value[length] != '\0') {
        ++length;
    }

    return std::string(value, length);
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

esp_err_t sendHtmlResponse(httpd_req_t* request, const std::string& html) {
    constexpr std::size_t kChunkSize = 1024U;

    for (std::size_t offset = 0; offset < html.size(); offset += kChunkSize) {
        const std::size_t remaining = html.size() - offset;
        const std::size_t chunk_size = remaining < kChunkSize ? remaining : kChunkSize;
        const esp_err_t err =
            httpd_resp_send_chunk(request, html.data() + offset, chunk_size);
        if (err != ESP_OK) {
            return err;
        }
    }

    return httpd_resp_send_chunk(request, nullptr, 0);
}

std::string_view assetPathFromUri(const char* uri) {
    if (uri == nullptr) {
        return {};
    }

    std::string_view path(uri);
    constexpr std::string_view kPrefix = "/assets/";
    if (path.size() < kPrefix.size() || path.substr(0, kPrefix.size()) != kPrefix) {
        return {};
    }

    path.remove_prefix(kPrefix.size());
    const std::size_t query = path.find('?');
    if (query != std::string_view::npos) {
        path = path.substr(0, query);
    }
    return path;
}

esp_err_t sendAssetResponse(httpd_req_t* request, std::string_view asset_path) {
    const WebAssetView* asset = findEmbeddedWebAsset(asset_path);
    if (asset == nullptr || asset->data == nullptr) {
        httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "Asset not found");
        return ESP_ERR_NOT_FOUND;
    }

    httpd_resp_set_type(request, asset->content_type);
    httpd_resp_set_hdr(request, "Cache-Control", "public, max-age=604800, immutable");
    return httpd_resp_send(request, asset->data, asset->size);
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

struct ConfigPageViewModel {
    std::string network_mode;
    std::string ip_address;
    std::string device_name;
    std::string local_auth_state;
    std::string notice_html;
    std::string network_error_html;
    std::string wifi_ssid_value;
    std::string wifi_password_value;
    std::string ap_ssid_value;
    std::string ap_password_value;
    bool local_auth_checked = false;
};

struct BackendCardViewModel {
    std::string display_name;
    std::string backend_key;
    bool implemented = false;
    bool enabled = false;
    std::string endpoint;
    bool show_device_id_override = false;
    std::string device_id_override;
    bool show_bearer_token = false;
    std::string bearer_token;
    bool has_status = false;
    std::string state_key;
    std::string result_key;
    std::uint32_t retry_count = 0U;
    std::string last_attempt;
    std::string last_success;
    int last_http_status = 0;
    std::uint32_t last_response_time_ms = 0U;
    std::string last_error;
};

struct BackendsPageViewModel {
    std::size_t enabled_count = 0U;
    std::size_t degraded_count = 0U;
    std::uint32_t upload_interval_ms = 0U;
    std::size_t configured_backends_count = 0U;
    std::string notice_html;
    std::vector<BackendCardViewModel> cards;
};

struct SensorCardViewModel {
    std::uint32_t id = 0U;
    std::string display_name;
    std::string runtime_state_key;
    std::string transport_summary;
    std::uint32_t poll_interval_ms = 0U;
    std::string runtime_error;
    std::string latest_reading;
    std::string sensor_type_options_html;
    std::string defaults_hint;
    bool show_gpio_pin_select = false;
    std::string gpio_options_html;
    bool enabled = false;
};

struct SensorsPageViewModel {
    std::size_t configured_count = 0U;
    std::size_t max_sensors = 0U;
    bool has_pending_changes = false;
    std::size_t runtime_sensor_count = 0U;
    std::size_t free_slots = 0U;
    std::string notice_html;
    std::vector<SensorCardViewModel> cards;
    std::string sensor_type_options_html;
    std::string selected_sensor_defaults;
    std::string sensor_defaults_list_html;
    std::string gpio_options_html;
};

ConfigPageViewModel buildConfigPageViewModel(
    const DeviceConfig& config,
    const NetworkState& network_state,
    const std::string& notice,
    bool error_notice);
std::string renderBackendCard(const BackendCardViewModel& card);
BackendsPageViewModel buildBackendsPageViewModel(
    const BackendConfigList& backend_config_list,
    const UploadManager& upload_manager,
    const BuildInfo& build_info,
    const std::string& notice,
    bool error_notice);
std::string renderSensorCard(const SensorCardViewModel& card);
SensorsPageViewModel buildSensorsPageViewModel(
    const SensorConfigList& sensor_config_list,
    const SensorManager& sensor_manager,
    bool has_pending_changes,
    const std::string& notice,
    bool error_notice);
const BackendRecord* findBackendRecordForDescriptor(
    const BackendConfigList& config,
    const BackendDescriptor& descriptor);

std::string renderConfigPage(
    const DeviceConfig& config,
    const NetworkState& network_state,
    const std::string& notice,
    bool error_notice) {
    const ConfigPageViewModel model =
        buildConfigPageViewModel(config, network_state, notice, error_notice);
    const std::string body = renderPageTemplate(
        WebTemplateKey::kConfig,
        WebTemplateBindings{
            {"NETWORK_MODE", htmlEscape(model.network_mode)},
            {"IP_ADDRESS", htmlEscape(model.ip_address)},
            {"DEVICE_NAME", htmlEscape(model.device_name)},
            {"LOCAL_AUTH_STATE", model.local_auth_state},
            {"NOTICE", model.notice_html},
            {"NETWORK_ERROR", model.network_error_html},
            {"DEVICE_NAME_VALUE", htmlEscape(model.device_name)},
            {"WIFI_SSID_VALUE", htmlEscape(model.wifi_ssid_value)},
            {"WIFI_PASSWORD_VALUE", htmlEscape(model.wifi_password_value)},
            {"AP_SSID_VALUE", htmlEscape(model.ap_ssid_value)},
            {"AP_PASSWORD_VALUE", htmlEscape(model.ap_password_value)},
            {"LOCAL_AUTH_CHECKED", model.local_auth_checked ? "checked" : ""},
        });

    return renderPageDocument(
        WebPageKey::kConfig,
        "air360 device configuration",
        "Device Configuration",
        "Manage station Wi-Fi, setup AP fallback, and local device identity without leaving the firmware UI.",
        body,
        false);
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
            return "Defaults: I2C bus 0 at address 0x76.";
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

std::string sensorTypeOptionHtml(const SensorDescriptor& descriptor, bool selected) {
    std::string html;
    html += "<option value='";
    html += htmlEscape(descriptor.type_key);
    html += "'";
    if (selected) {
        html += " selected";
    }
    html += " data-requires-pin='";
    html += (descriptor.supports_gpio || descriptor.supports_analog) ? "true" : "false";
    html += "' data-defaults-hint='";
    html += htmlEscape(sensorDefaultsHint(descriptor));
    html += "'>";
    html += htmlEscape(descriptor.display_name);
    html += "</option>";
    return html;
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

ConfigPageViewModel buildConfigPageViewModel(
    const DeviceConfig& config,
    const NetworkState& network_state,
    const std::string& notice,
    bool error_notice) {
    ConfigPageViewModel model;
    model.network_mode = networkModeLabel(network_state.mode);
    model.ip_address = network_state.ip_address.empty() ? "unavailable" : network_state.ip_address;
    model.device_name = config.device_name;
    model.local_auth_state = config.local_auth_enabled != 0U ? "Stored" : "Disabled";
    model.notice_html = renderNotice(notice, error_notice);
    model.wifi_ssid_value = config.wifi_sta_ssid;
    model.wifi_password_value = config.wifi_sta_password;
    model.ap_ssid_value = config.lab_ap_ssid;
    model.ap_password_value = config.lab_ap_password;
    model.local_auth_checked = config.local_auth_enabled != 0U;

    if (!network_state.last_error.empty()) {
        model.network_error_html += "<p>Last network error: <code>";
        model.network_error_html += htmlEscape(network_state.last_error);
        model.network_error_html += "</code></p>";
    }

    return model;
}

std::string renderBackendCard(const BackendCardViewModel& card) {
    std::string endpoint_block;
    if (!card.endpoint.empty()) {
        endpoint_block += "<p>Endpoint: <code>";
        endpoint_block += htmlEscape(card.endpoint);
        endpoint_block += "</code></p>";
    }

    std::string device_id_override_block;
    if (card.show_device_id_override) {
        device_id_override_block += "<div class='field'><label for='device_id_";
        device_id_override_block += htmlEscape(card.backend_key);
        device_id_override_block += "'>Device ID override</label>";
        device_id_override_block += "<input class='input' id='device_id_";
        device_id_override_block += htmlEscape(card.backend_key);
        device_id_override_block += "' name='device_id_";
        device_id_override_block += htmlEscape(card.backend_key);
        device_id_override_block += "' maxlength='";
        device_id_override_block += std::to_string(kBackendIdentifierCapacity - 1U);
        device_id_override_block += "' value='";
        device_id_override_block += htmlEscape(card.device_id_override);
        device_id_override_block += "'>";
        device_id_override_block += "<p class='hint'>Prefilled from Short ID. Change it only for debugging.</p></div>";
    }

    std::string bearer_token_block;
    if (card.show_bearer_token) {
        bearer_token_block += "<div class='field'><label for='token_";
        bearer_token_block += htmlEscape(card.backend_key);
        bearer_token_block += "'>Bearer token</label>";
        bearer_token_block += "<div class='secret-row'><input class='input' id='token_";
        bearer_token_block += htmlEscape(card.backend_key);
        bearer_token_block += "' name='token_";
        bearer_token_block += htmlEscape(card.backend_key);
        bearer_token_block += "' type='password' value='";
        bearer_token_block += htmlEscape(card.bearer_token);
        bearer_token_block += "'><button class='button button--ghost' type='button' data-secret-toggle='token_";
        bearer_token_block += htmlEscape(card.backend_key);
        bearer_token_block += "'>Show</button></div></div>";
    }

    std::string status_block;
    if (card.has_status) {
        status_block += "<div class='meta'><span class='pill'>State <code>";
        status_block += htmlEscape(card.state_key);
        status_block += "</code></span><span class='pill'>Result <code>";
        status_block += htmlEscape(card.result_key);
        status_block += "</code></span><span class='pill'>Retry ";
        status_block += std::to_string(card.retry_count);
        status_block += "</span></div>";
        status_block += "<p>Last attempt: <code>";
        status_block += htmlEscape(card.last_attempt);
        status_block += "</code></p>";
        status_block += "<p>Last success: <code>";
        status_block += htmlEscape(card.last_success);
        status_block += "</code></p>";
        status_block += "<p>HTTP code: <code>";
        status_block += card.last_http_status > 0 ? std::to_string(card.last_http_status)
                                                  : std::string("n/a");
        status_block += "</code> · Response time: <code>";
        status_block += card.last_response_time_ms > 0
                            ? std::to_string(card.last_response_time_ms) + " ms"
                            : std::string("n/a");
        status_block += "</code></p>";
        if (!card.last_error.empty()) {
            status_block += "<p>Last error: <code>";
            status_block += htmlEscape(card.last_error);
            status_block += "</code></p>";
        }
    } else {
        status_block = "<p>Status: <code>unavailable</code></p>";
    }

    return renderTemplate(
        WebTemplateKey::kBackendCard,
        WebTemplateBindings{
            {"DISPLAY_NAME", htmlEscape(card.display_name)},
            {"BACKEND_KEY", htmlEscape(card.backend_key)},
            {"BACKEND_KEY_ATTR", htmlEscape(card.backend_key)},
            {"IMPLEMENTED", card.implemented ? "true" : "false"},
            {"ENABLED_CHECKED", card.enabled ? "checked" : ""},
            {"ENDPOINT_BLOCK", endpoint_block},
            {"DEVICE_ID_OVERRIDE_BLOCK", device_id_override_block},
            {"BEARER_TOKEN_BLOCK", bearer_token_block},
            {"STATUS_BLOCK", status_block},
        });
}

BackendsPageViewModel buildBackendsPageViewModel(
    const BackendConfigList& backend_config_list,
    const UploadManager& upload_manager,
    const BuildInfo& build_info,
    const std::string& notice,
    bool error_notice) {
    BackendsPageViewModel model;
    BackendRegistry registry;

    model.enabled_count = upload_manager.enabledCount();
    model.degraded_count = upload_manager.degradedCount();
    model.upload_interval_ms = backend_config_list.upload_interval_ms;
    model.configured_backends_count = registry.descriptorCount();
    model.notice_html = renderNotice(notice, error_notice);
    model.cards.reserve(registry.descriptorCount());

    for (std::size_t index = 0; index < registry.descriptorCount(); ++index) {
        const BackendDescriptor& descriptor = registry.descriptors()[index];
        const BackendRecord* record =
            findBackendRecordForDescriptor(backend_config_list, descriptor);
        BackendStatusSnapshot status_storage{};
        const BackendStatusSnapshot* status =
            upload_manager.backendStatus(descriptor.type, status_storage) ? &status_storage
                                                                          : nullptr;

        BackendCardViewModel card;
        card.display_name = descriptor.display_name;
        card.backend_key = descriptor.backend_key;
        card.implemented = descriptor.implemented;
        card.enabled = record != nullptr && record->enabled != 0U;
        if (record != nullptr) {
            card.endpoint = backendDefaultEndpointUrl(descriptor.type);
            card.show_device_id_override = descriptor.type == BackendType::kSensorCommunity;
            if (card.show_device_id_override) {
                card.device_id_override =
                    boundedCString(record->device_id_override, sizeof(record->device_id_override));
                if (card.device_id_override.empty()) {
                    card.device_id_override = build_info.short_chip_id;
                }
            }
            card.show_bearer_token = descriptor.type == BackendType::kAir360Api;
            if (card.show_bearer_token) {
                card.bearer_token =
                    boundedCString(record->bearer_token, sizeof(record->bearer_token));
            }
        }

        if (status != nullptr) {
            card.has_status = true;
            card.state_key = backendRuntimeStateKey(status->state);
            card.result_key = uploadResultClassKey(status->last_result);
            card.retry_count = status->retry_count;
            card.last_attempt = formatStatusTime(
                status->last_attempt_unix_ms,
                status->last_attempt_uptime_ms);
            card.last_success = formatStatusTime(
                status->last_success_unix_ms,
                status->last_success_uptime_ms);
            card.last_http_status = status->last_http_status;
            card.last_response_time_ms = status->last_response_time_ms;
            card.last_error = status->last_error;
        }

        model.cards.push_back(std::move(card));
    }

    return model;
}

std::string renderSensorCard(const SensorCardViewModel& card) {
    std::string runtime_error_block;
    if (!card.runtime_error.empty()) {
        runtime_error_block += "<p>Runtime error: <code>";
        runtime_error_block += htmlEscape(card.runtime_error);
        runtime_error_block += "</code></p>";
    }

    std::string latest_reading_block;
    if (!card.latest_reading.empty()) {
        latest_reading_block += "<p>Latest reading: <code>";
        latest_reading_block += htmlEscape(card.latest_reading);
        latest_reading_block += "</code></p>";
    }

    std::string gpio_field_block;
    if (card.show_gpio_pin_select) {
        gpio_field_block += "<div class='field' data-sensor-pin-field><label for='analog_gpio_pin_";
        gpio_field_block += std::to_string(card.id);
        gpio_field_block += "'>Sensor pin</label>";
        gpio_field_block += "<select class='select' id='analog_gpio_pin_";
        gpio_field_block += std::to_string(card.id);
        gpio_field_block += "' name='analog_gpio_pin'>";
        gpio_field_block += card.gpio_options_html;
        gpio_field_block += "</select></div>";
    }

    return renderTemplate(
        WebTemplateKey::kSensorCard,
        WebTemplateBindings{
            {"DISPLAY_NAME", htmlEscape(card.display_name.empty() ? "Sensor" : card.display_name)},
            {"DISPLAY_NAME_VALUE", htmlEscape(card.display_name)},
            {"RUNTIME_STATE", htmlEscape(card.runtime_state_key)},
            {"TRANSPORT_SUMMARY", htmlEscape(card.transport_summary)},
            {"POLL_INTERVAL_MS", std::to_string(card.poll_interval_ms)},
            {"RUNTIME_ERROR_BLOCK", runtime_error_block},
            {"LATEST_READING_BLOCK", latest_reading_block},
            {"SENSOR_ID", std::to_string(card.id)},
            {"SENSOR_TYPE_OPTIONS", card.sensor_type_options_html},
            {"DEFAULTS_HINT_TEXT", htmlEscape(card.defaults_hint)},
            {"DEFAULTS_HINT_HIDDEN", card.defaults_hint.empty() ? "hidden" : ""},
            {"GPIO_FIELD_BLOCK", gpio_field_block},
            {"ENABLED_CHECKED", card.enabled ? "checked" : ""},
        });
}

SensorsPageViewModel buildSensorsPageViewModel(
    const SensorConfigList& sensor_config_list,
    const SensorManager& sensor_manager,
    bool has_pending_changes,
    const std::string& notice,
    bool error_notice) {
    SensorsPageViewModel model;
    SensorRegistry registry;
    const auto runtime_sensors = sensor_manager.sensors();

    model.configured_count = sensor_config_list.sensor_count;
    model.max_sensors = kMaxConfiguredSensors;
    model.has_pending_changes = has_pending_changes;
    model.runtime_sensor_count = runtime_sensors.size();
    model.free_slots = kMaxConfiguredSensors - sensor_config_list.sensor_count;
    model.notice_html = renderNotice(notice, error_notice);
    model.cards.reserve(sensor_config_list.sensor_count);

    for (std::size_t descriptor_index = 0; descriptor_index < registry.descriptorCount();
         ++descriptor_index) {
        const SensorDescriptor& descriptor = registry.descriptors()[descriptor_index];
        model.sensor_type_options_html += sensorTypeOptionHtml(descriptor, descriptor_index == 0U);
        const std::string hint = sensorDefaultsHint(descriptor);
        if (descriptor_index == 0U) {
            model.selected_sensor_defaults = hint;
        }
        if (!hint.empty()) {
            model.sensor_defaults_list_html += "<li><strong>";
            model.sensor_defaults_list_html += htmlEscape(descriptor.display_name);
            model.sensor_defaults_list_html += ":</strong> ";
            model.sensor_defaults_list_html += htmlEscape(hint);
            model.sensor_defaults_list_html += "</li>";
        }
    }

    appendBoardGpioOptions(model.gpio_options_html, CONFIG_AIR360_GPIO_SENSOR_PIN_0);

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

        SensorCardViewModel card;
        card.id = record.id;
        card.display_name = record.display_name;
        card.runtime_state_key =
            runtime_info != nullptr ? sensorRuntimeStateKey(runtime_info->state) : "unknown";
        card.transport_summary = transportSummaryForRecord(record);
        card.poll_interval_ms = record.poll_interval_ms;
        card.enabled = record.enabled != 0U;
        if (runtime_info != nullptr) {
            card.runtime_error = runtime_info->last_error;
            if (!runtime_info->measurement.empty()) {
                card.latest_reading = measurementSummary(runtime_info->measurement);
            }
        }

        for (std::size_t descriptor_index = 0; descriptor_index < registry.descriptorCount();
             ++descriptor_index) {
            const SensorDescriptor& option_descriptor = registry.descriptors()[descriptor_index];
            card.sensor_type_options_html +=
                sensorTypeOptionHtml(option_descriptor, option_descriptor.type == record.sensor_type);
        }

        if (descriptor != nullptr) {
            card.defaults_hint = sensorDefaultsHint(*descriptor);
        }
        card.show_gpio_pin_select =
            record.transport_kind == TransportKind::kGpio ||
            record.transport_kind == TransportKind::kAnalog;
        if (card.show_gpio_pin_select) {
            appendBoardGpioOptions(card.gpio_options_html, record.analog_gpio_pin);
        }

        model.cards.push_back(std::move(card));
    }

    return model;
}

const BackendRecord* findBackendRecordForDescriptor(
    const BackendConfigList& config,
    const BackendDescriptor& descriptor) {
    return findBackendRecordByType(config, descriptor.type);
}

std::string renderBackendsPage(
    const BackendConfigList& backend_config_list,
    const UploadManager& upload_manager,
    const BuildInfo& build_info,
    const std::string& notice,
    bool error_notice) {
    const BackendsPageViewModel model =
        buildBackendsPageViewModel(
            backend_config_list,
            upload_manager,
            build_info,
            notice,
            error_notice);

    std::string backend_cards;
    backend_cards.reserve(model.cards.size() * 1400U);
    for (const auto& card : model.cards) {
        backend_cards += renderBackendCard(card);
    }

    const std::string body = renderPageTemplate(
        WebTemplateKey::kBackends,
        WebTemplateBindings{
            {"ENABLED_COUNT", std::to_string(model.enabled_count)},
            {"DEGRADED_COUNT", std::to_string(model.degraded_count)},
            {"UPLOAD_INTERVAL", std::to_string(model.upload_interval_ms)},
            {"CONFIGURED_BACKENDS_COUNT", std::to_string(model.configured_backends_count)},
            {"NOTICE", model.notice_html},
            {"UPLOAD_INTERVAL_VALUE", std::to_string(model.upload_interval_ms)},
            {"BACKEND_CARDS", backend_cards},
        });
    return renderPageDocument(
        WebPageKey::kBackends,
        "air360 upload backends",
        "Upload Backends",
        "Tune upload cadence, enable or disable integrations, and inspect backend runtime state directly on the device.",
        body);
}

std::string renderSensorsPage(
    const SensorConfigList& sensor_config_list,
    const SensorManager& sensor_manager,
    bool has_pending_changes,
    const std::string& notice,
    bool error_notice) {
    const SensorsPageViewModel model =
        buildSensorsPageViewModel(
            sensor_config_list,
            sensor_manager,
            has_pending_changes,
            notice,
            error_notice);

    std::string configured_sensors_block;
    configured_sensors_block.reserve(model.cards.size() * 2600U);

    if (model.cards.empty()) {
        configured_sensors_block += "<div class='panel'><h2>Configured Sensors</h2><p class='muted'>No sensors configured yet.</p></div>";
    } else {
        for (const auto& card : model.cards) {
            configured_sensors_block += renderSensorCard(card);
        }
    }
    const std::string body = renderPageTemplate(
        WebTemplateKey::kSensors,
        WebTemplateBindings{
            {"CONFIGURED_COUNT", std::to_string(model.configured_count)},
            {"MAX_SENSORS", std::to_string(model.max_sensors)},
            {"PENDING_STATUS", model.has_pending_changes ? "Pending" : "Clean"},
            {"PENDING_STATUS_LOWER", model.has_pending_changes ? "pending" : "clean"},
            {"RUNTIME_SENSOR_COUNT", std::to_string(model.runtime_sensor_count)},
            {"FREE_SLOTS", std::to_string(model.free_slots)},
            {"NOTICE", model.notice_html},
            {"CONFIGURED_SENSORS_BLOCK", configured_sensors_block},
            {"SENSOR_TYPE_OPTIONS", model.sensor_type_options_html},
            {"SELECTED_SENSOR_DEFAULTS", htmlEscape(model.selected_sensor_defaults)},
            {"SENSOR_DEFAULTS_LIST", model.sensor_defaults_list_html},
            {"GPIO_OPTIONS", model.gpio_options_html},
        });
    return renderPageDocument(
        WebPageKey::kSensors,
        "air360 sensors",
        "Sensor Configuration",
        "Stage sensor changes locally, inspect live runtime state, then apply the final set with a reboot when you are ready.",
        body);
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
    config_httpd.stack_size = kHttpServerStackSize;
    config_httpd.max_uri_handlers = kHttpServerMaxUriHandlers;
    config_httpd.uri_match_fn = httpd_uri_match_wildcard;

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

    httpd_uri_t asset_uri{};
    asset_uri.uri = "/assets/*";
    asset_uri.method = HTTP_GET;
    asset_uri.handler = &WebServer::handleAsset;
    asset_uri.user_ctx = this;
    err = httpd_register_uri_handler(handle_, &asset_uri);
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

esp_err_t WebServer::handleAsset(httpd_req_t* request) {
    return sendAssetResponse(request, assetPathFromUri(request->uri));
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
                record.uart_port_id = descriptor->default_uart_port_id;
                record.uart_rx_gpio_pin = descriptor->default_uart_rx_gpio_pin;
                record.uart_tx_gpio_pin = descriptor->default_uart_tx_gpio_pin;
                if (type_changed || record.uart_baud_rate == 0U) {
                    record.uart_baud_rate = descriptor->default_uart_baud_rate;
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
            server->status_service_->buildInfo(),
            "",
            false);
        return sendHtmlResponse(request, html);
    }

    std::string body;
    if (readRequestBody(request, body) != ESP_OK) {
        const std::string html = renderBackendsPage(
            *server->backend_config_list_,
            *server->upload_manager_,
            server->status_service_->buildInfo(),
            "Failed to read form body.",
            true);
        return sendHtmlResponse(request, html);
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
            server->status_service_->buildInfo(),
            "Upload interval must be between 10000 ms and 300000 ms.",
            true);
        return sendHtmlResponse(request, html);
    }
    updated.upload_interval_ms = static_cast<std::uint32_t>(upload_interval_ms);

    for (std::size_t index = 0; index < registry.descriptorCount(); ++index) {
        const BackendDescriptor& descriptor = registry.descriptors()[index];
        BackendRecord* record = findBackendRecordByType(updated, descriptor.type);
        if (record == nullptr) {
            const std::string html = renderBackendsPage(
                *server->backend_config_list_,
                *server->upload_manager_,
                server->status_service_->buildInfo(),
                "Backend configuration is incomplete.",
                true);
            return sendHtmlResponse(request, html);
        }

        const std::string checkbox_name = std::string("enabled_") + descriptor.backend_key;
        record->enabled = formHasKey(fields, checkbox_name.c_str()) ? 1U : 0U;
        applyBackendStaticDefaults(*record);

        if (descriptor.type == BackendType::kSensorCommunity) {
            const std::string device_id_name = std::string("device_id_") + descriptor.backend_key;
            copyString(
                record->device_id_override,
                sizeof(record->device_id_override),
                findFormValue(fields, device_id_name.c_str()));
        }

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
            server->status_service_->buildInfo(),
            std::string("Failed to save backend configuration: ") + esp_err_to_name(save_err),
            true);
        return sendHtmlResponse(request, html);
    }

    *server->backend_config_list_ = updated;
    server->upload_manager_->applyConfig(updated);
    server->status_service_->setUploads(*server->upload_manager_);

    const std::string html = renderBackendsPage(
        *server->backend_config_list_,
        *server->upload_manager_,
        server->status_service_->buildInfo(),
        "Backend selection saved.",
        false);
    return sendHtmlResponse(request, html);
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
