#include "air360/web_server.hpp"

#include <array>
#include <cinttypes>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

#include "air360/sensors/sensor_config_repository.hpp"
#include "air360/string_utils.hpp"
#include "air360/sensors/sensor_manager.hpp"
#include "air360/sensors/sensor_registry.hpp"
#include "air360/sensors/sensor_types.hpp"
#include "air360/uploads/backend_config.hpp"
#include "air360/uploads/backend_registry.hpp"
#include "air360/uploads/upload_manager.hpp"
#include "air360/web_server_internal.hpp"
#include "air360/web_ui.hpp"
#include "esp_log.h"
#include "esp_netif.h"
#include "captive_portal.h"
#include "sdkconfig.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.web";
// 10 KB stack leaves headroom for std::string-heavy HTML rendering in route
// handlers without risking overflow in the HTTP server task.
constexpr std::size_t kHttpServerStackSize = 10240U;
// Current route set plus captive-portal catchall fits in 15 slots while
// leaving a little room for future diagnostics endpoints.
constexpr std::size_t kHttpServerMaxUriHandlers = 15U;
// Match the save-time validation floor so the web UI cannot submit a poll
// interval below what SensorManager supports at runtime.
constexpr std::uint32_t kMinSensorPollIntervalMs = 5000U;

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
    BackendType backend_type = BackendType::kUnknown;
    bool use_https = true;
    std::string endpoint;
    std::string host;
    std::string path;
    std::string port;
    std::string username;
    std::string password;
    std::string measurement_name;
    std::string device_id_override;
    // Status:
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
    std::uint32_t failures = 0U;
    std::uint64_t next_retry_ms = 0U;
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
    html.reserve(256U);
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

constexpr std::array<SensorType, 7U> kClimateSensorTypes{{
    SensorType::kBme280,
    SensorType::kBme680,
    SensorType::kDht11,
    SensorType::kDht22,
    SensorType::kDs18b20,
    SensorType::kHtu2x,
    SensorType::kSht4x,
}};

constexpr std::array<SensorType, 1U> kLightSensorTypes{{
    SensorType::kVeml7700,
}};

constexpr std::array<SensorType, 1U> kParticulateMatterSensorTypes{{
    SensorType::kSps30,
}};

constexpr std::array<SensorType, 1U> kLocationSensorTypes{{
    SensorType::kGpsNmea,
}};

constexpr std::array<SensorType, 3U> kGasSensorTypes{{
    SensorType::kScd30,
    SensorType::kMe3No2,
    SensorType::kMhz19b,
}};

constexpr std::array<SensorType, 1U> kPowerSensorTypes{{
    SensorType::kIna219,
}};

constexpr std::array<SensorCategoryDescriptor, 6U> kSensorCategoryDescriptors{{
    {
        SensorCategory::kClimate,
        "climate",
        "Climate",
        "Temperature, humidity, pressure, and related climate measurements over GPIO or I2C.",
        false,
        kClimateSensorTypes.data(),
        kClimateSensorTypes.size(),
    },
    {
        SensorCategory::kParticulateMatter,
        "particulate-matter",
        "Particulate Matter",
        "Fine dust concentration and particle size metrics.",
        false,
        kParticulateMatterSensorTypes.data(),
        kParticulateMatterSensorTypes.size(),
    },
    {
        SensorCategory::kLight,
        "light",
        "Light",
        "Ambient light and illuminance sensing.",
        false,
        kLightSensorTypes.data(),
        kLightSensorTypes.size(),
    },
    {
        SensorCategory::kLocation,
        "location",
        "Location",
        "GPS coordinates, altitude, movement, and fix state.",
        false,
        kLocationSensorTypes.data(),
        kLocationSensorTypes.size(),
    },
    {
        SensorCategory::kGas,
        "gas",
        "Gas",
        "Gas sensors. Multiple gas sensors are allowed.",
        true,
        kGasSensorTypes.data(),
        kGasSensorTypes.size(),
    },
    {
        SensorCategory::kPower,
        "power",
        "Power",
        "DC current, voltage, and power monitoring over I2C.",
        false,
        kPowerSensorTypes.data(),
        kPowerSensorTypes.size(),
    },
}};

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
    html.reserve(512U);
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
    const WifiScanSnapshot wifi_scan = network_manager.wifiScanSnapshot();

    appendWifiSsidOption(
        model.wifi_ssid_options_html,
        "",
        "Select network...",
        model.wifi_ssid_value.empty());

    bool selected_network_present = false;
    for (const auto& network : wifi_scan.networks) {
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

    for (const auto& network : wifi_scan.networks) {
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

std::string renderHttpsCheckbox(const BackendCardViewModel& card) {
    std::string html;
    html.reserve(256U);
    html += "<label class='checkbox'>";
    html += "<input type='checkbox' name='use_https_";
    html += htmlEscape(card.backend_key);
    html += "' value='1' data-backend-https-toggle";
    if (card.use_https) {
        html += " checked";
    }
    html += "><span class='checkbox__label'>Use HTTPS</span></label>";
    return html;
}

std::string renderEndpointFields(const BackendCardViewModel& card) {
    std::string html;
    html.reserve(1024U);
    html += "<div class='field'><label for='host_";
    html += htmlEscape(card.backend_key);
    html += "'>Host</label><input class='input' id='host_";
    html += htmlEscape(card.backend_key);
    html += "' name='host_";
    html += htmlEscape(card.backend_key);
    html += "' maxlength='";
    html += std::to_string(kBackendHostCapacity - 1U);
    html += "' value='";
    html += htmlEscape(card.host);
    html += "'></div>";

    html += "<div class='field'><label for='path_";
    html += htmlEscape(card.backend_key);
    html += "'>Path</label><input class='input' id='path_";
    html += htmlEscape(card.backend_key);
    html += "' name='path_";
    html += htmlEscape(card.backend_key);
    html += "' maxlength='";
    html += std::to_string(kBackendPathCapacity - 1U);
    html += "' placeholder='/write?db=air360' value='";
    html += htmlEscape(card.path);
    html += "'></div>";

    html += "<div class='field'><label for='port_";
    html += htmlEscape(card.backend_key);
    html += "'>Port</label><input class='input' id='port_";
    html += htmlEscape(card.backend_key);
    html += "' name='port_";
    html += htmlEscape(card.backend_key);
    html += "' inputmode='numeric' maxlength='5' data-backend-port-input value='";
    html += htmlEscape(card.port);
    html += "'></div>";
    return html;
}

std::string renderAuthFields(const BackendCardViewModel& card) {
    std::string html;
    html.reserve(768U);
    html += "<div class='field'><label for='user_";
    html += htmlEscape(card.backend_key);
    html += "'>User</label><input class='input' id='user_";
    html += htmlEscape(card.backend_key);
    html += "' name='user_";
    html += htmlEscape(card.backend_key);
    html += "' maxlength='";
    html += std::to_string(kBackendUsernameCapacity - 1U);
    html += "' value='";
    html += htmlEscape(card.username);
    html += "'></div>";

    html += "<div class='field'><label for='password_";
    html += htmlEscape(card.backend_key);
    html += "'>Password</label><input class='input' type='password' id='password_";
    html += htmlEscape(card.backend_key);
    html += "' name='password_";
    html += htmlEscape(card.backend_key);
    html += "' maxlength='";
    html += std::to_string(kBackendPasswordCapacity - 1U);
    html += "' value='";
    html += htmlEscape(card.password);
    html += "'></div>";
    return html;
}

std::string renderBackendCard(const BackendCardViewModel& card) {
    std::string https_block;
    https_block.reserve(256U);
    std::string endpoint_block;
    endpoint_block.reserve(1024U);
    std::string device_id_override_block;
    device_id_override_block.reserve(512U);

    switch (card.backend_type) {
        case BackendType::kSensorCommunity:
            https_block = renderHttpsCheckbox(card);
            if (!card.endpoint.empty()) {
                endpoint_block += "<p>Endpoint: <code>";
                endpoint_block += htmlEscape(card.endpoint);
                endpoint_block += "</code></p>";
            }
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
            break;

        case BackendType::kAir360Api:
            https_block = renderHttpsCheckbox(card);
            if (!card.endpoint.empty()) {
                endpoint_block += "<p>Endpoint: <code>";
                endpoint_block += htmlEscape(card.endpoint);
                endpoint_block += "</code></p>";
            }
            break;

        case BackendType::kCustomUpload:
            https_block = renderHttpsCheckbox(card);
            endpoint_block = renderEndpointFields(card);
            break;

        case BackendType::kInfluxDb:
            https_block = renderHttpsCheckbox(card);
            endpoint_block = renderEndpointFields(card);
            endpoint_block += renderAuthFields(card);
            endpoint_block += "<div class='field'><label for='measurement_";
            endpoint_block += htmlEscape(card.backend_key);
            endpoint_block += "'>Measurement</label><input class='input' id='measurement_";
            endpoint_block += htmlEscape(card.backend_key);
            endpoint_block += "' name='measurement_";
            endpoint_block += htmlEscape(card.backend_key);
            endpoint_block += "' maxlength='";
            endpoint_block += std::to_string(kBackendMeasurementCapacity - 1U);
            endpoint_block += "' value='";
            endpoint_block += htmlEscape(card.measurement_name);
            endpoint_block += "'></div>";
            endpoint_block += "<p class='hint'>Sends Influx line protocol with one line per sensor sample group.</p>";
            break;

        default:
            break;
    }

    std::string status_block;
    status_block.reserve(512U);
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
        card.backend_type = descriptor.type;
        card.enabled = record != nullptr && record->enabled != 0U;

        if (record != nullptr) {
            card.use_https = record->protocol == BackendProtocol::kHttps;
            card.endpoint = formatBackendDisplayEndpoint(*record);
            card.host = boundedCString(record->host, kBackendHostCapacity);
            card.path = boundedCString(record->path, kBackendPathCapacity);
            card.port = std::to_string(record->port);
            card.username = boundedCString(record->auth.basic_username, kBackendUsernameCapacity);
            card.password = boundedCString(record->auth.basic_password, kBackendPasswordCapacity);
            card.measurement_name =
                boundedCString(record->influxdb_measurement, kBackendMeasurementCapacity);
            card.device_id_override =
                boundedCString(record->sensor_community_device_id, kBackendIdentifierCapacity);
            if (card.device_id_override.empty()) {
                card.device_id_override = build_info.short_chip_id;
            }
        } else {
            card.use_https = descriptor.defaults.protocol == BackendProtocol::kHttps;
            card.port = std::to_string(descriptor.defaults.port);
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
    runtime_error_block.reserve(256U);
    if (!card.runtime_error.empty()) {
        runtime_error_block += "<p>Runtime error: <code>";
        runtime_error_block += htmlEscape(card.runtime_error);
        runtime_error_block += "</code></p>";
    }
    if (card.failures > 0U) {
        runtime_error_block += "<p>Runtime failures: <code>";
        runtime_error_block += std::to_string(card.failures);
        runtime_error_block += "</code>";
        if (card.next_retry_ms > 0U) {
            runtime_error_block += " next retry uptime <code>";
            runtime_error_block += std::to_string(card.next_retry_ms);
            runtime_error_block += " ms</code>";
        }
        runtime_error_block += "</p>";
    }

    std::string latest_reading_block;
    latest_reading_block.reserve(256U);
    if (!card.latest_reading.empty()) {
        latest_reading_block += "<p>Latest reading: <code>";
        latest_reading_block += htmlEscape(card.latest_reading);
        latest_reading_block += "</code></p>";
    }

    std::string i2c_field_block;
    i2c_field_block.reserve(384U);
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
    gpio_field_block.reserve(384U);
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
    add_form_block.reserve(section.show_add_form ? 4096U : 0U);
    if (section.show_add_form) {
        std::string i2c_field_block;
        i2c_field_block.reserve(512U);
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
        gpio_field_block.reserve(512U);
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
    section_html.reserve(cards_html.size() + add_form_block.size() + 1024U);
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
    model.sections.reserve(kSensorCategoryDescriptors.size());

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
            card.failures = runtime_info->failures;
            card.next_retry_ms = runtime_info->next_retry_ms;
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
    const char* static_ip_error = nullptr;
    if (!validateStaticIpv4Config(
            sta_use_static_ip,
            sta_ip,
            sta_netmask,
            sta_gateway,
            sta_dns,
            static_ip_error)) {
        error = static_ip_error == nullptr ? "Static IPv4 configuration is invalid."
                                           : static_ip_error;
        return false;
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

}  // namespace

namespace web {

bool parseUnsignedLong(const std::string& input, unsigned long& value, int base) {
    return ::air360::parseUnsignedLong(input, value, base);
}

bool parseSignedLong(const std::string& input, long& value) {
    return ::air360::parseSignedLong(input, value);
}

bool parseI2cAddress(const std::string& input, std::uint8_t& value) {
    return ::air360::parseI2cAddress(input, value);
}

TransportKind inferredTransportKind(const SensorDescriptor& descriptor) {
    return ::air360::inferredTransportKind(descriptor);
}

std::int16_t defaultBoardGpioPin() {
    return ::air360::defaultBoardGpioPin();
}

bool validateSensorCategorySelection(
    const SensorConfigList& sensor_config_list,
    const SensorRecord& record,
    std::string& error) {
    return ::air360::validateSensorCategorySelection(sensor_config_list, record, error);
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
    return ::air360::validateConfigForm(
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
        error);
}

std::string renderConfigPage(
    const DeviceConfig& config,
    const CellularConfig& cellular_config,
    const NetworkState& network_state,
    const NetworkManager& network_manager,
    const std::string& notice,
    bool error_notice) {
    return ::air360::renderConfigPage(
        config,
        cellular_config,
        network_state,
        network_manager,
        notice,
        error_notice);
}

std::string renderBackendsPage(
    const BackendConfigList& backend_config_list,
    const UploadManager& upload_manager,
    const BuildInfo& build_info,
    const std::string& notice,
    bool error_notice) {
    return ::air360::renderBackendsPage(
        backend_config_list,
        upload_manager,
        build_info,
        notice,
        error_notice);
}

std::string renderSensorsPage(
    const SensorConfigList& sensor_config_list,
    const SensorManager& sensor_manager,
    const MeasurementStore& measurement_store,
    bool has_pending_changes,
    const std::string& notice,
    bool error_notice) {
    return ::air360::renderSensorsPage(
        sensor_config_list,
        sensor_manager,
        measurement_store,
        has_pending_changes,
        notice,
        error_notice);
}

}  // namespace web

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

    httpd_uri_t wifi_scan_refresh_uri{};
    wifi_scan_refresh_uri.uri = "/wifi-scan";
    wifi_scan_refresh_uri.method = HTTP_POST;
    wifi_scan_refresh_uri.handler = &WebServer::handleWifiScanRefresh;
    wifi_scan_refresh_uri.user_ctx = this;
    err = httpd_register_uri_handler(handle_, &wifi_scan_refresh_uri);
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

void WebServer::stop() {
    if (handle_ != nullptr) {
        httpd_stop(handle_);
        handle_ = nullptr;
    }
}

}  // namespace air360
