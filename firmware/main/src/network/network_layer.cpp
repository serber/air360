#include "air360/network/network_layer.hpp"

#include <cinttypes>
#include <cstdint>

#include "air360/config_load_status.hpp"
#include "air360/platform/platform_layer.hpp"
#include "air360/status_service.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

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

bool hasStationConfig(const DeviceConfig& config) {
    return config.wifi_sta_ssid[0] != '\0';
}

void debugWindowCallback(void* arg) {
    auto* nm = static_cast<NetworkManager*>(arg);
    if (nm != nullptr) {
        nm->requestStopStation();
    }
}

}  // namespace

NetworkLayer::NetworkLayer()
    : cellular_config_(makeDefaultCellularConfig()) {}

void NetworkLayer::bootCellular(PlatformLayer& /*platform*/, StatusService& status_service) {
    cellular_config_ = makeDefaultCellularConfig();
    bool cellular_config_loaded = false;
    bool cellular_defaults_written = false;

    ESP_LOGI(kTag, "Boot step 4b/9: load or create cellular config");
    const esp_err_t cellular_config_err = cellular_config_repository_.loadOrCreate(
        cellular_config_,
        cellular_config_loaded,
        cellular_defaults_written);
    if (cellular_config_err != ESP_OK) {
        ESP_LOGW(
            kTag,
            "Cellular config load failed, using in-memory defaults: %s",
            esp_err_to_name(cellular_config_err));
        cellular_config_ = makeDefaultCellularConfig();
    }
    const ConfigLoadSource cellular_config_source =
        resolveConfigLoadSource(cellular_config_loaded);
    status_service.recordConfigLoad(
        ConfigRepositoryKind::kCellular,
        cellular_config_source,
        cellular_config_err,
        cellular_defaults_written);
    ESP_LOGI(
        kTag,
        "Cellular config source: %s; wrote defaults: %s",
        configLoadSourceLabel(cellular_config_source),
        cellular_defaults_written ? "yes" : "no");
    ESP_LOGI(
        kTag,
        "Cellular uplink: %s",
        cellular_config_.enabled ? "enabled" : "disabled");

    cellular_manager_.init(network_manager_);
    cellular_manager_.start(cellular_config_);
    status_service.setCellularManager(cellular_manager_);
}

void NetworkLayer::bootWifi(PlatformLayer& platform, StatusService& status_service) {
    DeviceConfig& config = platform.deviceConfig();

    ESP_LOGI(kTag, "Boot step 7/9: resolve network mode");
    if (cellular_config_.enabled != 0U) {
        // Cellular is the primary uplink.  Wi-Fi station is started only if
        // credentials exist, giving the operator a debug window at boot.
        // No AP fallback here — if cellular also fails, the fallback cascade
        // is driven by CellularManager (Phase 1).
        if (hasStationConfig(config)) {
            const esp_err_t station_err = network_manager_.connectStation(config);
            if (station_err != ESP_OK) {
                ESP_LOGW(
                    kTag,
                    "Station join failed during cellular debug window: %s",
                    esp_err_to_name(station_err));
            }

            if (cellular_config_.wifi_debug_window_s > 0U && debug_window_timer_ == nullptr) {
                esp_timer_create_args_t timer_args{};
                timer_args.callback = debugWindowCallback;
                timer_args.arg = &network_manager_;
                timer_args.name = "wifi_dbg_win";
                const esp_err_t timer_err =
                    esp_timer_create(&timer_args, &debug_window_timer_);
                if (timer_err == ESP_OK) {
                    const std::uint64_t window_us =
                        static_cast<std::uint64_t>(cellular_config_.wifi_debug_window_s) *
                        1000000ULL;
                    esp_timer_start_once(debug_window_timer_, window_us);
                    ESP_LOGI(
                        kTag,
                        "Wi-Fi debug window active (%" PRIu16 " s)",
                        cellular_config_.wifi_debug_window_s);
                } else {
                    ESP_LOGW(
                        kTag,
                        "Failed to create debug window timer: %s",
                        esp_err_to_name(timer_err));
                }
            }
        } else {
            ESP_LOGI(kTag, "Cellular uplink, no station credentials — skipping Wi-Fi");
        }
    } else {
        // Cellular disabled — standard Wi-Fi / setup-AP flow.
        if (hasStationConfig(config)) {
            ESP_LOGI(kTag, "Station config present, attempting normal mode Wi-Fi join");
            const esp_err_t station_err = network_manager_.connectStation(config);
            if (station_err != ESP_OK) {
                ESP_LOGW(
                    kTag,
                    "Station join failed, falling back to setup AP: %s",
                    esp_err_to_name(station_err));
                const esp_err_t ap_err = network_manager_.startLabAp(config);
                if (ap_err != ESP_OK) {
                    ESP_LOGW(kTag, "Setup AP start failed: %s", esp_err_to_name(ap_err));
                }
            }
        } else {
            ESP_LOGI(kTag, "No station config present, entering setup AP mode");
            const esp_err_t ap_err = network_manager_.startLabAp(config);
            if (ap_err != ESP_OK) {
                ESP_LOGW(kTag, "Setup AP start failed: %s", esp_err_to_name(ap_err));
            }
        }
    }
    status_service.setNetworkState(network_manager_.state());
    status_service.setCellularState(cellular_manager_.state());
}

}  // namespace air360
