#include "air360/platform/platform_layer.hpp"

#include <cstdint>

#include "air360/config_load_status.hpp"
#include "air360/status_service.hpp"
#include "esp_log.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.app";

ConfigLoadSource resolveConfigLoadSource(bool loaded_from_storage) {
    return loaded_from_storage ? ConfigLoadSource::kNvsPrimary : ConfigLoadSource::kDefaults;
}

const char* configLoadSourceLabel(ConfigLoadSource source) {
    switch (source) {
        case ConfigLoadSource::kNvsPrimary:
            return "NVS primary";
        case ConfigLoadSource::kNvsBackup:
            return "NVS backup";
        case ConfigLoadSource::kDefaults:
        default:
            return "defaults";
    }
}

}  // namespace

PlatformLayer::PlatformLayer()
    : build_info_(getBuildInfo()),
      config_(makeDefaultDeviceConfig()) {}

void PlatformLayer::boot(StatusService& status_service) {
    config_ = makeDefaultDeviceConfig();
    bool loaded_from_storage = false;
    bool wrote_defaults = false;

    ESP_LOGI(kTag, "Boot step 4/9: load or create device config");
    const esp_err_t config_err =
        config_repository_.loadOrCreate(config_, loaded_from_storage, wrote_defaults);
    if (config_err != ESP_OK) {
        ESP_LOGW(
            kTag,
            "Config load failed, using in-memory defaults: %s",
            esp_err_to_name(config_err));
        config_ = makeDefaultDeviceConfig();
    }
    const ConfigLoadSource device_config_source = resolveConfigLoadSource(loaded_from_storage);
    ESP_LOGI(
        kTag,
        "Device config source: %s; wrote defaults: %s",
        configLoadSourceLabel(device_config_source),
        wrote_defaults ? "yes" : "no");

    std::uint32_t boot_count = 0;
    const esp_err_t boot_count_err = config_repository_.incrementBootCount(boot_count);
    if (boot_count_err != ESP_OK) {
        ESP_LOGW(
            kTag,
            "Boot counter update failed: %s",
            esp_err_to_name(boot_count_err));
    }

    status_service.setConfig(config_, loaded_from_storage, wrote_defaults);
    status_service.recordConfigLoad(
        ConfigRepositoryKind::kDevice,
        device_config_source,
        config_err,
        wrote_defaults);
    status_service.setBootCount(boot_count);
}

}  // namespace air360
