#include "air360/config_transaction.hpp"

#include "nvs.h"

namespace air360 {
namespace {

constexpr char kNamespace[] = "air360";
constexpr char kDeviceConfigKey[] = "device_cfg";
constexpr char kCellularConfigKey[] = "cellular_cfg";

}  // namespace

esp_err_t saveDeviceAndCellularConfig(
    const ConfigRepository& config_repository,
    const CellularConfigRepository& cellular_config_repository,
    const DeviceConfig& device_config,
    const CellularConfig& cellular_config) {
    if (!config_repository.isValid(device_config) ||
        !cellular_config_repository.isValid(cellular_config)) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_blob(handle, kDeviceConfigKey, &device_config, sizeof(device_config));
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, kCellularConfigKey, &cellular_config, sizeof(cellular_config));
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}

}  // namespace air360
