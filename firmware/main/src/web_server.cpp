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
constexpr std::size_t kHttpServerMaxUriHandlers = 20U;
constexpr char kAir360MapBaseUrl[] = "https://air360.ru/map";
constexpr char kSensorCommunityMapBaseUrl[] = "https://maps.sensor.community/";
// Match the save-time validation floor so the web UI cannot submit a poll
// interval below what SensorManager supports at runtime.
constexpr std::uint32_t kMinSensorPollIntervalMs = air360::kMinSensorPollIntervalMs;
constexpr std::uint32_t kMaxSensorPollIntervalMs = air360::kMaxSensorPollIntervalMs;

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
    std::string latitude;
    std::string longitude;
    std::string air360_map_url;
    std::string sensor_community_map_url;
    std::string air360_upload_secret_preview;
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
    std::string i2c_address_options_html;
    bool show_uart_port_select = false;
    std::string uart_port_options_html;
    std::string uart_pin_hint;
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
    std::string add_i2c_address_options_html;
    bool add_show_uart_port_select = false;
    std::string add_uart_port_options_html;
    std::string add_uart_pin_hint;
    std::string add_gpio_options_html;
    std::uint32_t add_poll_interval_ms = kDefaultSensorPollIntervalMs;
    bool add_show_gpio_pin_select = false;
    bool show_add_form = false;
    std::string add_button_label;
};

struct SensorsPageViewModel {
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
    const std::string& air360_upload_secret_preview,
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
        "Air360 device configuration",
        "Device Configuration",
        "<div class='actions'>"
        "<button class='btn sm primary' type='submit' form='config-form'>"
        "Save and reboot</button></div>",
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

std::string allowedI2cAddressesValue(const SensorDescriptor& descriptor) {
    std::string value;
    for (std::size_t index = 0; index < descriptor.allowed_i2c_address_count; ++index) {
        if (index > 0U) {
            value += ",";
        }
        value += formatI2cAddress(descriptor.allowed_i2c_addresses[index]);
    }
    return value;
}

std::string formatAllowedI2cAddresses(const SensorDescriptor& descriptor) {
    std::string formatted;
    for (std::size_t index = 0; index < descriptor.allowed_i2c_address_count; ++index) {
        if (index > 0U) {
            formatted += index + 1U == descriptor.allowed_i2c_address_count ? " or " : ", ";
        }
        formatted += formatI2cAddress(descriptor.allowed_i2c_addresses[index]);
    }
    return formatted;
}

void appendI2cAddressOptions(
    std::string& html,
    const SensorDescriptor& descriptor,
    std::uint8_t selected_address) {
    for (std::size_t index = 0; index < descriptor.allowed_i2c_address_count; ++index) {
        const std::uint8_t address = descriptor.allowed_i2c_addresses[index];
        html += "<option value='";
        html += formatI2cAddress(address);
        html += "'";
        if (address == selected_address) {
            html += " selected";
        }
        html += ">";
        html += formatI2cAddress(address);
        html += "</option>";
    }
}

std::string allowedUartBindingsValue(const SensorDescriptor& descriptor) {
    std::string value;
    for (std::size_t index = 0; index < descriptor.allowed_uart_port_count; ++index) {
        const SensorUartPortBinding* binding =
            findSensorUartPortBinding(descriptor.allowed_uart_ports[index]);
        if (binding == nullptr) {
            continue;
        }
        if (!value.empty()) {
            value += ",";
        }
        value += std::to_string(binding->port_id);
        value += ":";
        value += std::to_string(binding->rx_gpio_pin);
        value += ":";
        value += std::to_string(binding->tx_gpio_pin);
    }
    return value;
}

std::string uartPortLabel(const SensorUartPortBinding& binding) {
    std::string label = "UART";
    label += std::to_string(binding.port_id);
    label += " - RX GPIO";
    label += std::to_string(binding.rx_gpio_pin);
    label += ", TX GPIO";
    label += std::to_string(binding.tx_gpio_pin);
    return label;
}

std::string uartPinHint(std::uint8_t port_id) {
    const SensorUartPortBinding* binding = findSensorUartPortBinding(port_id);
    if (binding == nullptr) {
        return "";
    }

    std::string hint = "Pins: RX GPIO";
    hint += std::to_string(binding->rx_gpio_pin);
    hint += ", TX GPIO";
    hint += std::to_string(binding->tx_gpio_pin);
    return hint;
}

void appendUartPortOptions(
    std::string& html,
    const SensorDescriptor& descriptor,
    std::uint8_t selected_port) {
    for (std::size_t index = 0; index < descriptor.allowed_uart_port_count; ++index) {
        const SensorUartPortBinding* binding =
            findSensorUartPortBinding(descriptor.allowed_uart_ports[index]);
        if (binding == nullptr) {
            continue;
        }
        html += "<option value='";
        html += std::to_string(binding->port_id);
        html += "'";
        html += " data-rx-gpio='";
        html += std::to_string(binding->rx_gpio_pin);
        html += "' data-tx-gpio='";
        html += std::to_string(binding->tx_gpio_pin);
        html += "'";
        if (binding->port_id == selected_port) {
            html += " selected";
        }
        html += ">";
        html += htmlEscape(uartPortLabel(*binding));
        html += "</option>";
    }
}

std::string allowedGpioPinsValue(const SensorDescriptor& descriptor) {
    std::string value;
    for (std::size_t index = 0; index < descriptor.allowed_gpio_pin_count; ++index) {
        if (!value.empty()) {
            value += ",";
        }
        value += std::to_string(descriptor.allowed_gpio_pins[index]);
    }
    return value;
}

std::string formatAllowedGpioPins(const SensorDescriptor& descriptor) {
    std::string formatted;
    for (std::size_t index = 0; index < descriptor.allowed_gpio_pin_count; ++index) {
        if (index > 0U) {
            if (descriptor.allowed_gpio_pin_count == 2U) {
                formatted += " or ";
            } else if (index + 1U == descriptor.allowed_gpio_pin_count) {
                formatted += ", or ";
            } else {
                formatted += ", ";
            }
        }
        formatted += "GPIO ";
        formatted += std::to_string(descriptor.allowed_gpio_pins[index]);
    }
    return formatted;
}

void appendGpioPinOptions(
    std::string& html,
    const SensorDescriptor& descriptor,
    std::int16_t selected_pin) {
    for (std::size_t index = 0; index < descriptor.allowed_gpio_pin_count; ++index) {
        const std::int16_t pin = descriptor.allowed_gpio_pins[index];
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

std::uint32_t normalizeSensorPollInterval(std::uint32_t value) {
    if (value < kMinSensorPollIntervalMs) {
        return kMinSensorPollIntervalMs;
    }
    if (value > kMaxSensorPollIntervalMs) {
        return kMaxSensorPollIntervalMs;
    }
    return value;
}

constexpr std::array<SensorType, 9U> kClimateSensorTypes{{
    SensorType::kAht30,
    SensorType::kBme280,
    SensorType::kBme680,
    SensorType::kDht11,
    SensorType::kDht22,
    SensorType::kDs18b20,
    SensorType::kHtu2x,
    SensorType::kSht3x,
    SensorType::kSht4x,
}};

constexpr std::array<SensorType, 1U> kLightSensorTypes{{
    SensorType::kVeml7700,
}};

constexpr std::array<SensorType, 2U> kParticulateMatterSensorTypes{{
    SensorType::kSps30,
    SensorType::kSds011,
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
        case SensorType::kAht30:
        case SensorType::kBme280:
        case SensorType::kBme680:
        case SensorType::kDht11:
        case SensorType::kDht22:
        case SensorType::kDs18b20:
        case SensorType::kHtu2x:
        case SensorType::kSht3x:
        case SensorType::kSht4x:
            return SensorCategory::kClimate;
        case SensorType::kScd30:
            return SensorCategory::kGas;
        case SensorType::kVeml7700:
            return SensorCategory::kLight;
        case SensorType::kSps30:
        case SensorType::kSds011:
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

std::string sensorDefaultsHint(const SensorDescriptor& descriptor) {
    if (descriptor.supports_i2c) {
        std::string hint = "Defaults: I2C bus ";
        hint += std::to_string(descriptor.default_i2c_bus_id);
        hint += " at address ";
        hint += formatI2cAddress(descriptor.default_i2c_address);
        if (descriptor.allowed_i2c_address_count > 1U) {
            hint += " (allowed: ";
            hint += formatAllowedI2cAddresses(descriptor);
            hint += ")";
        }
        hint += ".";
        return hint;
    }

    if (descriptor.supports_uart) {
        std::string hint = "Defaults: UART ";
        hint += std::to_string(descriptor.default_uart_port_id);
        hint += ", ";
        hint += uartPinHint(descriptor.default_uart_port_id);
        hint += " @ ";
        hint += std::to_string(descriptor.default_uart_baud_rate);
        hint += " baud.";
        return hint;
    }

    if (descriptor.supports_gpio || descriptor.supports_analog) {
        std::string hint = "Defaults: choose ";
        hint += formatAllowedGpioPins(descriptor);
        hint += ".";
        return hint;
    }

    switch (descriptor.type) {
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
    html += "' data-requires-uart='";
    html += descriptor.supports_uart ? "true" : "false";
    html += "' data-defaults-hint='";
    html += htmlEscape(sensorDefaultsHint(descriptor));
    html += "' data-default-i2c-address='";
    html += descriptor.supports_i2c ? htmlEscape(formatI2cAddress(descriptor.default_i2c_address))
                                    : "";
    html += "' data-allowed-i2c-addresses='";
    html += descriptor.supports_i2c ? htmlEscape(allowedI2cAddressesValue(descriptor)) : "";
    html += "' data-default-uart-port='";
    html += descriptor.supports_uart ? std::to_string(descriptor.default_uart_port_id) : "";
    html += "' data-allowed-uart-bindings='";
    html += descriptor.supports_uart ? htmlEscape(allowedUartBindingsValue(descriptor)) : "";
    html += "' data-allowed-gpio-pins='";
    html += (descriptor.supports_gpio || descriptor.supports_analog)
        ? htmlEscape(allowedGpioPinsValue(descriptor))
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

std::string formatCoordinate(float value) {
    char buffer[24];
    const int written = std::snprintf(buffer, sizeof(buffer), "%.6f", static_cast<double>(value));
    if (written <= 0) {
        return "";
    }

    std::string formatted(buffer);
    while (!formatted.empty() && formatted.back() == '0') {
        formatted.pop_back();
    }
    if (!formatted.empty() && formatted.back() == '.') {
        formatted.push_back('0');
    }
    return formatted;
}

std::string formatMapCoordinate(float value) {
    char buffer[24];
    const int written = std::snprintf(buffer, sizeof(buffer), "%.4f", static_cast<double>(value));
    return written > 0 ? std::string(buffer) : "";
}

std::string buildMapHash(float latitude, float longitude) {
    const std::string lat = formatMapCoordinate(latitude);
    const std::string lon = formatMapCoordinate(longitude);
    if (lat.empty() || lon.empty()) {
        return "";
    }

    return std::string("#15/") + lat + "/" + lon;
}

std::string renderMapLinksBlock(const BackendCardViewModel& card) {
    if (card.air360_map_url.empty() || card.sensor_community_map_url.empty()) {
        return "";
    }

    std::string html =
        "<div class='backend-project-links backend-map-links'><span>Maps</span>";
    html += "<a href='";
    html += htmlEscape(card.air360_map_url);
    html += "' target='_blank' rel='noopener noreferrer'>Air360</a>";
    html += "<a href='";
    html += htmlEscape(card.sensor_community_map_url);
    html += "' target='_blank' rel='noopener noreferrer'>Sensor.Community</a>";
    html += "</div>";
    return html;
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
    std::string project_links_block;
    project_links_block.reserve(256U);

    switch (card.backend_type) {
        case BackendType::kSensorCommunity:
            https_block = renderHttpsCheckbox(card);
            project_links_block =
                "<div class='backend-project-links'><span>Project</span>"
                "<a href='https://sensor.community/' target='_blank' rel='noopener noreferrer'>"
                "sensor.community</a></div>";
            if (!card.endpoint.empty()) {
                endpoint_block += "<span class='field-hint'>Endpoint: <code>";
                endpoint_block += htmlEscape(card.endpoint);
                endpoint_block += "</code></span>";
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
            device_id_override_block += "<span class='field-hint'>Prefilled from Short ID. Change it only for debugging.</span></div>";
            break;

        case BackendType::kAir360Api: {
            https_block = renderHttpsCheckbox(card);
            project_links_block =
                "<div class='backend-project-links'><span>Project</span>"
                "<a href='https://github.com/serber/air360' target='_blank' rel='noopener noreferrer'>"
                "github.com/serber/air360</a></div>";
            if (!card.endpoint.empty()) {
                endpoint_block += "<span class='field-hint'>Endpoint: <code>";
                endpoint_block += htmlEscape(card.endpoint);
                endpoint_block += "</code></span>";
            }
            endpoint_block += "<div class='field'><label for='lat_";
            endpoint_block += htmlEscape(card.backend_key);
            endpoint_block += "'>Latitude</label>";
            endpoint_block += "<input class='input' id='lat_";
            endpoint_block += htmlEscape(card.backend_key);
            endpoint_block += "' name='lat_";
            endpoint_block += htmlEscape(card.backend_key);
            endpoint_block += "' type='number' step='any' min='-90' max='90' value='";
            endpoint_block += htmlEscape(card.latitude);
            endpoint_block += "' placeholder='e.g. 55.7512' required></div>";
            endpoint_block += "<div class='field'><label for='lon_";
            endpoint_block += htmlEscape(card.backend_key);
            endpoint_block += "'>Longitude</label>";
            endpoint_block += "<input class='input' id='lon_";
            endpoint_block += htmlEscape(card.backend_key);
            endpoint_block += "' name='lon_";
            endpoint_block += htmlEscape(card.backend_key);
            endpoint_block += "' type='number' step='any' min='-180' max='180' value='";
            endpoint_block += htmlEscape(card.longitude);
            endpoint_block += "' placeholder='e.g. 37.6173' required></div>";
            endpoint_block += "<div class='location-map-wrap'>";
            endpoint_block += "<div class='location-map' data-air360-location-map data-lat-input='lat_";
            endpoint_block += htmlEscape(card.backend_key);
            endpoint_block += "' data-lon-input='lon_";
            endpoint_block += htmlEscape(card.backend_key);
            endpoint_block += "'></div>";
            endpoint_block += "<div class='location-map-status' data-air360-location-map-status></div>";
            endpoint_block += "</div>";
            endpoint_block += "<div class='field air360-secret-field'>";
            const std::string secret_input_id =
                std::string("upload_secret_") + card.backend_key;
            const std::string secret_panel_id =
                std::string("upload_secret_panel_") + card.backend_key;
            const bool secret_configured = !card.air360_upload_secret_preview.empty();
            if (secret_configured) {
                endpoint_block += "<label>Upload secret</label>";
                endpoint_block += "<div class='secret-status'>";
                endpoint_block += "<span class='secret-state'>Configured</span>";
                endpoint_block += "<code class='secret-preview'>";
                endpoint_block += htmlEscape(card.air360_upload_secret_preview);
                endpoint_block += "</code>";
                endpoint_block += "<button class='btn' type='button' data-change-air360-secret='";
                endpoint_block += htmlEscape(secret_panel_id);
                endpoint_block += "' data-secret-input='";
                endpoint_block += htmlEscape(secret_input_id);
                endpoint_block += "'>Change</button>";
                endpoint_block += "</div>";
                endpoint_block += "<span class='field-hint'>";
                endpoint_block += "An upload secret is already stored. Keep it unless you need to replace it with a previously saved secret.";
                endpoint_block += "</span>";
                endpoint_block += "<div class='secret-edit-panel' id='";
                endpoint_block += htmlEscape(secret_panel_id);
                endpoint_block += "' hidden>";
            } else {
                endpoint_block += "<label for='";
                endpoint_block += htmlEscape(secret_input_id);
                endpoint_block += "'>Upload secret</label>";
                endpoint_block += "<span class='field-hint'>";
                endpoint_block += "This secret lets the device upload to the same Air360 record after reset. Generate it or paste a saved one, save it somewhere safe, then press Save.";
                endpoint_block += "</span>";
            }
            endpoint_block += "<div class='secret-input-row'>";
            endpoint_block += "<textarea class='input textarea' id='";
            endpoint_block += htmlEscape(secret_input_id);
            endpoint_block += "' name='";
            endpoint_block += htmlEscape(secret_input_id);
            endpoint_block += "' rows='3' maxlength='";
            endpoint_block += std::to_string(kAir360UploadSecretLength);
            endpoint_block += "' autocomplete='off' autocapitalize='off' spellcheck='false' ";
            if (secret_configured) {
                endpoint_block += "disabled ";
            }
            endpoint_block += "placeholder='air360_us_v1_...'></textarea>";
            endpoint_block += "<button class='btn' type='button' data-generate-air360-secret='";
            endpoint_block += htmlEscape(secret_input_id);
            endpoint_block += "'>Generate</button>";
            endpoint_block += "</div>";
            if (secret_configured) {
                endpoint_block += "<span class='field-hint'>Saving this form replaces the stored upload secret.</span></div>";
            }
            endpoint_block += "</div>";
            break;
        }

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
            endpoint_block += "<span class='field-hint'>Sends Influx line protocol with one line per sensor sample group.</span>";
            break;

        default:
            break;
    }

    if (card.backend_type == BackendType::kSensorCommunity ||
        card.backend_type == BackendType::kAir360Api) {
        project_links_block += renderMapLinksBlock(card);
    }

    std::string status_block;
    status_block.reserve(512U);
    if (!card.enabled) {
        status_block.clear();
    } else if (card.has_status) {
        status_block += "<hr/>";
        status_block += "<div class='backend-status'>";
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
        status_block += "</div>";
    } else {
        status_block = "<hr/>";
        status_block += "<div class='backend-status'><p>Status: <code>unavailable</code></p></div>";
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
            {"PROJECT_LINKS_BLOCK", project_links_block},
            {"STATUS_BLOCK", status_block},
        });
}

BackendsPageViewModel buildBackendsPageViewModel(
    const BackendConfigList& backend_config_list,
    const UploadManager& upload_manager,
    const BuildInfo& build_info,
    const std::string& air360_upload_secret_preview,
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

    std::string air360_map_url;
    std::string sensor_community_map_url;
    const BackendRecord* air360_record =
        findBackendRecordByType(backend_config_list, BackendType::kAir360Api);
    if (air360_record != nullptr &&
        (air360_record->latitude != 0.0F || air360_record->longitude != 0.0F)) {
        const std::string map_hash =
            buildMapHash(air360_record->latitude, air360_record->longitude);
        if (!map_hash.empty()) {
            air360_map_url = std::string(kAir360MapBaseUrl) + map_hash;
            sensor_community_map_url = std::string(kSensorCommunityMapBaseUrl) + map_hash;
        }
    }

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
        if (descriptor.type == BackendType::kSensorCommunity ||
            descriptor.type == BackendType::kAir360Api) {
            card.air360_map_url = air360_map_url;
            card.sensor_community_map_url = sensor_community_map_url;
        }
        if (descriptor.type == BackendType::kAir360Api) {
            card.air360_upload_secret_preview = air360_upload_secret_preview;
        }

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
                card.device_id_override = build_info.short_device_id;
            }
            if (record->backend_type == BackendType::kAir360Api) {
                if (record->latitude != 0.0F || record->longitude != 0.0F) {
                    card.latitude = formatCoordinate(record->latitude);
                    card.longitude = formatCoordinate(record->longitude);
                }
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
    // Runtime state chip
    const std::string& state = card.runtime_state_key;
    std::string runtime_state_chip = "<span class='chip ";
    if (state == "polling" || state == "initialized") {
        runtime_state_chip += "ok'><span class='dot'></span>";
    } else if (state == "error" || state == "failed" || state == "absent" || state == "unsupported") {
        runtime_state_chip += "err'><span class='dot'></span>";
    } else {
        runtime_state_chip += "'>";
    }
    runtime_state_chip += htmlEscape(state);
    runtime_state_chip += "</span>";

    // Status block (error + latest reading, shown above form when non-empty)
    std::string status_inner;
    status_inner.reserve(256U);
    if (!card.runtime_error.empty()) {
        status_inner += "<p>Error: <code>";
        status_inner += htmlEscape(card.runtime_error);
        status_inner += "</code></p>";
    }
    if (card.failures > 0U) {
        status_inner += "<p>Failures: <code>";
        status_inner += std::to_string(card.failures);
        status_inner += "</code>";
        if (card.next_retry_ms > 0U) {
            status_inner += " &middot; retry at <code>";
            status_inner += std::to_string(card.next_retry_ms);
            status_inner += " ms</code>";
        }
        status_inner += "</p>";
    }
    if (!card.latest_reading.empty()) {
        status_inner += "<p>Latest: <code>";
        status_inner += htmlEscape(card.latest_reading);
        status_inner += "</code></p>";
    }
    std::string status_block;
    if (!status_inner.empty()) {
        status_block = "<div class='backend-status' style='margin-bottom:12px'>" + status_inner + "</div>";
    }

    // Transport field blocks — <label class='field'> pattern for row grid layout
    std::string i2c_field_block;
    if (card.show_i2c_address_input) {
        i2c_field_block += "<label class='field' data-sensor-i2c-field>";
        i2c_field_block += "<span class='field-label'>I2C address</span>";
        i2c_field_block += "<select class='select' id='i2c_address_";
        i2c_field_block += std::to_string(card.id);
        i2c_field_block += "' name='i2c_address'>";
        i2c_field_block += card.i2c_address_options_html;
        i2c_field_block += "</select></label>";
    }

    std::string uart_field_block;
    if (card.show_uart_port_select) {
        uart_field_block += "<label class='field' data-sensor-uart-field>";
        uart_field_block += "<span class='field-label'>UART port</span>";
        uart_field_block += "<select class='select' id='uart_port_";
        uart_field_block += std::to_string(card.id);
        uart_field_block += "' name='uart_port_id' data-sensor-uart-port-select>";
        uart_field_block += card.uart_port_options_html;
        uart_field_block += "</select>";
        uart_field_block += "<span class='field-hint' data-sensor-uart-pins>";
        uart_field_block += htmlEscape(card.uart_pin_hint);
        uart_field_block += "</span></label>";
    }

    std::string gpio_field_block;
    if (card.show_gpio_pin_select) {
        gpio_field_block += "<label class='field' data-sensor-pin-field>";
        gpio_field_block += "<span class='field-label'>GPIO pin</span>";
        gpio_field_block += "<select class='select' id='analog_gpio_pin_";
        gpio_field_block += std::to_string(card.id);
        gpio_field_block += "' name='analog_gpio_pin'>";
        gpio_field_block += card.gpio_options_html;
        gpio_field_block += "</select></label>";
    }

    const bool has_transport =
        card.show_i2c_address_input || card.show_uart_port_select || card.show_gpio_pin_select;

    return renderTemplate(
        WebTemplateKey::kSensorCard,
        WebTemplateBindings{
            {"DISPLAY_NAME", htmlEscape(card.display_name.empty() ? "Sensor" : card.display_name)},
            {"RUNTIME_STATE_CHIP", runtime_state_chip},
            {"STATUS_BLOCK", status_block},
            {"SENSOR_ID", std::to_string(card.id)},
            {"SENSOR_TYPE_OPTIONS", card.sensor_type_options_html},
            {"DEFAULTS_HINT_TEXT", htmlEscape(card.defaults_hint)},
            {"DEFAULTS_HINT_HIDDEN", card.defaults_hint.empty() ? "hidden" : ""},
            {"FIELD_ROW_CLASS", has_transport ? "row-3" : "row-2"},
            {"I2C_FIELD_BLOCK", i2c_field_block},
            {"UART_FIELD_BLOCK", uart_field_block},
            {"GPIO_FIELD_BLOCK", gpio_field_block},
            {"POLL_INTERVAL_MS", std::to_string(card.poll_interval_ms)},
        });
}

// Builds the add-sensor form fields (shared by inline and sub-card forms).
// Returns the inner form HTML only (no <form> wrapper).
static std::string buildAddSensorFormFields(const SensorCategorySectionViewModel& section) {
    const std::string add_key = htmlEscape(section.key);
    std::string html;
    html.reserve(2048U);

    // Model + poll interval in a row-2
    html += "<div class='row-2'>";
    html += "<label class='field'><span class='field-label'>Model</span>";
    html += "<select class='select' id='sensor_type_add_";
    html += add_key;
    html += "' name='sensor_type' data-sensor-type-select>";
    html += section.add_sensor_type_options_html;
    html += "</select>";
    html += "<span class='field-hint' data-sensor-defaults";
    if (section.add_defaults_hint.empty()) { html += " hidden"; }
    html += ">";
    html += htmlEscape(section.add_defaults_hint);
    html += "</span></label>";
    html += "<label class='field'><span class='field-label'>Poll interval (ms)</span>";
    html += "<input class='input' id='poll_interval_ms_add_";
    html += add_key;
    html += "' name='poll_interval_ms' inputmode='numeric' min='30000' max='1800000' step='1000' value='";
    html += std::to_string(section.add_poll_interval_ms);
    html += "'></label></div>";

    // Transport fields — hidden/shown by JS based on selected model
    html += "<label class='field' data-sensor-i2c-field";
    if (!section.add_show_i2c_address_input) { html += " hidden"; }
    html += "><span class='field-label'>I2C address</span>";
    html += "<select class='select' id='i2c_address_add_";
    html += add_key;
    html += "' name='i2c_address'";
    if (!section.add_show_i2c_address_input) { html += " disabled"; }
    html += ">";
    html += section.add_i2c_address_options_html;
    html += "</select></label>";

    html += "<label class='field' data-sensor-uart-field";
    if (!section.add_show_uart_port_select) { html += " hidden"; }
    html += "><span class='field-label'>UART port</span>";
    html += "<select class='select' id='uart_port_add_";
    html += add_key;
    html += "' name='uart_port_id' data-sensor-uart-port-select";
    if (!section.add_show_uart_port_select) { html += " disabled"; }
    html += ">";
    html += section.add_uart_port_options_html;
    html += "</select><span class='field-hint' data-sensor-uart-pins>";
    html += htmlEscape(section.add_uart_pin_hint);
    html += "</span></label>";

    html += "<label class='field' data-sensor-pin-field";
    if (!section.add_show_gpio_pin_select) { html += " hidden"; }
    html += "><span class='field-label'>GPIO pin</span>";
    html += "<select class='select' id='analog_gpio_pin_add_";
    html += add_key;
    html += "' name='analog_gpio_pin'";
    if (!section.add_show_gpio_pin_select) { html += " disabled"; }
    html += ">";
    html += section.add_gpio_options_html;
    html += "</select></label>";

    return html;
}

std::string renderSensorCategorySection(const SensorCategorySectionViewModel& section) {
    const std::string add_key = htmlEscape(section.key);
    const bool has_sensors = !section.cards.empty();

    // Card-right status chips
    std::string right_chips;
    if (section.allow_multiple) {
        right_chips += "<span class='chip accent'>multiple</span>";
    }
    if (has_sensors) {
        right_chips += "<span class='chip accent'>configured</span>";
    } else {
        right_chips += "<span class='chip'>not configured</span>";
    }

    // Sensor sub-cards
    std::string cards_html;
    cards_html.reserve(section.cards.size() * 2200U);
    for (const auto& card : section.cards) {
        cards_html += renderSensorCard(card);
    }

    // Add-sensor form (inline when unconfigured, sub-card when multiple+configured)
    std::string add_form_html;
    if (section.show_add_form) {
        const std::string form_fields = buildAddSensorFormFields(section);
        const std::string form_id = "sensor-add-" + add_key;
        const std::string add_label = htmlEscape(section.add_button_label);

        if (has_sensors) {
            // Sub-card style: sunken card below existing sensors
            add_form_html += "<div class='card' style='background:var(--bg-sunken);border-color:var(--line-3)'>";
            add_form_html += "<div class='card-header' style='border-bottom-color:var(--line-3)'>";
            add_form_html += "<div style='font-family:var(--font-display);font-weight:700;font-size:14px'>";
            add_form_html += section.allow_multiple ? "Add sensor" : "Add sensor";
            add_form_html += "</div>";
            add_form_html += "<div class='card-right'>";
            add_form_html += "<button class='btn sm primary' type='submit' form='";
            add_form_html += form_id;
            add_form_html += "'>";
            add_form_html += add_label;
            add_form_html += "</button></div></div>";
            add_form_html += "<div class='card-body'>";
            add_form_html += "<form id='";
            add_form_html += form_id;
            add_form_html += "' method='POST' action='/sensors' data-dirty-track='sensor-add-";
            add_form_html += add_key;
            add_form_html += "' data-sensor-form><input type='hidden' name='action' value='add'>";
            add_form_html += "<div class='stack-14'>";
            add_form_html += form_fields;
            add_form_html += "</div></form></div></div>";
        } else {
            // Inline style: fields directly in card body
            add_form_html += "<form id='";
            add_form_html += form_id;
            add_form_html += "' method='POST' action='/sensors' data-dirty-track='sensor-add-";
            add_form_html += add_key;
            add_form_html += "' data-sensor-form><input type='hidden' name='action' value='add'>";
            add_form_html += "<div class='stack-14'>";
            add_form_html += form_fields;
            add_form_html += "</div></form>";
            add_form_html += "<div class='row-end' style='margin-top:14px'>";
            add_form_html += "<button class='btn sm primary' type='submit' form='";
            add_form_html += form_id;
            add_form_html += "'>";
            add_form_html += add_label;
            add_form_html += "</button></div>";
        }
    }

    // Assemble section
    std::string section_html;
    section_html.reserve(cards_html.size() + add_form_html.size() + 1024U);
    section_html += "<section class='card'><div class='card-header'><div><h3 class='card-title'>";
    section_html += htmlEscape(section.title);
    section_html += "</h3><div class='card-sub'>";
    section_html += htmlEscape(section.description);
    section_html += "</div></div><div class='card-right'>";
    section_html += right_chips;
    section_html += "</div></div><div class='card-body'>";
    if (!section.notice_html.empty()) {
        section_html += section.notice_html;
    }
    if (has_sensors) {
        section_html += "<div class='stack-14'>";
        section_html += cards_html;
        section_html += add_form_html;
        section_html += "</div>";
    } else {
        section_html += add_form_html;
    }
    section_html += "</div></section>";
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
        section.add_button_label = "Stage sensor";

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
            appendI2cAddressOptions(
                section.add_i2c_address_options_html,
                *descriptor,
                descriptor->default_i2c_address);
            section.add_show_uart_port_select = descriptor->supports_uart;
            appendUartPortOptions(
                section.add_uart_port_options_html,
                *descriptor,
                descriptor->default_uart_port_id);
            section.add_uart_pin_hint = uartPinHint(descriptor->default_uart_port_id);
            section.add_show_gpio_pin_select =
                descriptor->supports_gpio || descriptor->supports_analog;
            appendGpioPinOptions(
                section.add_gpio_options_html,
                *descriptor,
                firstAllowedGpioPin(*descriptor));
        }
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
            card.show_uart_port_select = descriptor->supports_uart;
        }
        if (card.show_i2c_address_input) {
            card.i2c_address_value = formatI2cAddress(record.i2c_address);
            if (descriptor != nullptr) {
                appendI2cAddressOptions(
                    card.i2c_address_options_html,
                    *descriptor,
                    record.i2c_address);
            }
        }
        if (card.show_uart_port_select && descriptor != nullptr) {
            appendUartPortOptions(
                card.uart_port_options_html,
                *descriptor,
                record.uart_port_id);
            card.uart_pin_hint = uartPinHint(record.uart_port_id);
        }
        card.show_gpio_pin_select =
            record.transport_kind == TransportKind::kGpio ||
            record.transport_kind == TransportKind::kAnalog;
        if (card.show_gpio_pin_select && descriptor != nullptr) {
            appendGpioPinOptions(card.gpio_options_html, *descriptor, record.analog_gpio_pin);
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
    const std::string& air360_upload_secret_preview,
    const std::string& notice,
    bool error_notice) {
    const BackendsPageViewModel model =
        buildBackendsPageViewModel(
            backend_config_list,
            upload_manager,
            build_info,
            air360_upload_secret_preview,
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
            {"UPLOAD_INTERVAL_MIN", std::to_string(kMinUploadIntervalMs)},
            {"UPLOAD_INTERVAL_MAX", std::to_string(kMaxUploadIntervalMs)},
            {"BACKEND_CARDS", backend_cards},
        });
    return renderPageDocument(
        WebPageKey::kBackends,
        "Air360 Upload Backends",
        "Upload Backends",
        "<div class='actions'>"
        "<button class='btn sm primary' type='submit' form='backends-form'>"
        "Apply</button></div>",
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
            {"NOTICE", model.notice_html},
            {"CATEGORY_SECTIONS", category_sections_html},
        });
    return renderPageDocument(
        WebPageKey::kSensors,
        "Air360 Sensor Configuration",
        "Sensor Configuration",
        "<div class='actions'>"
        "<button class='btn sm' type='submit' form='sensor-discard-form'>Discard</button>"
        "<button class='btn sm primary' type='submit' form='sensor-apply-form'>Apply</button>"
        "</div>",
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
    const std::string& air360_upload_secret_preview,
    const std::string& notice,
    bool error_notice) {
    return ::air360::renderBackendsPage(
        backend_config_list,
        upload_manager,
        build_info,
        air360_upload_secret_preview,
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
    Air360ApiCredentialRepository& air360_api_credentials,
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
    air360_api_credentials_ = &air360_api_credentials;
    backend_config_list_ = &backend_config_list;
    upload_manager_ = &upload_manager;
    cellular_config_repository_ = &cellular_config_repository;
    cellular_config_ = &cellular_config;
    staged_sensor_config_ = sensor_config_list;
    has_pending_sensor_changes_ = false;

    httpd_config_t config_httpd = HTTPD_DEFAULT_CONFIG();
    config_httpd.server_port = port;
    // Single httpd task; all concurrent connections share this one stack.
    // logHttpHandlerWatermark() logs if usage crosses 50/70/90 %.
    config_httpd.stack_size = web::kHttpdStackBytes;
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

    httpd_uri_t favicon_uri{};
    favicon_uri.uri = "/favicon.ico";
    favicon_uri.method = HTTP_GET;
    favicon_uri.handler = &WebServer::handleFavicon;
    favicon_uri.user_ctx = this;
    err = httpd_register_uri_handler(handle_, &favicon_uri);
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

    httpd_uri_t air360_secret_uri{};
    air360_secret_uri.uri = "/backends/air360-upload-secret";
    air360_secret_uri.method = HTTP_GET;
    air360_secret_uri.handler = &WebServer::handleAir360UploadSecret;
    air360_secret_uri.user_ctx = this;
    err = httpd_register_uri_handler(handle_, &air360_secret_uri);
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
