#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>
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
#include "air360/uploads/upload_prune_policy.hpp"
#include "air360/uploads/upload_transport.hpp"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace air360 {

struct BackendStatusSnapshot {
    std::uint32_t id = 0U;
    bool enabled = false;
    bool configured = false;
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
    bool best_effort = false;
    std::uint32_t missed_sample_count = 0U;
    std::uint64_t best_effort_since_uptime_ms = 0U;
    std::uint64_t next_retry_uptime_ms = 0U;
    std::string last_error;
};

struct UploadManagerRuntimeSnapshot {
    std::vector<BackendStatusSnapshot> backends;
    std::size_t enabled_count = 0U;
    std::size_t degraded_count = 0U;
    std::size_t inflight_sample_count = 0U;
    std::uint32_t upload_interval_ms = 0U;
    std::uint64_t last_overall_attempt_uptime_ms = 0U;
    std::int64_t last_overall_attempt_unix_ms = 0;
};

class UploadManager {
  public:
    UploadManager();
    UploadManager(const UploadManager&) = delete;
    UploadManager& operator=(const UploadManager&) = delete;
    UploadManager(UploadManager&&) = delete;
    UploadManager& operator=(UploadManager&&) = delete;

    void start(
        const BuildInfo& build_info,
        const DeviceConfig& device_config,
        const SensorManager& sensor_manager,
        MeasurementStore& measurement_store,
        const NetworkManager& network_manager);
    [[nodiscard]] esp_err_t applyConfig(const BackendConfigList& config);
    [[nodiscard]] esp_err_t stop();

    std::vector<BackendStatusSnapshot> backends() const;
    UploadManagerRuntimeSnapshot runtimeSnapshot() const;
    bool backendStatus(BackendType type, BackendStatusSnapshot& out_status) const;
    std::size_t enabledCount() const;
    std::size_t degradedCount() const;
    std::uint32_t uploadIntervalMs() const;
    std::uint64_t lastOverallAttemptUptimeMs() const;
    std::int64_t lastOverallAttemptUnixMs() const;
    std::size_t taskStackHighWaterMarkBytes() const;

  private:
    struct ManagedBackend {
        BackendRecord record{};
        const BackendDescriptor* descriptor = nullptr;
        std::unique_ptr<IBackendUploader> uploader;
        BackendStatusSnapshot snapshot{};
        std::uint64_t next_action_time_ms = 0U;
        std::uint64_t acknowledged_sample_id = 0U;
        std::uint64_t inflight_last_sample_id = 0U;
        std::uint64_t first_failure_uptime_ms = 0U;
        std::vector<std::uint64_t> inflight_sample_ids;
        std::vector<MeasurementSample> inflight_samples;
    };

    void lock() const;
    void unlock() const;
    std::vector<ManagedBackend> buildManagedBackends(const BackendConfigList& config) const;
    MeasurementBatch buildMeasurementBatch(
        std::uint64_t now_ms,
        const std::vector<MeasurementSample>& samples) const;
    bool hasNetworkForUpload(std::string& last_error) const;
    PerBackendCursor pruneCursorsLocked() const;
    esp_err_t startLocked();
    bool stopRequested() const;
    static void taskEntry(void* arg);
    static bool deliveryStopRequested(void* arg);
    static void deliveryWatchdogReset(void* arg, const char* checkpoint);
    void taskMain();

    static constexpr EventBits_t kTaskStoppedBit = BIT0;

    BuildInfo build_info_{};
    const DeviceConfig* device_config_ = nullptr;
    const SensorManager* sensor_manager_ = nullptr;
    MeasurementStore* measurement_store_ = nullptr;
    const NetworkManager* network_manager_ = nullptr;
    mutable StaticSemaphore_t mutex_buffer_{};
    mutable SemaphoreHandle_t mutex_ = nullptr;
    StaticEventGroup_t lifecycle_events_buffer_{};
    EventGroupHandle_t lifecycle_events_ = nullptr;
    TaskHandle_t task_ = nullptr;
    std::atomic_bool stop_requested_{false};
    std::vector<ManagedBackend> backends_;
    UploadTransport transport_{};
    std::uint64_t last_overall_attempt_uptime_ms_ = 0U;
    std::int64_t last_overall_attempt_unix_ms_ = 0;
    std::uint64_t next_cycle_time_ms_ = 0U;
    std::uint32_t cycle_interval_ms_ = 145000U;
};

}  // namespace air360
