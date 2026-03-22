#include "air360/network_manager.hpp"

#include <cstdio>
#include <cstring>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_wifi.h"
#include "lwip/ip4_addr.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.net";

}  // namespace

esp_err_t NetworkManager::startLabAp(const DeviceConfig& config) {
    esp_netif_t* ap_netif = esp_netif_create_default_wifi_ap();
    if (ap_netif == nullptr) {
        return ESP_FAIL;
    }

    const wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&wifi_init);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_netif_dhcps_stop(ap_netif);
    if (err != ESP_OK) {
        return err;
    }

    esp_netif_ip_info_t ip_info{};
    IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
    err = esp_netif_set_ip_info(ap_netif, &ip_info);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_netif_dhcps_start(ap_netif);
    if (err != ESP_OK) {
        return err;
    }

    wifi_config_t wifi_config{};
    std::strncpy(
        reinterpret_cast<char*>(wifi_config.ap.ssid),
        config.lab_ap_ssid,
        sizeof(wifi_config.ap.ssid));
    std::strncpy(
        reinterpret_cast<char*>(wifi_config.ap.password),
        config.lab_ap_password,
        sizeof(wifi_config.ap.password));
    wifi_config.ap.ssid_len = std::strlen(config.lab_ap_ssid);
    wifi_config.ap.channel = CONFIG_AIR360_LAB_AP_CHANNEL;
    wifi_config.ap.max_connection = CONFIG_AIR360_LAB_AP_MAX_CONNECTIONS;
    wifi_config.ap.authmode =
        std::strlen(config.lab_ap_password) == 0U ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    wifi_config.ap.pmf_cfg.required = false;

    err = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_start();
    if (err != ESP_OK) {
        return err;
    }

    char ip_buffer[16];
    std::snprintf(ip_buffer, sizeof(ip_buffer), IPSTR, IP2STR(&ip_info.ip));
    state_.lab_ap_active = true;
    state_.ip_address = ip_buffer;

    ESP_LOGI(
        kTag,
        "Lab AP active: ssid=%s ip=%s",
        config.lab_ap_ssid,
        state_.ip_address.c_str());

    return ESP_OK;
}

const NetworkState& NetworkManager::state() const {
    return state_;
}

}  // namespace air360
