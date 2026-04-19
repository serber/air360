#include "air360/config_repository.hpp"

#include <cstddef>
#include <cstring>

#include "esp_log.h"
#include "nvs.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.config";
constexpr char kNamespace[] = "air360";
constexpr char kConfigKey[] = "device_cfg";
constexpr char kBootCountKey[] = "boot_count";

void copyString(char* destination, std::size_t destination_size, const char* source) {
    if (destination_size == 0U) {
        return;
    }

    std::strncpy(destination, source, destination_size - 1U);
    destination[destination_size - 1U] = '\0';
}

esp_err_t saveInternal(nvs_handle_t handle, const DeviceConfig& config) {
    esp_err_t err = nvs_set_blob(handle, kConfigKey, &config, sizeof(config));
    if (err != ESP_OK) {
        return err;
    }

    return nvs_commit(handle);
}

}  // namespace

DeviceConfig makeDefaultDeviceConfig() {
    DeviceConfig config{};
    config.magic = kDeviceConfigMagic;
    config.schema_version = kDeviceConfigSchemaVersion;
    config.record_size = static_cast<std::uint16_t>(sizeof(DeviceConfig));
    config.http_port = CONFIG_AIR360_HTTP_PORT;
#ifdef CONFIG_AIR360_ENABLE_LAB_AP
    config.lab_ap_enabled = 1U;
#else
    config.lab_ap_enabled = 0U;
#endif
    config.local_auth_enabled = 0U;
    config.ble_advertise_enabled = 0U;
    config.ble_adv_interval_index = kBleAdvIntervalDefaultIndex;
    config.reserved1[0] = 0U;
    config.reserved1[1] = 0U;
    copyString(config.device_name, sizeof(config.device_name), CONFIG_AIR360_DEVICE_NAME);
#ifdef CONFIG_AIR360_LAB_AP_SSID
    copyString(config.lab_ap_ssid, sizeof(config.lab_ap_ssid), CONFIG_AIR360_LAB_AP_SSID);
#endif
#ifdef CONFIG_AIR360_LAB_AP_PASSWORD
    copyString(
        config.lab_ap_password,
        sizeof(config.lab_ap_password),
        CONFIG_AIR360_LAB_AP_PASSWORD);
#endif
    config.sntp_server[0] = '\0';
    return config;
}

bool ConfigRepository::isValid(const DeviceConfig& config) const {
    if (config.magic != kDeviceConfigMagic ||
        config.schema_version != kDeviceConfigSchemaVersion ||
        config.record_size != static_cast<std::uint16_t>(sizeof(DeviceConfig)) ||
        config.http_port == 0U) {
        return false;
    }

    if (config.device_name[0] == '\0' ||
        config.device_name[sizeof(config.device_name) - 1U] != '\0' ||
        config.wifi_sta_ssid[sizeof(config.wifi_sta_ssid) - 1U] != '\0' ||
        config.wifi_sta_password[sizeof(config.wifi_sta_password) - 1U] != '\0' ||
        config.lab_ap_ssid[sizeof(config.lab_ap_ssid) - 1U] != '\0' ||
        config.lab_ap_password[sizeof(config.lab_ap_password) - 1U] != '\0' ||
        config.sntp_server[sizeof(config.sntp_server) - 1U] != '\0' ||
        config.sta_ip[sizeof(config.sta_ip) - 1U] != '\0' ||
        config.sta_netmask[sizeof(config.sta_netmask) - 1U] != '\0' ||
        config.sta_gateway[sizeof(config.sta_gateway) - 1U] != '\0' ||
        config.sta_dns[sizeof(config.sta_dns) - 1U] != '\0') {
        return false;
    }

    const std::size_t wifi_ssid_length = std::strlen(config.wifi_sta_ssid);
    const std::size_t wifi_password_length = std::strlen(config.wifi_sta_password);
    const std::size_t ssid_length = std::strlen(config.lab_ap_ssid);
    const std::size_t password_length = std::strlen(config.lab_ap_password);
    if (wifi_ssid_length > 32U) {
        return false;
    }

    if (wifi_password_length > 63U) {
        return false;
    }

    if (ssid_length == 0U || ssid_length > 32U) {
        return false;
    }

    if (password_length != 0U && (password_length < 8U || password_length > 63U)) {
        return false;
    }

    return true;
}

esp_err_t ConfigRepository::loadOrCreate(
    DeviceConfig& out_config,
    bool& loaded_from_storage,
    bool& wrote_defaults) {
    loaded_from_storage = false;
    wrote_defaults = false;

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    std::size_t blob_size = 0;
    err = nvs_get_blob(handle, kConfigKey, nullptr, &blob_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(kTag, "No stored config found, writing defaults");
        out_config = makeDefaultDeviceConfig();
        err = saveInternal(handle, out_config);
        if (err == ESP_OK) {
            wrote_defaults = true;
        }
        nvs_close(handle);
        return err;
    }

    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    if (blob_size != sizeof(DeviceConfig)) {
        ESP_LOGW(kTag, "Stored config size mismatch, replacing with defaults");
        out_config = makeDefaultDeviceConfig();
        err = saveInternal(handle, out_config);
        if (err == ESP_OK) {
            wrote_defaults = true;
        }
        nvs_close(handle);
        return err;
    }

    DeviceConfig loaded{};
    err = nvs_get_blob(handle, kConfigKey, &loaded, &blob_size);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    if (blob_size != sizeof(DeviceConfig) || !isValid(loaded)) {
        ESP_LOGW(kTag, "Stored config invalid, replacing with defaults");
        out_config = makeDefaultDeviceConfig();
        err = saveInternal(handle, out_config);
        if (err == ESP_OK) {
            wrote_defaults = true;
        }
        nvs_close(handle);
        return err;
    }

    out_config = loaded;
    loaded_from_storage = true;
    nvs_close(handle);
    return ESP_OK;
}

esp_err_t ConfigRepository::save(const DeviceConfig& config) {
    if (!isValid(config)) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = saveInternal(handle, config);
    nvs_close(handle);
    return err;
}

esp_err_t ConfigRepository::incrementBootCount(std::uint32_t& out_boot_count) {
    out_boot_count = 0;

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_u32(handle, kBootCountKey, &out_boot_count);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return err;
    }

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        out_boot_count = 0;
    }

    ++out_boot_count;
    err = nvs_set_u32(handle, kBootCountKey, out_boot_count);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}

}  // namespace air360
