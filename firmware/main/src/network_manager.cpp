#include "air360/network_manager.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>

#include "air360/time_utils.hpp"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_netif_sntp.h"
#include "esp_task_wdt.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "lwip/ip4_addr.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.net";
constexpr EventBits_t kStationConnectedBit = BIT0;
constexpr EventBits_t kStationFailedBit = BIT1;
constexpr char kDefaultSntpServer[] = "pool.ntp.org";
constexpr std::uint32_t kSntpPollIntervalMs = 250U;

struct RuntimeContext {
    EventGroupHandle_t station_events = nullptr;
    esp_netif_t* ap_netif = nullptr;
    esp_netif_t* sta_netif = nullptr;
    bool wifi_initialized = false;
    bool sntp_initialized = false;
};

RuntimeContext& runtimeContext() {
    static RuntimeContext context;
    return context;
}

void copyString(char* destination, std::size_t destination_size, const char* source) {
    if (destination_size == 0U) {
        return;
    }

    std::strncpy(destination, source, destination_size - 1U);
    destination[destination_size - 1U] = '\0';
}

bool hasStationConfig(const DeviceConfig& config) {
    return config.wifi_sta_ssid[0] != '\0';
}

std::string stationHostname(const DeviceConfig& config) {
    std::string hostname;
    hostname.reserve(sizeof(config.device_name));

    for (std::size_t index = 0; index < sizeof(config.device_name); ++index) {
        const unsigned char raw = static_cast<unsigned char>(config.device_name[index]);
        if (raw == '\0') {
            break;
        }

        if (std::isalnum(raw)) {
            hostname.push_back(static_cast<char>(std::tolower(raw)));
            continue;
        }

        if (raw == '-' || raw == '_') {
            hostname.push_back(static_cast<char>(raw));
            continue;
        }

        if (!hostname.empty() && hostname.back() != '-') {
            hostname.push_back('-');
        }
    }

    while (!hostname.empty() && hostname.back() == '-') {
        hostname.pop_back();
    }

    if (hostname.empty()) {
        hostname = "air360";
    }

    return hostname;
}

void setStateError(NetworkState& state, const char* error) {
    state.last_error = error == nullptr ? "" : error;
}

}  // namespace

void NetworkManager::resetState() {
    state_ = {};
}

esp_err_t NetworkManager::ensureWifiInit() {
    RuntimeContext& context = runtimeContext();
    if (context.station_events == nullptr) {
        context.station_events = xEventGroupCreate();
        if (context.station_events == nullptr) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (context.wifi_initialized) {
        return ESP_OK;
    }

    const wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&wifi_init);
    if (err == ESP_OK) {
        context.wifi_initialized = true;
    } else if (err == ESP_ERR_INVALID_STATE) {
        context.wifi_initialized = true;
        err = ESP_OK;
    }

    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        return err;
    }

    return ESP_OK;
}

esp_err_t NetworkManager::synchronizeTime(std::uint32_t timeout_ms) {
    RuntimeContext& context = runtimeContext();

    state_.time_sync_attempted = true;
    state_.time_synchronized = false;
    state_.time_sync_error.clear();
    state_.last_time_sync_unix_ms = 0;

    if (!state_.station_connected) {
        state_.time_sync_error = "station is not connected";
        return ESP_ERR_INVALID_STATE;
    }

    if (hasValidUnixTime()) {
        state_.time_synchronized = true;
        state_.last_time_sync_unix_ms = air360::currentUnixMilliseconds();
        ESP_LOGI(kTag, "System time already valid, skipping SNTP wait");
        return ESP_OK;
    }

    esp_err_t err = ESP_OK;
    if (!context.sntp_initialized) {
        esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG(kDefaultSntpServer);
        err = esp_netif_sntp_init(&sntp_config);
        if (err == ESP_OK) {
            context.sntp_initialized = true;
        } else {
            state_.time_sync_error = esp_err_to_name(err);
            ESP_LOGW(kTag, "SNTP init failed: %s", esp_err_to_name(err));
            return err;
        }
    } else {
        err = esp_netif_sntp_start();
        if (err != ESP_OK) {
            state_.time_sync_error = esp_err_to_name(err);
            ESP_LOGW(kTag, "SNTP start failed: %s", esp_err_to_name(err));
            return err;
        }
    }

    ESP_LOGI(kTag, "Waiting for SNTP time sync");
    const std::int64_t started_ms = uptimeMilliseconds();
    bool synchronized = false;
    while ((uptimeMilliseconds() - started_ms) < timeout_ms) {
        if (hasValidUnixTime()) {
            synchronized = true;
            break;
        }
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(kSntpPollIntervalMs));
    }

    if (!synchronized || !hasValidUnixTime()) {
        state_.time_sync_error = "time is still invalid after SNTP sync";
        ESP_LOGW(kTag, "SNTP sync failed: %s", state_.time_sync_error.c_str());
        return ESP_ERR_TIMEOUT;
    }

    state_.time_synchronized = true;
    state_.last_time_sync_unix_ms = air360::currentUnixMilliseconds();
    ESP_LOGI(kTag, "SNTP time synchronized");
    return ESP_OK;
}

void NetworkManager::handleWifiEvent(
    void* arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void* event_data) {
    if (event_base != WIFI_EVENT) {
        return;
    }

    auto* manager = static_cast<NetworkManager*>(arg);
    RuntimeContext& context = runtimeContext();
    if (manager == nullptr || context.station_events == nullptr) {
        return;
    }

    switch (event_id) {
        case WIFI_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_DISCONNECTED: {
            manager->state_.station_connected = false;
            manager->state_.mode = NetworkMode::kOffline;
            manager->state_.ip_address.clear();

            const auto* disconnected =
                static_cast<const wifi_event_sta_disconnected_t*>(event_data);
            char reason_buffer[64];
            std::snprintf(
                reason_buffer,
                sizeof(reason_buffer),
                "station disconnected (reason=%d)",
                disconnected != nullptr ? static_cast<int>(disconnected->reason) : -1);
            setStateError(manager->state_, reason_buffer);
            xEventGroupSetBits(context.station_events, kStationFailedBit);
            break;
        }
        default:
            break;
    }
}

void NetworkManager::handleIpEvent(
    void* arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void* event_data) {
    if (event_base != IP_EVENT || event_id != IP_EVENT_STA_GOT_IP) {
        return;
    }

    auto* manager = static_cast<NetworkManager*>(arg);
    RuntimeContext& context = runtimeContext();
    if (manager == nullptr || context.station_events == nullptr) {
        return;
    }

    const auto* got_ip = static_cast<const ip_event_got_ip_t*>(event_data);
    if (got_ip != nullptr) {
        char ip_buffer[16];
        std::snprintf(ip_buffer, sizeof(ip_buffer), IPSTR, IP2STR(&got_ip->ip_info.ip));
        manager->state_.ip_address = ip_buffer;
    }

    manager->state_.mode = NetworkMode::kStation;
    manager->state_.station_connected = true;
    manager->state_.lab_ap_active = false;
    manager->state_.last_error.clear();
    xEventGroupSetBits(context.station_events, kStationConnectedBit);
}

esp_err_t NetworkManager::connectStation(const DeviceConfig& config, std::uint32_t timeout_ms) {
    resetState();
    state_.station_config_present = hasStationConfig(config);
    state_.station_connect_attempted = true;
    state_.station_ssid = config.wifi_sta_ssid;
    state_.lab_ap_ssid = config.lab_ap_ssid;

    if (!state_.station_config_present) {
        setStateError(state_, "missing station configuration");
        return ESP_ERR_INVALID_ARG;
    }

    RuntimeContext& context = runtimeContext();
    esp_err_t err = ensureWifiInit();
    if (err != ESP_OK) {
        setStateError(state_, esp_err_to_name(err));
        return err;
    }

    if (context.sta_netif == nullptr) {
        context.sta_netif = esp_netif_create_default_wifi_sta();
        if (context.sta_netif == nullptr) {
            setStateError(state_, "failed to create station netif");
            return ESP_FAIL;
        }
    }

    const std::string hostname = stationHostname(config);
    err = esp_netif_set_hostname(context.sta_netif, hostname.c_str());
    if (err != ESP_OK) {
        setStateError(state_, esp_err_to_name(err));
        return err;
    }

    xEventGroupClearBits(context.station_events, kStationConnectedBit | kStationFailedBit);

    esp_event_handler_instance_t wifi_handler = nullptr;
    esp_event_handler_instance_t ip_handler = nullptr;
    err = esp_event_handler_instance_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &NetworkManager::handleWifiEvent,
        this,
        &wifi_handler);
    if (err != ESP_OK) {
        setStateError(state_, esp_err_to_name(err));
        return err;
    }

    err = esp_event_handler_instance_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &NetworkManager::handleIpEvent,
        this,
        &ip_handler);
    if (err != ESP_OK) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_handler);
        setStateError(state_, esp_err_to_name(err));
        return err;
    }

    wifi_config_t wifi_config{};
    copyString(
        reinterpret_cast<char*>(wifi_config.sta.ssid),
        sizeof(wifi_config.sta.ssid),
        config.wifi_sta_ssid);
    copyString(
        reinterpret_cast<char*>(wifi_config.sta.password),
        sizeof(wifi_config.sta.password),
        config.wifi_sta_password);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT && err != ESP_ERR_WIFI_NOT_STARTED) {
        ESP_LOGW(kTag, "esp_wifi_stop before STA start failed: %s", esp_err_to_name(err));
    }

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) {
        err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    }
    if (err == ESP_OK) {
        err = esp_wifi_start();
    }
    if (err == ESP_OK) {
        err = esp_wifi_set_ps(WIFI_PS_NONE);
    }

    if (err != ESP_OK) {
        setStateError(state_, esp_err_to_name(err));
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_handler);
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_handler);
        return err;
    }

    ESP_LOGI(kTag, "Station Wi-Fi power save disabled for lower upload latency");
    ESP_LOGI(kTag, "Station hostname: %s", hostname.c_str());
    ESP_LOGI(kTag, "Attempting station join: ssid=%s", config.wifi_sta_ssid);

    const EventBits_t bits = xEventGroupWaitBits(
        context.station_events,
        kStationConnectedBit | kStationFailedBit,
        pdTRUE,
        pdFALSE,
        pdMS_TO_TICKS(timeout_ms));

    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_handler);
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_handler);

    if ((bits & kStationConnectedBit) != 0U) {
        const esp_err_t time_err = synchronizeTime();
        if (time_err != ESP_OK) {
            ESP_LOGW(kTag, "Time sync failed after station join: %s", esp_err_to_name(time_err));
        }
        return ESP_OK;
    }

    if ((bits & kStationFailedBit) == 0U) {
        setStateError(state_, "station connect timeout");
        err = ESP_ERR_TIMEOUT;
    } else {
        err = ESP_FAIL;
    }

    err = esp_wifi_disconnect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT) {
        ESP_LOGW(kTag, "esp_wifi_disconnect failed: %s", esp_err_to_name(err));
    }
    err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT && err != ESP_ERR_WIFI_NOT_STARTED) {
        ESP_LOGW(kTag, "esp_wifi_stop after STA failure failed: %s", esp_err_to_name(err));
    }

    return (bits & kStationFailedBit) == 0U ? ESP_ERR_TIMEOUT : ESP_FAIL;
}

esp_err_t NetworkManager::startLabAp(const DeviceConfig& config) {
    RuntimeContext& context = runtimeContext();
    esp_err_t err = ensureWifiInit();
    if (err != ESP_OK) {
        setStateError(state_, esp_err_to_name(err));
        return err;
    }

    if (context.ap_netif == nullptr) {
        context.ap_netif = esp_netif_create_default_wifi_ap();
        if (context.ap_netif == nullptr) {
            setStateError(state_, "failed to create AP netif");
            return ESP_FAIL;
        }
    }

    err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT && err != ESP_ERR_WIFI_NOT_STARTED) {
        ESP_LOGW(kTag, "esp_wifi_stop before AP start failed: %s", esp_err_to_name(err));
    }

    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK) {
        setStateError(state_, esp_err_to_name(err));
        return err;
    }

    state_.station_config_present = hasStationConfig(config);
    state_.station_ssid = config.wifi_sta_ssid;
    state_.lab_ap_ssid = config.lab_ap_ssid;

    err = esp_netif_dhcps_stop(context.ap_netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        setStateError(state_, esp_err_to_name(err));
        return err;
    }

    esp_netif_ip_info_t ip_info{};
    IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
    err = esp_netif_set_ip_info(context.ap_netif, &ip_info);
    if (err != ESP_OK) {
        setStateError(state_, esp_err_to_name(err));
        return err;
    }

    err = esp_netif_dhcps_start(context.ap_netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
        setStateError(state_, esp_err_to_name(err));
        return err;
    }

    wifi_config_t wifi_config{};
    copyString(
        reinterpret_cast<char*>(wifi_config.ap.ssid),
        sizeof(wifi_config.ap.ssid),
        config.lab_ap_ssid);
    copyString(
        reinterpret_cast<char*>(wifi_config.ap.password),
        sizeof(wifi_config.ap.password),
        config.lab_ap_password);
    wifi_config.ap.ssid_len = std::strlen(config.lab_ap_ssid);
    wifi_config.ap.channel = CONFIG_AIR360_LAB_AP_CHANNEL;
    wifi_config.ap.max_connection = CONFIG_AIR360_LAB_AP_MAX_CONNECTIONS;
    wifi_config.ap.authmode =
        std::strlen(config.lab_ap_password) == 0U ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    wifi_config.ap.pmf_cfg.required = false;

    err = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    if (err != ESP_OK) {
        setStateError(state_, esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_start();
    if (err != ESP_OK) {
        setStateError(state_, esp_err_to_name(err));
        return err;
    }

    char ip_buffer[16];
    std::snprintf(ip_buffer, sizeof(ip_buffer), IPSTR, IP2STR(&ip_info.ip));
    state_.mode = NetworkMode::kSetupAp;
    state_.lab_ap_active = true;
    state_.ip_address = ip_buffer;

    ESP_LOGI(
        kTag,
        "Setup AP active: ssid=%s ip=%s",
        config.lab_ap_ssid,
        state_.ip_address.c_str());

    return ESP_OK;
}

const NetworkState& NetworkManager::state() const {
    return state_;
}

bool NetworkManager::hasValidTime() const {
    return hasValidUnixTime();
}

std::int64_t NetworkManager::currentUnixMilliseconds() const {
    return air360::currentUnixMilliseconds();
}

}  // namespace air360
