#include "air360/cellular_config_repository.hpp"

#include "air360/string_utils.hpp"
#include "esp_log.h"
#include "nvs.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.cellular.cfg";
constexpr char kNamespace[] = "air360";
constexpr char kConfigKey[] = "cellular_cfg";

[[nodiscard]] esp_err_t saveInternal(nvs_handle_t handle, const CellularConfig& config) {
    esp_err_t err = nvs_set_blob(handle, kConfigKey, &config, sizeof(config));
    if (err != ESP_OK) {
        return err;
    }

    return nvs_commit(handle);
}

}  // namespace

CellularConfig makeDefaultCellularConfig() {
    CellularConfig config{};
    config.magic = kCellularConfigMagic;
    config.schema_version = kCellularConfigSchemaVersion;
    config.record_size = static_cast<std::uint16_t>(sizeof(CellularConfig));
    config.enabled = 0U;
    config.uart_port = static_cast<std::uint8_t>(CONFIG_AIR360_CELLULAR_DEFAULT_UART);
    config.uart_rx_gpio = static_cast<std::uint8_t>(CONFIG_AIR360_CELLULAR_DEFAULT_RX_GPIO);
    config.uart_tx_gpio = static_cast<std::uint8_t>(CONFIG_AIR360_CELLULAR_DEFAULT_TX_GPIO);
    config.uart_baud = 115200U;
    config.pwrkey_gpio = static_cast<std::uint8_t>(CONFIG_AIR360_CELLULAR_DEFAULT_PWRKEY_GPIO);
    config.sleep_gpio = static_cast<std::uint8_t>(CONFIG_AIR360_CELLULAR_DEFAULT_SLEEP_GPIO);
    config.reset_gpio = 0xFFU;
    config.reserved0 = 0U;
    config.wifi_debug_window_s =
        static_cast<std::uint16_t>(CONFIG_AIR360_CELLULAR_WIFI_DEBUG_WINDOW_S);
    config.reserved1 = 0U;
    copyString(
        config.connectivity_check_host,
        sizeof(config.connectivity_check_host),
        "8.8.8.8");
    return config;
}

bool CellularConfigRepository::isValid(const CellularConfig& config) const {
    if (config.magic != kCellularConfigMagic ||
        config.schema_version != kCellularConfigSchemaVersion ||
        config.record_size != static_cast<std::uint16_t>(sizeof(CellularConfig)) ||
        config.uart_baud == 0U) {
        return false;
    }

    if (config.apn[sizeof(config.apn) - 1U] != '\0' ||
        config.username[sizeof(config.username) - 1U] != '\0' ||
        config.password[sizeof(config.password) - 1U] != '\0' ||
        config.sim_pin[sizeof(config.sim_pin) - 1U] != '\0' ||
        config.connectivity_check_host[sizeof(config.connectivity_check_host) - 1U] != '\0') {
        return false;
    }

    return true;
}

esp_err_t CellularConfigRepository::loadOrCreate(
    CellularConfig& out_config,
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
        ESP_LOGI(kTag, "No stored cellular config found, writing defaults");
        out_config = makeDefaultCellularConfig();
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

    if (blob_size != sizeof(CellularConfig)) {
        ESP_LOGW(kTag, "Stored cellular config size mismatch, replacing with defaults");
        out_config = makeDefaultCellularConfig();
        err = saveInternal(handle, out_config);
        if (err == ESP_OK) {
            wrote_defaults = true;
        }
        nvs_close(handle);
        return err;
    }

    CellularConfig loaded{};
    err = nvs_get_blob(handle, kConfigKey, &loaded, &blob_size);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    if (blob_size != sizeof(CellularConfig) || !isValid(loaded)) {
        ESP_LOGW(kTag, "Stored cellular config invalid, replacing with defaults");
        out_config = makeDefaultCellularConfig();
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

esp_err_t CellularConfigRepository::save(const CellularConfig& config) {
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

}  // namespace air360
