#pragma once

#include <cstdint>
#include <string>

#include "air360/build_info.hpp"
#include "air360/cellular_manager.hpp"
#include "air360/config_repository.hpp"
#include "air360/network_manager.hpp"
#include "air360/sensors/sensor_manager.hpp"
#include "air360/uploads/measurement_store.hpp"
#include "air360/uploads/upload_manager.hpp"
#include "esp_system.h"

namespace air360 {

class StatusService {
  public:
    explicit StatusService(BuildInfo build_info);

    void markNvsReady(bool ready);
    void markWatchdogArmed(bool armed);
    void setConfig(
        const DeviceConfig& config,
        bool loaded_from_storage,
        bool wrote_defaults);
    void setBootCount(std::uint32_t boot_count);
    void setNetworkState(const NetworkState& state);
    void setCellularState(const CellularState& state);
    void setSensors(const SensorManager& sensor_manager);
    void setMeasurements(const MeasurementStore& measurement_store);
    void setUploads(const UploadManager& upload_manager);
    void setWebServerStarted(bool started);

    std::string renderRootHtml() const;
    std::string renderStatusJson() const;
    const NetworkState& networkState() const;
    const BuildInfo& buildInfo() const;

  private:
    BuildInfo build_info_;
    DeviceConfig config_{};
    NetworkState network_state_{};
    CellularState cellular_state_{};
    const SensorManager* sensor_manager_ = nullptr;
    const MeasurementStore* measurement_store_ = nullptr;
    const UploadManager* upload_manager_ = nullptr;
    std::uint32_t boot_count_ = 0;
    bool nvs_ready_ = false;
    bool watchdog_armed_ = false;
    bool config_loaded_from_storage_ = false;
    bool wrote_default_config_ = false;
    bool web_server_started_ = false;
    esp_reset_reason_t reset_reason_ = esp_reset_reason();
};

}  // namespace air360
