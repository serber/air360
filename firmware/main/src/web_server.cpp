#include "air360/web_server.hpp"

#include <cinttypes>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

#include "air360/log_buffer.hpp"
#include "air360/sensors/sensor_config_repository.hpp"
#include "air360/sensors/sensor_manager.hpp"
#include "air360/sensors/sensor_registry.hpp"
#include "air360/sensors/sensor_types.hpp"
#include "air360/uploads/backend_http_config.hpp"
#include "air360/uploads/backend_registry.hpp"
#include "air360/uploads/upload_manager.hpp"
#include "air360/web_assets.hpp"
#include "air360/web_ui.hpp"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "captive_portal.h"
#include "sdkconfig.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.web";
constexpr std::size_t kHttpServerStackSize = 10240U;
constexpr std::size_t kHttpServerMaxUriHandlers = 14U;
constexpr std::uint32_t kMinSensorPollIntervalMs = 5000U;

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
    httpd_resp_set_hdr(request, "Cache-Control", "no-cache");
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

constexpr std::string_view kHttpScheme = "http://";
constexpr std::string_view kHttpsScheme = "https://";

bool backendUseHttps(const BackendRecord* record, BackendType type) {
    if (type == BackendType::kCustomUpload) {
        const std::string endpoint =
            record != nullptr ? boundedCString(record->endpoint_url, sizeof(record->endpoint_url)) : "";
        if (endpoint.rfind(kHttpsScheme, 0U) == 0U) {
            return true;
        }
        if (endpoint.rfind(kHttpScheme, 0U) == 0U) {
            return false;
        }
        return true;
    }

    if (record != nullptr) {
        BackendHttpConfigView config;
        std::string error;
        if (decodeBackendHttpRecord(*record, config, error)) {
            return config.use_https;
        }
    }
    return backendDefaultUseHttps(type);
}

BackendHttpConfigView backendHttpConfigForUi(const BackendRecord* record, BackendType type) {
    if (record != nullptr) {
        BackendHttpConfigView config;
        std::string error;
        if (decodeBackendHttpRecord(*record, config, error)) {
            return config;
        }
    }
    return defaultBackendHttpConfig(type);
}

std::string backendDisplayEndpoint(const BackendRecord* record, BackendType type) {
    if (type == BackendType::kCustomUpload) {
        return record != nullptr ? boundedCString(record->endpoint_url, sizeof(record->endpoint_url)) : "";
    }
    return formatBackendHttpDisplayEndpoint(backendHttpConfigForUi(record, type));
}

bool hasSupportedEndpointScheme(std::string_view url) {
    return url.rfind(kHttpScheme, 0U) == 0U || url.rfind(kHttpsScheme, 0U) == 0U;
}

struct ConfigPageViewModel {
    std::string network_mode;
    std::string ip_address;
    std::string device_name;
    std::string notice_html;
    std::string network_error_html;
    std::string wifi_ssid_value;
    std::string wifi_password_value;
    std::string wifi_ssid_options_html;
    std::string sntp_server_value;
    bool wifi_power_save_enabled = false;
    bool sta_use_static_ip = false;
    std::string sta_ip_value;
    std::string sta_netmask_value;
    std::string sta_gateway_value;
    std::string sta_dns_value;
    // Cellular
    bool cellular_enabled = false;
    std::string cellular_apn_value;
    std::string cellular_username_value;
    std::string cellular_password_value;
    std::string cellular_sim_pin_value;
    std::string cellular_connectivity_check_host_value;
    std::string cellular_wifi_debug_window_s_value;
    // BLE
    bool ble_advertise_enabled = false;
    std::uint8_t ble_adv_interval_index = kBleAdvIntervalDefaultIndex;
};

struct BackendCardViewModel {
    std::string display_name;
    std::string backend_key;
    bool enabled = false;
    bool use_https = true;
    std::string endpoint;
    bool show_endpoint_input = false;
    std::string endpoint_input_value;
    bool show_influx_config = false;
    std::string host;
    std::string path;
    std::string port;
    std::string username;
    std::string password;
    std::string measurement_name;
    bool show_device_id_override = false;
    std::string device_id_override;
    bool has_status = false;
    std::string state_key;
    std::string result_key;
    std::uint32_t retry_count = 0U;
    std::string last_attempt;
    int last_http_status = 0;
    std::uint32_t last_response_time_ms = 0U;
    std::string last_error;
};

void appendWifiSsidOption(
    std::string& html,
    const std::string& value,
    const std::string& label,
    bool selected) {
    html += "<option value='";
    html += htmlEscape(value);
    html += "'";
    if (selected) {
        html += " selected";
    }
    html += ">";
    html += htmlEscape(label);
    html += "</option>";
}

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
    std::size_t queued_sample_count = 0U;
    std::string runtime_error;
    std::string latest_reading;
    std::string sensor_type_options_html;
    std::string defaults_hint;
    bool show_i2c_address_input = false;
    std::string i2c_address_value;
    bool show_gpio_pin_select = false;
    std::string gpio_options_html;
    bool enabled = false;
};

enum class SensorCategory : std::uint8_t {
    kClimate = 1U,
    kLight = 2U,
    kParticulateMatter = 3U,
    kLocation = 4U,
    kGas = 5U,
    kPower = 6U,
};

struct SensorCategoryDescriptor {
    SensorCategory category;
    const char* key;
    const char* title;
    const char* description;
    bool allow_multiple;
    const SensorType* sensor_types;
    std::size_t sensor_type_count;
};

struct SensorCategorySectionViewModel {
    SensorCategory category = SensorCategory::kClimate;
    std::string key;
    std::string title;
    std::string description;
    bool allow_multiple = false;
    std::size_t configured_count = 0U;
    std::string supported_models_html;
    std::string notice_html;
    std::vector<SensorCardViewModel> cards;
    std::string add_sensor_type_options_html;
    std::string add_defaults_hint;
    bool add_show_i2c_address_input = false;
    std::string add_i2c_address_value;
    std::string add_gpio_options_html;
    std::uint32_t add_poll_interval_ms = 10000U;
    bool add_show_gpio_pin_select = false;
    bool show_add_form = false;
    std::string add_button_label;
};

struct SensorsPageViewModel {
    std::size_t configured_count = 0U;
    std::size_t max_sensors = 0U;
    bool has_pending_changes = false;
    std::size_t runtime_sensor_count = 0U;
    std::size_t free_slots = 0U;
    std::string notice_html;
    std::vector<SensorCategorySectionViewModel> sections;
};

ConfigPageViewModel buildConfigPageViewModel(
    const DeviceConfig& config,
    const CellularConfig& cellular_config,
    const NetworkState& network_state,
    const NetworkManager& network_manager,
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
std::string renderSensorCategorySection(const SensorCategorySectionViewModel& section);
SensorsPageViewModel buildSensorsPageViewModel(
    const SensorConfigList& sensor_config_list,
    const SensorManager& sensor_manager,
    const MeasurementStore& measurement_store,
    bool has_pending_changes,
    const std::string& notice,
    bool error_notice);
const BackendRecord* findBackendRecordForDescriptor(
    const BackendConfigList& config,
    const BackendDescriptor& descriptor);

std::string buildBleIntervalOptions(std::uint8_t selected_index) {
    constexpr const char* kLabels[kBleAdvIntervalCount] = {"100 ms", "300 ms", "1 s", "3 s"};
    std::string html;
    for (std::uint8_t i = 0U; i < kBleAdvIntervalCount; ++i) {
        html += "<option value='";
        html += std::to_string(i);
        html += "'";
        if (i == selected_index) {
            html += " selected";
        }
        html += ">";
        html += kLabels[i];
        html += "</option>";
    }
    return html;
}

std::string renderConfigPage(
    const DeviceConfig& config,
    const CellularConfig& cellular_config,
    const NetworkState& network_state,
    const NetworkManager& network_manager,
    const std::string& notice,
    bool error_notice) {
    const ConfigPageViewModel model =
        buildConfigPageViewModel(
            config, cellular_config, network_state, network_manager, notice, error_notice);

    const std::string body = renderPageTemplate(
        WebTemplateKey::kConfig,
        WebTemplateBindings{
            {"NETWORK_MODE", htmlEscape(model.network_mode)},
            {"IP_ADDRESS", htmlEscape(model.ip_address)},
            {"DEVICE_NAME", htmlEscape(model.device_name)},
            {"NOTICE", model.notice_html},
            {"NETWORK_ERROR", model.network_error_html},
            {"DEVICE_NAME_VALUE", htmlEscape(model.device_name)},
            {"WIFI_SSID_VALUE", htmlEscape(model.wifi_ssid_value)},
            {"WIFI_PASSWORD_VALUE", htmlEscape(model.wifi_password_value)},
            {"WIFI_SSID_OPTIONS", model.wifi_ssid_options_html},
            {"SNTP_SERVER_VALUE", htmlEscape(model.sntp_server_value)},
            {"WIFI_POWER_SAVE_CHECKED", model.wifi_power_save_enabled ? "checked" : ""},
            {"STA_USE_STATIC_IP_CHECKED", model.sta_use_static_ip ? "checked" : ""},
            {"STA_STATIC_IP_GROUP_DISABLED_CLASS", model.sta_use_static_ip ? "" : "field--disabled"},
            {"STA_IP_VALUE", htmlEscape(model.sta_ip_value)},
            {"STA_NETMASK_VALUE", htmlEscape(model.sta_netmask_value)},
            {"STA_GATEWAY_VALUE", htmlEscape(model.sta_gateway_value)},
            {"STA_DNS_VALUE", htmlEscape(model.sta_dns_value)},
            {"CELLULAR_ENABLED_CHECKED", model.cellular_enabled ? "checked" : ""},
            {"CELLULAR_GROUP_DISABLED_CLASS", model.cellular_enabled ? "" : "field--disabled"},
            {"CELLULAR_APN_VALUE", htmlEscape(model.cellular_apn_value)},
            {"CELLULAR_USERNAME_VALUE", htmlEscape(model.cellular_username_value)},
            {"CELLULAR_PASSWORD_VALUE", htmlEscape(model.cellular_password_value)},
            {"CELLULAR_SIM_PIN_VALUE", htmlEscape(model.cellular_sim_pin_value)},
            {"CELLULAR_CONNECTIVITY_CHECK_HOST_VALUE",
             htmlEscape(model.cellular_connectivity_check_host_value)},
            {"CELLULAR_WIFI_DEBUG_WINDOW_S_VALUE", model.cellular_wifi_debug_window_s_value},
            {"BLE_ADVERTISE_ENABLED_CHECKED", model.ble_advertise_enabled ? "checked" : ""},
            {"BLE_GROUP_DISABLED_CLASS", model.ble_advertise_enabled ? "" : "field--disabled"},
            {"BLE_ADV_INTERVAL_OPTIONS", buildBleIntervalOptions(model.ble_adv_interval_index)},
        });

    return renderPageDocument(
        WebPageKey::kConfig,
        "air360 device configuration",
        "Device Configuration",
        "Manage station Wi-Fi and local device identity without leaving the firmware UI.",
        body,
        true,
        network_state.mode == NetworkMode::kSetupAp);
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

bool parseI2cAddress(const std::string& input, std::uint8_t& value) {
    if (input.empty()) {
        return false;
    }

    char* end = nullptr;
    const unsigned long parsed = std::strtoul(input.c_str(), &end, 0);
    if (end == nullptr || *end != '\0' || parsed > 0x7FU) {
        return false;
    }

    value = static_cast<std::uint8_t>(parsed);
    return true;
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

std::string formatI2cAddress(std::uint8_t address) {
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "0x%02X", static_cast<unsigned>(address));
    return buffer;
}

std::uint32_t normalizeSensorPollInterval(std::uint32_t value) {
    return std::max<std::uint32_t>(value, kMinSensorPollIntervalMs);
}

constexpr SensorType kClimateSensorTypes[] = {
    SensorType::kBme280,
    SensorType::kBme680,
    SensorType::kDht11,
    SensorType::kDht22,
    SensorType::kDs18b20,
    SensorType::kHtu2x,
    SensorType::kSht4x,
};

constexpr SensorType kLightSensorTypes[] = {
    SensorType::kVeml7700,
};

constexpr SensorType kParticulateMatterSensorTypes[] = {
    SensorType::kSps30,
};

constexpr SensorType kLocationSensorTypes[] = {
    SensorType::kGpsNmea,
};

constexpr SensorType kGasSensorTypes[] = {
    SensorType::kScd30,
    SensorType::kMe3No2,
    SensorType::kMhz19b,
};

constexpr SensorType kPowerSensorTypes[] = {
    SensorType::kIna219,
};

constexpr SensorCategoryDescriptor kSensorCategoryDescriptors[] = {
    {
        SensorCategory::kClimate,
        "climate",
        "Climate",
        "Temperature, humidity, pressure, and related climate measurements over GPIO or I2C.",
        false,
        kClimateSensorTypes,
        sizeof(kClimateSensorTypes) / sizeof(kClimateSensorTypes[0]),
    },
    {
        SensorCategory::kParticulateMatter,
        "particulate-matter",
        "Particulate Matter",
        "Fine dust concentration and particle size metrics.",
        false,
        kParticulateMatterSensorTypes,
        sizeof(kParticulateMatterSensorTypes) / sizeof(kParticulateMatterSensorTypes[0]),
    },
    {
        SensorCategory::kLight,
        "light",
        "Light",
        "Ambient light and illuminance sensing.",
        false,
        kLightSensorTypes,
        sizeof(kLightSensorTypes) / sizeof(kLightSensorTypes[0]),
    },
    {
        SensorCategory::kLocation,
        "location",
        "Location",
        "GPS coordinates, altitude, movement, and fix state.",
        false,
        kLocationSensorTypes,
        sizeof(kLocationSensorTypes) / sizeof(kLocationSensorTypes[0]),
    },
    {
        SensorCategory::kGas,
        "gas",
        "Gas",
        "Gas sensors. Multiple gas sensors are allowed.",
        true,
        kGasSensorTypes,
        sizeof(kGasSensorTypes) / sizeof(kGasSensorTypes[0]),
    },
    {
        SensorCategory::kPower,
        "power",
        "Power",
        "DC current, voltage, and power monitoring over I2C.",
        false,
        kPowerSensorTypes,
        sizeof(kPowerSensorTypes) / sizeof(kPowerSensorTypes[0]),
    },
};

SensorCategory sensorCategoryForType(SensorType type) {
    switch (type) {
        case SensorType::kBme280:
        case SensorType::kBme680:
        case SensorType::kDht11:
        case SensorType::kDht22:
        case SensorType::kDs18b20:
        case SensorType::kHtu2x:
        case SensorType::kSht4x:
            return SensorCategory::kClimate;
        case SensorType::kScd30:
            return SensorCategory::kGas;
        case SensorType::kVeml7700:
            return SensorCategory::kLight;
        case SensorType::kSps30:
            return SensorCategory::kParticulateMatter;
        case SensorType::kGpsNmea:
            return SensorCategory::kLocation;
        case SensorType::kIna219:
            return SensorCategory::kPower;
        case SensorType::kMe3No2:
        case SensorType::kMhz19b:
        case SensorType::kUnknown:
        default:
            return SensorCategory::kGas;
    }
}

const SensorCategoryDescriptor* findSensorCategoryDescriptor(SensorCategory category) {
    for (const auto& descriptor : kSensorCategoryDescriptors) {
        if (descriptor.category == category) {
            return &descriptor;
        }
    }

    return nullptr;
}

const SensorDescriptor* firstSensorDescriptorForCategory(
    const SensorRegistry& registry,
    const SensorCategoryDescriptor& category_descriptor) {
    for (std::size_t index = 0; index < category_descriptor.sensor_type_count; ++index) {
        const SensorDescriptor* descriptor =
            registry.findByType(category_descriptor.sensor_types[index]);
        if (descriptor != nullptr) {
            return descriptor;
        }
    }

    return nullptr;
}

std::size_t countOtherSensorsInCategory(
    const SensorConfigList& sensor_config_list,
    SensorCategory category,
    std::uint32_t ignored_id) {
    std::size_t count = 0U;
    for (std::size_t index = 0; index < sensor_config_list.sensor_count; ++index) {
        const SensorRecord& record = sensor_config_list.sensors[index];
        if (record.id == ignored_id) {
            continue;
        }
        if (sensorCategoryForType(record.sensor_type) == category) {
            ++count;
        }
    }

    return count;
}

bool validateSensorCategorySelection(
    const SensorConfigList& sensor_config_list,
    const SensorRecord& record,
    std::string& error) {
    const SensorCategory category = sensorCategoryForType(record.sensor_type);
    const SensorCategoryDescriptor* category_descriptor =
        findSensorCategoryDescriptor(category);
    if (category_descriptor == nullptr || category_descriptor->allow_multiple) {
        return true;
    }

    if (countOtherSensorsInCategory(sensor_config_list, category, record.id) > 0U) {
        error = std::string(category_descriptor->title) +
                " already has a configured sensor. Edit the existing sensor or remove it first.";
        return false;
    }

    return true;
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
            return "Defaults: I2C bus 0 at address 0x77.";
        case SensorType::kSps30:
            return "Defaults: I2C bus 0 at address 0x69.";
        case SensorType::kScd30:
            return "Defaults: I2C bus 0 at address 0x61.";
        case SensorType::kVeml7700:
            return "Defaults: I2C bus 0 at address 0x10.";
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
        case SensorType::kDs18b20:
            return "Defaults: choose one of the board GPIO sensor slots (GPIO 4, 5, or 6).";
        case SensorType::kHtu2x:
            return "Defaults: I2C bus 0 at address 0x40.";
        case SensorType::kSht4x:
            return "Defaults: I2C bus 0 at address 0x44.";
        case SensorType::kMe3No2:
            return "Defaults: analog input on one of the board sensor GPIO slots (GPIO 4, 5, or 6).";
        case SensorType::kMhz19b: {
            std::string hint = "Defaults: UART ";
            hint += std::to_string(CONFIG_AIR360_MHZ19B_DEFAULT_UART_PORT);
            hint += " RX";
            hint += std::to_string(CONFIG_AIR360_MHZ19B_DEFAULT_RX_GPIO);
            hint += " TX";
            hint += std::to_string(CONFIG_AIR360_MHZ19B_DEFAULT_TX_GPIO);
            hint += " @ 9600 baud.";
            return hint;
        }
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
    html += "' data-requires-i2c='";
    html += descriptor.supports_i2c ? "true" : "false";
    html += "' data-defaults-hint='";
    html += htmlEscape(sensorDefaultsHint(descriptor));
    html += "' data-default-i2c-address='";
    html += descriptor.supports_i2c ? htmlEscape(formatI2cAddress(descriptor.default_i2c_address))
                                    : "";
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
    const CellularConfig& cellular_config,
    const NetworkState& network_state,
    const NetworkManager& network_manager,
    const std::string& notice,
    bool error_notice) {
    ConfigPageViewModel model;
    model.network_mode = networkModeLabel(network_state.mode);
    model.ip_address = network_state.ip_address.empty() ? "unavailable" : network_state.ip_address;
    model.device_name = config.device_name;
    model.notice_html = renderNotice(notice, error_notice);
    model.wifi_ssid_value = config.wifi_sta_ssid;
    model.wifi_password_value = config.wifi_sta_password;
    model.sntp_server_value = boundedCString(config.sntp_server, sizeof(config.sntp_server));

    appendWifiSsidOption(
        model.wifi_ssid_options_html,
        "",
        "Select network...",
        model.wifi_ssid_value.empty());

    bool selected_network_present = false;
    for (const auto& network : network_manager.availableNetworks()) {
        if (network.ssid == model.wifi_ssid_value) {
            selected_network_present = true;
        }
    }

    if (!model.wifi_ssid_value.empty() && !selected_network_present) {
        appendWifiSsidOption(
            model.wifi_ssid_options_html,
            model.wifi_ssid_value,
            model.wifi_ssid_value + " (saved)",
            true);
    }

    for (const auto& network : network_manager.availableNetworks()) {
        appendWifiSsidOption(
            model.wifi_ssid_options_html,
            network.ssid,
            network.ssid + " (" + std::to_string(network.rssi) + " dBm)",
            network.ssid == model.wifi_ssid_value);
    }

    if (!network_state.last_error.empty()) {
        model.network_error_html += "<p>Last network error: <code>";
        model.network_error_html += htmlEscape(network_state.last_error);
        model.network_error_html += "</code></p>";
    }

    model.wifi_power_save_enabled = config.wifi_power_save_enabled != 0U;
    model.sta_use_static_ip = config.sta_use_static_ip != 0U;
    model.sta_ip_value = boundedCString(config.sta_ip, sizeof(config.sta_ip));
    model.sta_netmask_value = boundedCString(config.sta_netmask, sizeof(config.sta_netmask));
    model.sta_gateway_value = boundedCString(config.sta_gateway, sizeof(config.sta_gateway));
    model.sta_dns_value = boundedCString(config.sta_dns, sizeof(config.sta_dns));

    // Pre-fill static IP fields from the current DHCP lease when not yet configured.
    if (model.sta_ip_value.empty() && network_state.station_connected) {
        esp_netif_t* sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (sta != nullptr) {
            esp_netif_ip_info_t ip_info{};
            if (esp_netif_get_ip_info(sta, &ip_info) == ESP_OK) {
                char buf[16];
                std::snprintf(buf, sizeof(buf), IPSTR, IP2STR(&ip_info.ip));
                model.sta_ip_value = buf;
                std::snprintf(buf, sizeof(buf), IPSTR, IP2STR(&ip_info.netmask));
                model.sta_netmask_value = buf;
                std::snprintf(buf, sizeof(buf), IPSTR, IP2STR(&ip_info.gw));
                model.sta_gateway_value = buf;
            }
            if (model.sta_dns_value.empty()) {
                esp_netif_dns_info_t dns_info{};
                if (esp_netif_get_dns_info(sta, ESP_NETIF_DNS_MAIN, &dns_info) == ESP_OK &&
                    dns_info.ip.u_addr.ip4.addr != 0U) {
                    char buf[16];
                    std::snprintf(buf, sizeof(buf), IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
                    model.sta_dns_value = buf;
                }
            }
        }
    }

    model.cellular_enabled = cellular_config.enabled != 0U;
    model.cellular_apn_value =
        boundedCString(cellular_config.apn, sizeof(cellular_config.apn));
    if (model.cellular_apn_value.empty()) {
        model.cellular_apn_value = "internet";
    }
    model.cellular_username_value =
        boundedCString(cellular_config.username, sizeof(cellular_config.username));
    model.cellular_password_value =
        boundedCString(cellular_config.password, sizeof(cellular_config.password));
    model.cellular_sim_pin_value =
        boundedCString(cellular_config.sim_pin, sizeof(cellular_config.sim_pin));
    model.cellular_connectivity_check_host_value = boundedCString(
        cellular_config.connectivity_check_host,
        sizeof(cellular_config.connectivity_check_host));
    if (model.cellular_connectivity_check_host_value.empty()) {
        model.cellular_connectivity_check_host_value = "8.8.8.8";
    }
    model.cellular_wifi_debug_window_s_value =
        std::to_string(cellular_config.wifi_debug_window_s);

    model.ble_advertise_enabled = config.ble_advertise_enabled != 0U;
    model.ble_adv_interval_index = config.ble_adv_interval_index < kBleAdvIntervalCount
        ? config.ble_adv_interval_index : kBleAdvIntervalDefaultIndex;

    return model;
}

std::string renderBackendCard(const BackendCardViewModel& card) {
    std::string https_block;
    std::string endpoint_block;
    if (card.show_influx_config) {
        https_block += "<label class='checkbox'>";
        https_block += "<input type='checkbox' name='use_https_";
        https_block += htmlEscape(card.backend_key);
        https_block += "'";
        if (card.use_https) {
            https_block += " checked";
        }
        https_block += ">";
        https_block += "<span class='checkbox__label'>Use HTTPS</span></label>";

        endpoint_block += "<div class='field'><label for='host_";
        endpoint_block += htmlEscape(card.backend_key);
        endpoint_block += "'>Host</label>";
        endpoint_block += "<input class='input' id='host_";
        endpoint_block += htmlEscape(card.backend_key);
        endpoint_block += "' name='host_";
        endpoint_block += htmlEscape(card.backend_key);
        endpoint_block += "' maxlength='";
        endpoint_block += std::to_string(kBackendUrlCapacity - 1U);
        endpoint_block += "' value='";
        endpoint_block += htmlEscape(card.host);
        endpoint_block += "'></div>";

        endpoint_block += "<div class='field'><label for='path_";
        endpoint_block += htmlEscape(card.backend_key);
        endpoint_block += "'>Path</label>";
        endpoint_block += "<input class='input' id='path_";
        endpoint_block += htmlEscape(card.backend_key);
        endpoint_block += "' name='path_";
        endpoint_block += htmlEscape(card.backend_key);
        endpoint_block += "' maxlength='";
        endpoint_block += std::to_string(kBackendUrlCapacity - 1U);
        endpoint_block += "' placeholder='/write?db=air360' value='";
        endpoint_block += htmlEscape(card.path);
        endpoint_block += "'></div>";

        endpoint_block += "<div class='field'><label for='port_";
        endpoint_block += htmlEscape(card.backend_key);
        endpoint_block += "'>Port</label>";
        endpoint_block += "<input class='input' id='port_";
        endpoint_block += htmlEscape(card.backend_key);
        endpoint_block += "' name='port_";
        endpoint_block += htmlEscape(card.backend_key);
        endpoint_block += "' inputmode='numeric' maxlength='5' value='";
        endpoint_block += htmlEscape(card.port);
        endpoint_block += "'></div>";

        endpoint_block += "<div class='field'><label for='user_";
        endpoint_block += htmlEscape(card.backend_key);
        endpoint_block += "'>User</label>";
        endpoint_block += "<input class='input' id='user_";
        endpoint_block += htmlEscape(card.backend_key);
        endpoint_block += "' name='user_";
        endpoint_block += htmlEscape(card.backend_key);
        endpoint_block += "' maxlength='";
        endpoint_block += std::to_string(kBackendUsernameCapacity - 1U);
        endpoint_block += "' value='";
        endpoint_block += htmlEscape(card.username);
        endpoint_block += "'></div>";

        endpoint_block += "<div class='field'><label for='password_";
        endpoint_block += htmlEscape(card.backend_key);
        endpoint_block += "'>Password</label>";
        endpoint_block += "<input class='input' type='password' id='password_";
        endpoint_block += htmlEscape(card.backend_key);
        endpoint_block += "' name='password_";
        endpoint_block += htmlEscape(card.backend_key);
        endpoint_block += "' maxlength='";
        endpoint_block += std::to_string(kBackendPasswordCapacity - 1U);
        endpoint_block += "' value='";
        endpoint_block += htmlEscape(card.password);
        endpoint_block += "'></div>";

        endpoint_block += "<div class='field'><label for='measurement_";
        endpoint_block += htmlEscape(card.backend_key);
        endpoint_block += "'>Measurement</label>";
        endpoint_block += "<input class='input' id='measurement_";
        endpoint_block += htmlEscape(card.backend_key);
        endpoint_block += "' name='measurement_";
        endpoint_block += htmlEscape(card.backend_key);
        endpoint_block += "' maxlength='";
        endpoint_block += std::to_string(kBackendIdentifierCapacity - 1U);
        endpoint_block += "' value='";
        endpoint_block += htmlEscape(card.measurement_name);
        endpoint_block += "'></div>";
        endpoint_block += "<p class='hint'>Sends Influx line protocol with one line per sensor sample group.</p>";
    } else if (card.show_endpoint_input) {
        endpoint_block += "<div class='field'><label for='endpoint_url_";
        endpoint_block += htmlEscape(card.backend_key);
        endpoint_block += "'>Endpoint URL</label>";
        endpoint_block += "<input class='input' id='endpoint_url_";
        endpoint_block += htmlEscape(card.backend_key);
        endpoint_block += "' name='endpoint_url_";
        endpoint_block += htmlEscape(card.backend_key);
        endpoint_block += "'";
        endpoint_block += "' maxlength='";
        endpoint_block += std::to_string(kBackendUrlCapacity - 1U);
        endpoint_block += "' placeholder='https://example.com/api/air360' value='";
        endpoint_block += htmlEscape(card.endpoint_input_value);
        endpoint_block += "'>";
        endpoint_block += "<p class='hint'>Sends a POST request with the Air360 JSON body to the exact URL above.</p></div>";
    } else {
        https_block += "<label class='checkbox'>";
        https_block += "<input type='checkbox' name='use_https_";
        https_block += htmlEscape(card.backend_key);
        https_block += "'";
        if (card.use_https) {
            https_block += " checked";
        }
        https_block += ">";
        https_block += "<span class='checkbox__label'>Use HTTPS</span></label>";

        if (!card.endpoint.empty()) {
            endpoint_block += "<p>Endpoint: <code>";
            endpoint_block += htmlEscape(card.endpoint);
            endpoint_block += "</code></p>";
        }
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

    std::string status_block;
    if (!card.enabled) {
        status_block.clear();
    } else if (card.has_status) {
        status_block += "<p>Last attempt: <code>";
        status_block += htmlEscape(card.last_attempt);
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
            {"BACKEND_KEY_ATTR", htmlEscape(card.backend_key)},
            {"ENABLED_CHECKED", card.enabled ? "checked" : ""},
            {"HTTPS_BLOCK", https_block},
            {"ENDPOINT_BLOCK", endpoint_block},
            {"DEVICE_ID_OVERRIDE_BLOCK", device_id_override_block},
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
        card.enabled = record != nullptr && record->enabled != 0U;
        card.use_https = backendUseHttps(record, descriptor.type);
        card.show_endpoint_input = descriptor.type == BackendType::kCustomUpload;
        card.show_influx_config = descriptor.type == BackendType::kInfluxDb;
        if (record != nullptr) {
            if (card.show_influx_config) {
                const BackendHttpConfigView config = backendHttpConfigForUi(record, descriptor.type);
                card.use_https = config.use_https;
                card.host = config.host;
                card.path = config.path;
                card.port = std::to_string(config.port);
                card.username = config.username;
                card.password = config.password;
                card.measurement_name = config.measurement_name;
            } else if (card.show_endpoint_input) {
                card.endpoint_input_value =
                    boundedCString(record->endpoint_url, sizeof(record->endpoint_url));
            } else {
                card.endpoint = backendDisplayEndpoint(record, descriptor.type);
            }
            card.show_device_id_override = descriptor.type == BackendType::kSensorCommunity;
            if (card.show_device_id_override) {
                card.device_id_override =
                    boundedCString(record->device_id_override, sizeof(record->device_id_override));
                if (card.device_id_override.empty()) {
                    card.device_id_override = build_info.short_chip_id;
                }
            }
        } else if (card.show_influx_config) {
            const BackendHttpConfigView config = defaultBackendHttpConfig(descriptor.type);
            card.use_https = config.use_https;
            card.port = std::to_string(config.port);
            card.measurement_name = config.measurement_name;
        } else if (!card.show_endpoint_input) {
            card.endpoint = backendDisplayEndpoint(nullptr, descriptor.type);
        }

        if (status != nullptr) {
            card.has_status = true;
            card.state_key = backendRuntimeStateKey(status->state);
            card.result_key = uploadResultClassKey(status->last_result);
            card.retry_count = status->retry_count;
            card.last_attempt = formatStatusTime(
                status->last_attempt_unix_ms,
                status->last_attempt_uptime_ms);
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

    std::string i2c_field_block;
    if (card.show_i2c_address_input) {
        i2c_field_block += "<div class='field' data-sensor-i2c-field><label for='i2c_address_";
        i2c_field_block += std::to_string(card.id);
        i2c_field_block += "'>I2C address</label>";
        i2c_field_block += "<input class='input' id='i2c_address_";
        i2c_field_block += std::to_string(card.id);
        i2c_field_block += "' name='i2c_address' value='";
        i2c_field_block += htmlEscape(card.i2c_address_value);
        i2c_field_block += "' placeholder='0x76' spellcheck='false' autocapitalize='off'></div>";
    }

    std::string gpio_field_block;
    if (card.show_gpio_pin_select) {
        gpio_field_block += "<div class='field' data-sensor-pin-field><label for='analog_gpio_pin_";
        gpio_field_block += std::to_string(card.id);
        gpio_field_block += "'>GPIO pin</label>";
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
            {"RUNTIME_STATE", htmlEscape(card.runtime_state_key)},
            {"TRANSPORT_SUMMARY", htmlEscape(card.transport_summary)},
            {"POLL_INTERVAL_MS", std::to_string(card.poll_interval_ms)},
            {"QUEUED_SAMPLE_COUNT", std::to_string(card.queued_sample_count)},
            {"RUNTIME_ERROR_BLOCK", runtime_error_block},
            {"LATEST_READING_BLOCK", latest_reading_block},
            {"SENSOR_ID", std::to_string(card.id)},
            {"SENSOR_TYPE_OPTIONS", card.sensor_type_options_html},
            {"DEFAULTS_HINT_TEXT", htmlEscape(card.defaults_hint)},
            {"DEFAULTS_HINT_HIDDEN", card.defaults_hint.empty() ? "hidden" : ""},
            {"I2C_FIELD_BLOCK", i2c_field_block},
            {"GPIO_FIELD_BLOCK", gpio_field_block},
            {"ENABLED_CHECKED", card.enabled ? "checked" : ""},
        });
}

std::string renderSensorCategorySection(const SensorCategorySectionViewModel& section) {
    std::string cards_html;
    cards_html.reserve(section.cards.size() * 2200U);
    for (const auto& card : section.cards) {
        cards_html += renderSensorCard(card);
    }

    if (cards_html.empty()) {
        cards_html = "<p class='muted'>No sensors configured in this category yet.</p>";
    }

    std::string add_form_block;
    if (section.show_add_form) {
        std::string i2c_field_block;
        i2c_field_block += "<div class='field' data-sensor-i2c-field";
        if (!section.add_show_i2c_address_input) {
            i2c_field_block += " hidden";
        }
        i2c_field_block += "><label for='i2c_address_add_";
        i2c_field_block += htmlEscape(section.key);
        i2c_field_block += "'>I2C address</label><input class='input' id='i2c_address_add_";
        i2c_field_block += htmlEscape(section.key);
        i2c_field_block += "' name='i2c_address' value='";
        i2c_field_block += htmlEscape(section.add_i2c_address_value);
        i2c_field_block += "' placeholder='0x76' spellcheck='false' autocapitalize='off'";
        if (!section.add_show_i2c_address_input) {
            i2c_field_block += " disabled";
        }
        i2c_field_block += "></div>";

        std::string gpio_field_block;
        gpio_field_block += "<div class='field' data-sensor-pin-field";
        if (!section.add_show_gpio_pin_select) {
            gpio_field_block += " hidden";
        }
        gpio_field_block += "><label for='analog_gpio_pin_add_";
        gpio_field_block += htmlEscape(section.key);
        gpio_field_block += "'>GPIO pin (4, 5, or 6)</label><select class='select' id='analog_gpio_pin_add_";
        gpio_field_block += htmlEscape(section.key);
        gpio_field_block += "' name='analog_gpio_pin'";
        if (!section.add_show_gpio_pin_select) {
            gpio_field_block += " disabled";
        }
        gpio_field_block += ">";
        gpio_field_block += section.add_gpio_options_html;
        gpio_field_block += "</select></div>";

        add_form_block += "<div class='list-card stack'><h3 class='list-card__title'>";
        add_form_block += section.allow_multiple ? "Add Gas Sensor" : "Add Sensor";
        add_form_block += "</h3><form class='stack' method='POST' action='/sensors' data-dirty-track='sensor-add-";
        add_form_block += htmlEscape(section.key);
        add_form_block += "' data-sensor-form><input type='hidden' name='action' value='add'><div class='field'><label for='sensor_type_add_";
        add_form_block += htmlEscape(section.key);
        add_form_block += "'>Model</label><select class='select' id='sensor_type_add_";
        add_form_block += htmlEscape(section.key);
        add_form_block += "' name='sensor_type' data-sensor-type-select>";
        add_form_block += section.add_sensor_type_options_html;
        add_form_block += "</select></div><p class='hint' data-sensor-defaults";
        if (section.add_defaults_hint.empty()) {
            add_form_block += " hidden";
        }
        add_form_block += ">";
        add_form_block += htmlEscape(section.add_defaults_hint);
        add_form_block += "</p><div class='field'><label for='poll_interval_ms_add_";
        add_form_block += htmlEscape(section.key);
        add_form_block += "'>Poll interval (ms)</label><input class='input' id='poll_interval_ms_add_";
        add_form_block += htmlEscape(section.key);
        add_form_block += "' name='poll_interval_ms' inputmode='numeric' min='5000' step='1000' value='";
        add_form_block += std::to_string(section.add_poll_interval_ms);
        add_form_block += "'></div>";
        add_form_block += i2c_field_block;
        add_form_block += gpio_field_block;
        add_form_block += "<label class='checkbox'><input name='enabled' type='checkbox'><span class='checkbox__label'>Enabled</span></label><div class='split-actions'><button class='button' type='submit'>";
        add_form_block += htmlEscape(section.add_button_label);
        add_form_block += "</button></div></form></div>";
    }

    std::string section_html;
    section_html += "<section class='panel stack'><div><h2>";
    section_html += htmlEscape(section.title);
    section_html += "</h2><p class='muted'>";
    section_html += htmlEscape(section.description);
    section_html += "</p><div class='meta'><span class='pill'>";
    section_html += section.allow_multiple ? "Multiple sensors allowed" : "Single sensor category";
    section_html += "</span><span class='pill'>Configured ";
    section_html += std::to_string(section.configured_count);
    section_html += "</span>";
    section_html += section.supported_models_html;
    section_html += "</div></div>";
    section_html += section.notice_html;
    section_html += cards_html;
    section_html += add_form_block;
    section_html += "</section>";
    return section_html;
}

SensorsPageViewModel buildSensorsPageViewModel(
    const SensorConfigList& sensor_config_list,
    const SensorManager& sensor_manager,
    const MeasurementStore& measurement_store,
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
    model.sections.reserve(
        sizeof(kSensorCategoryDescriptors) / sizeof(kSensorCategoryDescriptors[0]));

    for (const auto& category_descriptor : kSensorCategoryDescriptors) {
        SensorCategorySectionViewModel section;
        section.category = category_descriptor.category;
        section.key = category_descriptor.key;
        section.title = category_descriptor.title;
        section.description = category_descriptor.description;
        section.allow_multiple = category_descriptor.allow_multiple;
        section.add_button_label =
            category_descriptor.allow_multiple ? "Stage gas sensor" : "Stage sensor";

        for (std::size_t index = 0; index < category_descriptor.sensor_type_count; ++index) {
            const SensorDescriptor* descriptor =
                registry.findByType(category_descriptor.sensor_types[index]);
            if (descriptor == nullptr) {
                continue;
            }

            section.add_sensor_type_options_html +=
                sensorTypeOptionHtml(*descriptor, index == 0U);
            section.supported_models_html += "<span class='pill'>";
            section.supported_models_html += htmlEscape(descriptor->display_name);
            section.supported_models_html += "</span>";
        }

        if (const SensorDescriptor* descriptor =
                firstSensorDescriptorForCategory(registry, category_descriptor);
            descriptor != nullptr) {
            section.add_defaults_hint = sensorDefaultsHint(*descriptor);
            section.add_poll_interval_ms =
                normalizeSensorPollInterval(descriptor->default_poll_interval_ms);
            section.add_show_i2c_address_input = descriptor->supports_i2c;
            section.add_i2c_address_value = formatI2cAddress(descriptor->default_i2c_address);
            section.add_show_gpio_pin_select =
                descriptor->supports_gpio || descriptor->supports_analog;
        }
        appendBoardGpioOptions(section.add_gpio_options_html, defaultBoardGpioPin());
        model.sections.push_back(std::move(section));
    }

    for (std::size_t index = 0; index < sensor_config_list.sensor_count; ++index) {
        const SensorRecord& record = sensor_config_list.sensors[index];
        const SensorDescriptor* descriptor = registry.findByType(record.sensor_type);
        const SensorCategory category = sensorCategoryForType(record.sensor_type);
        const SensorRuntimeInfo* runtime_info = nullptr;
        for (const auto& sensor : runtime_sensors) {
            if (sensor.id == record.id) {
                runtime_info = &sensor;
                break;
            }
        }

        SensorCardViewModel card;
        card.id = record.id;
        card.display_name = descriptor != nullptr ? descriptor->display_name : std::string("Sensor");
        card.runtime_state_key =
            runtime_info != nullptr ? sensorRuntimeStateKey(runtime_info->state) : "unknown";
        card.transport_summary = transportSummaryForRecord(record);
        card.poll_interval_ms = record.poll_interval_ms;
        card.enabled = record.enabled != 0U;
        const MeasurementRuntimeInfo measurement_runtime =
            measurement_store.runtimeInfoForSensor(record.id);
        if (runtime_info != nullptr) {
            card.runtime_error = runtime_info->last_error;
        }
        card.queued_sample_count = measurement_runtime.queued_sample_count;
        if (!measurement_runtime.measurement.empty()) {
            card.latest_reading = measurementSummary(measurement_runtime.measurement);
        }

        const SensorCategoryDescriptor* category_descriptor =
            findSensorCategoryDescriptor(category);
        if (category_descriptor != nullptr) {
            for (std::size_t descriptor_index = 0;
                 descriptor_index < category_descriptor->sensor_type_count;
                 ++descriptor_index) {
                const SensorDescriptor* option_descriptor =
                    registry.findByType(category_descriptor->sensor_types[descriptor_index]);
                if (option_descriptor == nullptr) {
                    continue;
                }

                card.sensor_type_options_html += sensorTypeOptionHtml(
                    *option_descriptor,
                    option_descriptor->type == record.sensor_type);
            }
        }

        if (descriptor != nullptr) {
            card.defaults_hint = sensorDefaultsHint(*descriptor);
            card.show_i2c_address_input = descriptor->supports_i2c;
        }
        if (card.show_i2c_address_input) {
            card.i2c_address_value = formatI2cAddress(record.i2c_address);
        }
        card.show_gpio_pin_select =
            record.transport_kind == TransportKind::kGpio ||
            record.transport_kind == TransportKind::kAnalog;
        if (card.show_gpio_pin_select) {
            appendBoardGpioOptions(card.gpio_options_html, record.analog_gpio_pin);
        }

        for (auto& section : model.sections) {
            if (section.category == category) {
                section.cards.push_back(std::move(card));
                break;
            }
        }
    }

    for (auto& section : model.sections) {
        section.configured_count = section.cards.size();
        section.show_add_form =
            model.free_slots > 0U && (section.allow_multiple || section.cards.empty());
        if (!section.allow_multiple && section.cards.size() > 1U) {
            section.notice_html = renderNotice(
                "This category should contain only one sensor. Remove the extra sensor entries.",
                true);
        }
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
    const MeasurementStore& measurement_store,
    bool has_pending_changes,
    const std::string& notice,
    bool error_notice) {
    const SensorsPageViewModel model =
        buildSensorsPageViewModel(
            sensor_config_list,
            sensor_manager,
            measurement_store,
            has_pending_changes,
            notice,
            error_notice);

    std::string category_sections_html;
    category_sections_html.reserve(model.sections.size() * 3200U);
    for (const auto& section : model.sections) {
        category_sections_html += renderSensorCategorySection(section);
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
            {"CATEGORY_SECTIONS", category_sections_html},
        });
    return renderPageDocument(
        WebPageKey::kSensors,
        "air360 sensors",
        "Sensor Configuration",
        "Configure sensors by category, keep one device per category where it makes sense, and stage the final sensor set before applying it with a reboot.",
        body);
}

bool isValidIpv4(const std::string& s) {
    if (s.size() < 7U || s.size() > 15U) {
        return false;
    }
    int a = 0;
    int b = 0;
    int c = 0;
    int d = 0;
    if (std::sscanf(s.c_str(), "%d.%d.%d.%d", &a, &b, &c, &d) != 4) {
        return false;
    }
    if (a < 0 || a > 255 || b < 0 || b > 255 || c < 0 || c > 255 || d < 0 || d > 255) {
        return false;
    }
    char rebuilt[16];
    std::snprintf(rebuilt, sizeof(rebuilt), "%d.%d.%d.%d", a, b, c, d);
    return s == rebuilt;
}

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
    if (sntp_server.size() > 63U) {
        error = "SNTP server name is too long (max 63 characters).";
        return false;
    }
    for (const char ch : sntp_server) {
        if (ch <= ' ' || ch > '~') {
            error = "SNTP server contains invalid characters.";
            return false;
        }
    }
    if (sta_use_static_ip) {
        if (!isValidIpv4(sta_ip)) {
            error = "IP address is not a valid IPv4 address.";
            return false;
        }
        if (!isValidIpv4(sta_netmask)) {
            error = "Subnet mask is not a valid IPv4 address.";
            return false;
        }
        if (!isValidIpv4(sta_gateway)) {
            error = "Gateway is not a valid IPv4 address.";
            return false;
        }
        if (!sta_dns.empty() && !isValidIpv4(sta_dns)) {
            error = "DNS server is not a valid IPv4 address.";
            return false;
        }
    }
    if (cellular_enabled) {
        if (cellular_apn.empty()) {
            error = "APN is required when cellular uplink is enabled.";
            return false;
        }
        if (cellular_apn.size() > 63U) {
            error = "APN is too long (max 63 characters).";
            return false;
        }
    }
    if (cellular_username.size() > 31U) {
        error = "Cellular username is too long (max 31 characters).";
        return false;
    }
    if (cellular_password.size() > 63U) {
        error = "Cellular password is too long (max 63 characters).";
        return false;
    }
    if (cellular_sim_pin.size() > 7U) {
        error = "SIM PIN is too long (max 7 characters).";
        return false;
    }
    for (const char ch : cellular_sim_pin) {
        if (ch < '0' || ch > '9') {
            error = "SIM PIN must contain digits only.";
            return false;
        }
    }
    if (cellular_connectivity_check_host.size() > 63U) {
        error = "Connectivity check host is too long (max 63 characters).";
        return false;
    }
    if (cellular_wifi_debug_window_s > 3600UL) {
        error = "Wi-Fi debug window must be 0–3600 seconds.";
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
    std::uint16_t port) {
    if (handle_ != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    status_service_ = &status_service;
    network_manager_ = &network_manager;
    config_repository_ = &config_repository;
    config_ = &config;
    sensor_config_repository_ = &sensor_config_repository;
    sensor_config_list_ = &sensor_config_list;
    sensor_manager_ = &sensor_manager;
    measurement_store_ = &measurement_store;
    backend_config_repository_ = &backend_config_repository;
    backend_config_list_ = &backend_config_list;
    upload_manager_ = &upload_manager;
    cellular_config_repository_ = &cellular_config_repository;
    cellular_config_ = &cellular_config;
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

    httpd_uri_t diagnostics_uri{};
    diagnostics_uri.uri = "/diagnostics";
    diagnostics_uri.method = HTTP_GET;
    diagnostics_uri.handler = &WebServer::handleDiagnostics;
    diagnostics_uri.user_ctx = this;
    err = httpd_register_uri_handler(handle_, &diagnostics_uri);
    if (err != ESP_OK) {
        stop();
        return err;
    }

    httpd_uri_t logs_data_uri{};
    logs_data_uri.uri = "/logs/data";
    logs_data_uri.method = HTTP_GET;
    logs_data_uri.handler = &WebServer::handleLogsData;
    logs_data_uri.user_ctx = this;
    err = httpd_register_uri_handler(handle_, &logs_data_uri);
    if (err != ESP_OK) {
        stop();
        return err;
    }

    httpd_uri_t wifi_scan_uri{};
    wifi_scan_uri.uri = "/wifi-scan";
    wifi_scan_uri.method = HTTP_GET;
    wifi_scan_uri.handler = &WebServer::handleWifiScan;
    wifi_scan_uri.user_ctx = this;
    err = httpd_register_uri_handler(handle_, &wifi_scan_uri);
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

    httpd_uri_t check_sntp_uri{};
    check_sntp_uri.uri = "/check-sntp";
    check_sntp_uri.method = HTTP_POST;
    check_sntp_uri.handler = &WebServer::handleCheckSntp;
    check_sntp_uri.user_ctx = this;
    err = httpd_register_uri_handler(handle_, &check_sntp_uri);
    if (err != ESP_OK) {
        stop();
        return err;
    }

    const esp_err_t captive_err = captive_portal_register_catchall(handle_, nullptr);
    if (captive_err != ESP_OK) {
        ESP_LOGW(kTag, "Captive portal registration failed: %s", esp_err_to_name(captive_err));
    }

    ESP_LOGI(kTag, "HTTP server listening on port %" PRIu16, port);
    return ESP_OK;
}

esp_err_t WebServer::handleAsset(httpd_req_t* request) {
    return sendAssetResponse(request, assetPathFromUri(request->uri));
}

esp_err_t WebServer::handleSensors(httpd_req_t* request) {
    auto* server = static_cast<WebServer*>(request->user_ctx);
    if (server->status_service_->networkState().mode == NetworkMode::kSetupAp) {
        httpd_resp_set_status(request, "302 Found");
        httpd_resp_set_hdr(request, "Location", "/config");
        return httpd_resp_send(request, nullptr, 0);
    }

    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");

    if (request->method == HTTP_GET) {
        const std::string html = renderSensorsPage(
            server->staged_sensor_config_,
            *server->sensor_manager_,
            *server->measurement_store_,
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
            *server->measurement_store_,
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
                *server->measurement_store_,
                server->has_pending_sensor_changes_,
                std::string("Failed to save sensor configuration: ") + esp_err_to_name(save_err),
                true);
            return httpd_resp_send(request, html.c_str(), html.size());
        }

        *server->sensor_config_list_ = server->staged_sensor_config_;
        server->sensor_manager_->applyConfig(*server->sensor_config_list_);
        server->has_pending_sensor_changes_ = false;
        const std::string html = renderSensorsPage(
            server->staged_sensor_config_,
            *server->sensor_manager_,
            *server->measurement_store_,
            server->has_pending_sensor_changes_,
            "Sensor configuration saved and applied.",
            false);
        return httpd_resp_send(request, html.c_str(), html.size());
    } else if (action == "discard") {
        server->staged_sensor_config_ = *server->sensor_config_list_;
        server->has_pending_sensor_changes_ = false;
        const std::string html = renderSensorsPage(
            server->staged_sensor_config_,
            *server->sensor_manager_,
            *server->measurement_store_,
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
                *server->measurement_store_,
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
                *server->measurement_store_,
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
                *server->measurement_store_,
                server->has_pending_sensor_changes_,
                "Invalid numeric sensor fields.",
                true);
            return httpd_resp_send(request, html.c_str(), html.size());
        }
        if (poll_interval_ms < kMinSensorPollIntervalMs) {
            const std::string html = renderSensorsPage(
                server->staged_sensor_config_,
                *server->sensor_manager_,
                *server->measurement_store_,
                server->has_pending_sensor_changes_,
                "Poll interval must be at least 5000 ms.",
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
                *server->measurement_store_,
                server->has_pending_sensor_changes_,
                "Sensor pin must be a valid integer.",
                true);
            return httpd_resp_send(request, html.c_str(), html.size());
        }
        analog_pin = static_cast<std::int16_t>(parsed_signed);

        const std::string i2c_address_value = findFormValue(fields, "i2c_address");
        std::uint8_t parsed_i2c_address = 0U;
        if (!i2c_address_value.empty() &&
            !parseI2cAddress(i2c_address_value, parsed_i2c_address)) {
            const std::string html = renderSensorsPage(
                server->staged_sensor_config_,
                *server->sensor_manager_,
                *server->measurement_store_,
                server->has_pending_sensor_changes_,
                "I2C address must be a valid value like 0x76.",
                true);
            return httpd_resp_send(request, html.c_str(), html.size());
        }

        SensorRecord record{};
        const SensorRecord* existing = nullptr;
        if (action == "update") {
            unsigned long sensor_id = 0UL;
            if (!parseUnsignedLong(findFormValue(fields, "sensor_id"), sensor_id)) {
                const std::string html = renderSensorsPage(
                    server->staged_sensor_config_,
                    *server->sensor_manager_,
                    *server->measurement_store_,
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
                    *server->measurement_store_,
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

        record.transport_kind = inferredTransportKind(*descriptor);
        switch (record.transport_kind) {
            case TransportKind::kI2c:
                if (type_changed) {
                    record.i2c_bus_id = descriptor->default_i2c_bus_id;
                    record.i2c_address = descriptor->default_i2c_address;
                }
                if (!i2c_address_value.empty()) {
                    record.i2c_address = parsed_i2c_address;
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
                    *server->measurement_store_,
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
                *server->measurement_store_,
                server->has_pending_sensor_changes_,
                validation_error,
                true);
            return httpd_resp_send(request, html.c_str(), html.size());
        }

        if (!validateSensorCategorySelection(updated, record, validation_error)) {
            const std::string html = renderSensorsPage(
                server->staged_sensor_config_,
                *server->sensor_manager_,
                *server->measurement_store_,
                server->has_pending_sensor_changes_,
                validation_error,
                true);
            return httpd_resp_send(request, html.c_str(), html.size());
        }
    } else {
        const std::string html = renderSensorsPage(
            server->staged_sensor_config_,
            *server->sensor_manager_,
            *server->measurement_store_,
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
        *server->measurement_store_,
        server->has_pending_sensor_changes_,
        action == "delete" ? "Sensor deletion staged." : "Sensor changes staged in memory.",
        false);
    return httpd_resp_send(request, html.c_str(), html.size());
}

esp_err_t WebServer::handleBackends(httpd_req_t* request) {
    auto* server = static_cast<WebServer*>(request->user_ctx);
    if (server->status_service_->networkState().mode == NetworkMode::kSetupAp) {
        httpd_resp_set_status(request, "302 Found");
        httpd_resp_set_hdr(request, "Location", "/config");
        return httpd_resp_send(request, nullptr, 0);
    }

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
        if (record->enabled == 0U) {
            continue;
        }

        if (descriptor.type == BackendType::kInfluxDb) {
            BackendHttpConfigView backend_config = backendHttpConfigForUi(record, descriptor.type);
            backend_config.use_https =
                formHasKey(fields, (std::string("use_https_") + descriptor.backend_key).c_str());
            backend_config.host =
                findFormValue(fields, (std::string("host_") + descriptor.backend_key).c_str());
            backend_config.path =
                findFormValue(fields, (std::string("path_") + descriptor.backend_key).c_str());
            backend_config.username =
                findFormValue(fields, (std::string("user_") + descriptor.backend_key).c_str());
            backend_config.password =
                findFormValue(fields, (std::string("password_") + descriptor.backend_key).c_str());
            backend_config.measurement_name =
                findFormValue(fields, (std::string("measurement_") + descriptor.backend_key).c_str());

            const std::string port_value =
                findFormValue(fields, (std::string("port_") + descriptor.backend_key).c_str());
            unsigned long port = 0UL;
            if (!port_value.empty()) {
                if (!parseUnsignedLong(port_value, port) || port == 0UL || port > 65535UL) {
                    const std::string html = renderBackendsPage(
                        *server->backend_config_list_,
                        *server->upload_manager_,
                        server->status_service_->buildInfo(),
                        "InfluxDB port must be between 1 and 65535.",
                        true);
                    return sendHtmlResponse(request, html);
                }
            }
            backend_config.port = static_cast<std::uint16_t>(port);

            std::string encode_error;
            if (!encodeBackendHttpRecord(backend_config, *record, encode_error)) {
                const std::string html = renderBackendsPage(
                    *server->backend_config_list_,
                    *server->upload_manager_,
                    server->status_service_->buildInfo(),
                    encode_error.empty() ? "InfluxDB configuration is invalid." : encode_error,
                    true);
                return sendHtmlResponse(request, html);
            }
        } else if (descriptor.type == BackendType::kCustomUpload) {
            const std::string endpoint_field_name =
                std::string("endpoint_url_") + descriptor.backend_key;
            const std::string endpoint_url = findFormValue(fields, endpoint_field_name.c_str());
            if (endpoint_url.size() >= sizeof(record->endpoint_url) ||
                (!endpoint_url.empty() && !hasSupportedEndpointScheme(endpoint_url)) ||
                (record->enabled != 0U && endpoint_url.empty())) {
                const std::string html = renderBackendsPage(
                    *server->backend_config_list_,
                    *server->upload_manager_,
                    server->status_service_->buildInfo(),
                    "Custom upload endpoint must be a full http:// or https:// URL.",
                    true);
                return sendHtmlResponse(request, html);
            }
            copyString(record->endpoint_url, sizeof(record->endpoint_url), endpoint_url);
        } else {
            const std::string use_https_name = std::string("use_https_") + descriptor.backend_key;
            BackendHttpConfigView backend_config = backendHttpConfigForUi(record, descriptor.type);
            backend_config.use_https = formHasKey(fields, use_https_name.c_str());
            std::string encode_error;
            if (!encodeBackendHttpRecord(backend_config, *record, encode_error)) {
                const std::string html = renderBackendsPage(
                    *server->backend_config_list_,
                    *server->upload_manager_,
                    server->status_service_->buildInfo(),
                    encode_error.empty() ? "Backend configuration is invalid." : encode_error,
                    true);
                return sendHtmlResponse(request, html);
            }
        }

        if (descriptor.type == BackendType::kSensorCommunity) {
            const std::string device_id_name = std::string("device_id_") + descriptor.backend_key;
            copyString(
                record->device_id_override,
                sizeof(record->device_id_override),
                findFormValue(fields, device_id_name.c_str()));
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

esp_err_t WebServer::handleDiagnostics(httpd_req_t* request) {
    auto* server = static_cast<WebServer*>(request->user_ctx);
    const std::string html = server->status_service_->renderDiagnosticsHtml(logBufferGetContents());
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, html.c_str(), html.size());
}

esp_err_t WebServer::handleLogsData(httpd_req_t* request) {
    const std::string contents = logBufferGetContents();
    httpd_resp_set_type(request, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, contents.c_str(), contents.size());
}

esp_err_t WebServer::handleWifiScan(httpd_req_t* request) {
    auto* server = static_cast<WebServer*>(request->user_ctx);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");

    if (server->network_manager_->lastScanUptimeMs() == 0U) {
        server->network_manager_->scanAvailableNetworks();
    }

    std::string json;
    json.reserve(1024U);
    json += "{";
    json += "\"networks\":[";
    const auto& networks = server->network_manager_->availableNetworks();
    for (std::size_t index = 0; index < networks.size(); ++index) {
        if (index > 0U) {
            json += ",";
        }

        json += "{";
        json += "\"ssid\":\"";
        json += jsonEscape(networks[index].ssid);
        json += "\",\"rssi\":";
        json += std::to_string(networks[index].rssi);
        json += "}";
    }
    json += "],\"last_scan_uptime_ms\":";
    json += std::to_string(server->network_manager_->lastScanUptimeMs());
    json += ",\"last_scan_error\":\"";
    json += jsonEscape(server->network_manager_->lastScanError());
    json += "\"}";
    return httpd_resp_send(request, json.c_str(), json.size());
}

esp_err_t WebServer::handleConfig(httpd_req_t* request) {
    auto* server = static_cast<WebServer*>(request->user_ctx);
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");

    if (request->method == HTTP_GET) {
        const std::string html = renderConfigPage(
            *server->config_,
            *server->cellular_config_,
            server->status_service_->networkState(),
            *server->network_manager_,
            "",
            false);
        return httpd_resp_send(request, html.c_str(), html.size());
    }

    std::string body;
    if (readRequestBody(request, body) != ESP_OK) {
        const std::string html = renderConfigPage(
            *server->config_,
            *server->cellular_config_,
            server->status_service_->networkState(),
            *server->network_manager_,
            "Failed to read form body.",
            true);
        return httpd_resp_send(request, html.c_str(), html.size());
    }

    const FormFields fields = parseFormBody(body);

    // --- DeviceConfig fields ---
    const std::string device_name = findFormValue(fields, "device_name");
    const std::string wifi_ssid = findFormValue(fields, "wifi_ssid");
    const std::string wifi_password = findFormValue(fields, "wifi_password");
    const std::string sntp_server = findFormValue(fields, "sntp_server");
    const bool wifi_power_save_enabled = (findFormValue(fields, "wifi_power_save") == "1");
    const bool sta_use_static_ip = (findFormValue(fields, "sta_use_static_ip") == "1");
    const std::string sta_ip = sta_use_static_ip ? findFormValue(fields, "sta_ip") : "";
    const std::string sta_netmask = sta_use_static_ip ? findFormValue(fields, "sta_netmask") : "";
    const std::string sta_gateway = sta_use_static_ip ? findFormValue(fields, "sta_gateway") : "";
    const std::string sta_dns = sta_use_static_ip ? findFormValue(fields, "sta_dns") : "";

    // --- BLE fields ---
    const bool ble_advertise_enabled = (findFormValue(fields, "ble_advertise_enabled") == "1");
    unsigned long ble_adv_interval_index = kBleAdvIntervalDefaultIndex;
    parseUnsignedLong(findFormValue(fields, "ble_adv_interval_index"), ble_adv_interval_index);
    if (ble_adv_interval_index >= kBleAdvIntervalCount) {
        ble_adv_interval_index = kBleAdvIntervalDefaultIndex;
    }

    // --- CellularConfig fields ---
    const bool cellular_enabled = (findFormValue(fields, "cellular_enabled") == "1");
    const std::string cellular_apn =
        cellular_enabled ? findFormValue(fields, "cellular_apn") : "";
    const std::string cellular_username = findFormValue(fields, "cellular_username");
    const std::string cellular_password = findFormValue(fields, "cellular_password");
    const std::string cellular_sim_pin = findFormValue(fields, "cellular_sim_pin");
    const std::string cellular_connectivity_check_host =
        findFormValue(fields, "cellular_connectivity_check_host");

    unsigned long cellular_wifi_debug_window_s = server->cellular_config_->wifi_debug_window_s;
    parseUnsignedLong(findFormValue(fields, "cellular_wifi_debug_window_s"),
                      cellular_wifi_debug_window_s);

    // --- Validate ---
    std::string validation_error;
    if (!validateConfigForm(
            device_name,
            wifi_ssid,
            wifi_password,
            sntp_server,
            sta_use_static_ip,
            sta_ip,
            sta_netmask,
            sta_gateway,
            sta_dns,
            cellular_enabled,
            cellular_apn,
            cellular_username,
            cellular_password,
            cellular_sim_pin,
            cellular_connectivity_check_host,
            cellular_wifi_debug_window_s,
            validation_error)) {
        DeviceConfig preview = *server->config_;
        copyString(preview.device_name, sizeof(preview.device_name), device_name);
        copyString(preview.wifi_sta_ssid, sizeof(preview.wifi_sta_ssid), wifi_ssid);
        copyString(preview.wifi_sta_password, sizeof(preview.wifi_sta_password), wifi_password);
        copyString(preview.sntp_server, sizeof(preview.sntp_server), sntp_server);
        preview.wifi_power_save_enabled = wifi_power_save_enabled ? 1U : 0U;
        preview.ble_advertise_enabled = ble_advertise_enabled ? 1U : 0U;
        preview.ble_adv_interval_index = static_cast<std::uint8_t>(ble_adv_interval_index);
        preview.sta_use_static_ip = sta_use_static_ip ? 1U : 0U;
        copyString(preview.sta_ip, sizeof(preview.sta_ip), sta_ip);
        copyString(preview.sta_netmask, sizeof(preview.sta_netmask), sta_netmask);
        copyString(preview.sta_gateway, sizeof(preview.sta_gateway), sta_gateway);
        copyString(preview.sta_dns, sizeof(preview.sta_dns), sta_dns);

        CellularConfig preview_cellular = *server->cellular_config_;
        preview_cellular.enabled = cellular_enabled ? 1U : 0U;
        copyString(preview_cellular.apn, sizeof(preview_cellular.apn), cellular_apn);
        copyString(preview_cellular.username, sizeof(preview_cellular.username), cellular_username);
        copyString(preview_cellular.password, sizeof(preview_cellular.password), cellular_password);
        copyString(preview_cellular.sim_pin, sizeof(preview_cellular.sim_pin), cellular_sim_pin);
        copyString(preview_cellular.connectivity_check_host,
                   sizeof(preview_cellular.connectivity_check_host),
                   cellular_connectivity_check_host);
        preview_cellular.wifi_debug_window_s =
            static_cast<std::uint16_t>(cellular_wifi_debug_window_s);

        const std::string html = renderConfigPage(
            preview,
            preview_cellular,
            server->status_service_->networkState(),
            *server->network_manager_,
            validation_error,
            true);
        return httpd_resp_send(request, html.c_str(), html.size());
    }

    // --- Build and save DeviceConfig ---
    DeviceConfig updated = *server->config_;
    updated.magic = kDeviceConfigMagic;
    updated.schema_version = kDeviceConfigSchemaVersion;
    updated.record_size = static_cast<std::uint16_t>(sizeof(DeviceConfig));
    copyString(updated.device_name, sizeof(updated.device_name), device_name);
    copyString(updated.wifi_sta_ssid, sizeof(updated.wifi_sta_ssid), wifi_ssid);
    copyString(updated.wifi_sta_password, sizeof(updated.wifi_sta_password), wifi_password);
    copyString(updated.sntp_server, sizeof(updated.sntp_server), sntp_server);
    updated.wifi_power_save_enabled = wifi_power_save_enabled ? 1U : 0U;
    updated.ble_advertise_enabled = ble_advertise_enabled ? 1U : 0U;
    updated.ble_adv_interval_index = static_cast<std::uint8_t>(ble_adv_interval_index);
    updated.sta_use_static_ip = sta_use_static_ip ? 1U : 0U;
    copyString(updated.sta_ip, sizeof(updated.sta_ip), sta_ip);
    copyString(updated.sta_netmask, sizeof(updated.sta_netmask), sta_netmask);
    copyString(updated.sta_gateway, sizeof(updated.sta_gateway), sta_gateway);
    copyString(updated.sta_dns, sizeof(updated.sta_dns), sta_dns);

    const esp_err_t save_err = server->config_repository_->save(updated);
    if (save_err != ESP_OK) {
        const std::string html = renderConfigPage(
            updated,
            *server->cellular_config_,
            server->status_service_->networkState(),
            *server->network_manager_,
            std::string("Failed to save device configuration: ") + esp_err_to_name(save_err),
            true);
        return httpd_resp_send(request, html.c_str(), html.size());
    }

    // --- Build and save CellularConfig ---
    CellularConfig updated_cellular = *server->cellular_config_;
    updated_cellular.magic = kCellularConfigMagic;
    updated_cellular.schema_version = kCellularConfigSchemaVersion;
    updated_cellular.record_size = static_cast<std::uint16_t>(sizeof(CellularConfig));
    updated_cellular.enabled = cellular_enabled ? 1U : 0U;
    copyString(updated_cellular.apn, sizeof(updated_cellular.apn), cellular_apn);
    copyString(updated_cellular.username, sizeof(updated_cellular.username), cellular_username);
    copyString(updated_cellular.password, sizeof(updated_cellular.password), cellular_password);
    copyString(updated_cellular.sim_pin, sizeof(updated_cellular.sim_pin), cellular_sim_pin);
    copyString(updated_cellular.connectivity_check_host,
               sizeof(updated_cellular.connectivity_check_host),
               cellular_connectivity_check_host);
    updated_cellular.wifi_debug_window_s =
        static_cast<std::uint16_t>(cellular_wifi_debug_window_s);

    const esp_err_t cellular_save_err =
        server->cellular_config_repository_->save(updated_cellular);
    if (cellular_save_err != ESP_OK) {
        const std::string html = renderConfigPage(
            updated,
            updated_cellular,
            server->status_service_->networkState(),
            *server->network_manager_,
            std::string("Failed to save cellular configuration: ") +
                esp_err_to_name(cellular_save_err),
            true);
        return httpd_resp_send(request, html.c_str(), html.size());
    }

    *server->config_ = updated;
    *server->cellular_config_ = updated_cellular;
    server->status_service_->setConfig(updated, true, false);
    const std::string html = renderConfigPage(
        updated,
        updated_cellular,
        server->status_service_->networkState(),
        *server->network_manager_,
        "Configuration saved. Device is rebooting now.",
        false);
    esp_err_t response_err = httpd_resp_send(request, html.c_str(), html.size());
    if (response_err == ESP_OK) {
        scheduleRestart();
    }
    return response_err;
}

esp_err_t WebServer::handleCheckSntp(httpd_req_t* request) {
    auto* server = static_cast<WebServer*>(request->user_ctx);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");

    std::string body;
    if (readRequestBody(request, body) != ESP_OK) {
        return httpd_resp_sendstr(request, "{\"success\":false,\"error\":\"sync_failed\"}");
    }

    const FormFields fields = parseFormBody(body);
    const std::string sntp_server = findFormValue(fields, "server");

    const SntpCheckResult result = server->network_manager_->checkSntp(sntp_server);

    if (result.success) {
        return httpd_resp_sendstr(request, "{\"success\":true}");
    }

    std::string response = "{\"success\":false,\"error\":\"";
    response += jsonEscape(result.error);
    response += "\"}";
    return httpd_resp_sendstr(request, response.c_str());
}

}  // namespace air360
