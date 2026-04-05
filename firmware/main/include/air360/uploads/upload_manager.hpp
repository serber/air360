#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "air360/build_info.hpp"
#include "air360/config_repository.hpp"
#include "air360/network_manager.hpp"
#include "air360/sensors/sensor_manager.hpp"
#include "air360/uploads/backend_config.hpp"
#include "air360/uploads/backend_registry.hpp"
#include "air360/uploads/measurement_batch.hpp"
#include "air360/uploads/measurement_store.hpp"
#include "air360/uploads/upload_transport.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace air360 {

struct BackendStatusSnapshot {
    std::uint32_t id = 0U;
    bool enabled = false;
    bool configured = false;
    bool implemented = false;
    BackendType backend_type = BackendType::kUnknown;
    std::string backend_key;
    std::string display_name;
    BackendRuntimeState state = BackendRuntimeState::kDisabled;
    UploadResultClass last_result = UploadResultClass::kUnknown;
    std::uint64_t last_attempt_uptime_ms = 0U;
    std::uint64_t last_success_uptime_ms = 0U;
    std::int64_t last_attempt_unix_ms = 0;
    std::int64_t last_success_unix_ms = 0;
    int last_http_status = 0;
    std::uint32_t last_response_time_ms = 0U;
    std::uint32_t retry_count = 0U;
    std::uint64_t next_retry_uptime_ms = 0U;
    std::string last_error;
};

class UploadManager {
  public:
    void start(
        const BuildInfo& build_info,
        const DeviceConfig& device_config,
        const SensorManager& sensor_manager,
        MeasurementStore& measurement_store,
        const NetworkManager& network_manager);
    void applyConfig(const BackendConfigList& config);
    void stop();

    std::vector<BackendStatusSnapshot> backends() const;
    bool backendStatus(BackendType type, BackendStatusSnapshot& out_status) const;
    std::size_t enabledCount() const;
    std::size_t degradedCount() const;
    std::uint32_t uploadIntervalMs() const;
    std::uint64_t lastOverallAttemptUptimeMs() const;
    std::int64_t lastOverallAttemptUnixMs() const;

  private:
    struct ManagedBackend {
        BackendRecord record{};
        const BackendDescriptor* descriptor = nullptr;
        std::unique_ptr<IBackendUploader> uploader;
        BackendStatusSnapshot snapshot{};
        std::uint64_t next_action_time_ms = 0U;
    };

    void ensureMutex() const;
    void lock() const;
    void unlock() const;
    std::vector<ManagedBackend> buildManagedBackends(const BackendConfigList& config) const;
    MeasurementBatch buildMeasurementBatch(
        std::uint64_t now_ms,
        const std::vector<MeasurementSample>& samples) const;
    bool hasNetworkForUpload(std::string& last_error) const;
    void startLocked();
    static void taskEntry(void* arg);
    void taskMain();

    BuildInfo build_info_{};
    const DeviceConfig* device_config_ = nullptr;
    const SensorManager* sensor_manager_ = nullptr;
    MeasurementStore* measurement_store_ = nullptr;
    const NetworkManager* network_manager_ = nullptr;
    mutable StaticSemaphore_t mutex_buffer_{};
    mutable SemaphoreHandle_t mutex_ = nullptr;
    TaskHandle_t task_ = nullptr;
    bool stop_requested_ = false;
    std::vector<ManagedBackend> backends_;
    UploadTransport transport_{};
    std::uint64_t last_overall_attempt_uptime_ms_ = 0U;
    std::int64_t last_overall_attempt_unix_ms_ = 0;
    std::uint64_t next_cycle_time_ms_ = 0U;
    std::uint32_t cycle_interval_ms_ = 145000U;
};

}  // namespace air360
