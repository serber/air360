#include "air360/config_transaction.hpp"

#include "nvs.h"

namespace air360 {
namespace {

constexpr char kNamespace[] = "air360";
constexpr char kPairedConfigKey[] = "network_cfg";
constexpr std::uint32_t kPairedConfigMagic = 0x4E434647U;
constexpr std::uint16_t kPairedConfigVersion = 1U;

struct PairedConfig {
    std::uint32_t magic = kPairedConfigMagic;
    std::uint16_t version = kPairedConfigVersion;
    std::uint16_t record_size = sizeof(PairedConfig);
    DeviceConfig device{};
    CellularConfig cellular{};
};

}  // namespace

esp_err_t loadDeviceAndCellularConfig(
    DeviceConfig& device_config,
    CellularConfig& cellular_config) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    PairedConfig paired{};
    std::size_t size = sizeof(paired);
    err = nvs_get_blob(handle, kPairedConfigKey, &paired, &size);
    nvs_close(handle);
    if (err != ESP_OK) {
        return err;
    }
    if (size != sizeof(paired) || paired.magic != kPairedConfigMagic ||
        paired.version != kPairedConfigVersion || paired.record_size != sizeof(paired) ||
        !ConfigRepository{}.isValid(paired.device) ||
        !CellularConfigRepository{}.isValid(paired.cellular)) {
        return ESP_ERR_INVALID_STATE;
    }
    device_config = paired.device;
    cellular_config = paired.cellular;
    return ESP_OK;
}

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

    PairedConfig paired{};
    paired.device = device_config;
    paired.cellular = cellular_config;
    // NVS replaces one blob atomically; nvs_commit does not group separate keys.
    err = nvs_set_blob(handle, kPairedConfigKey, &paired, sizeof(paired));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}

}  // namespace air360
