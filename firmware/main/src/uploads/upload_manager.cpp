#include "air360/uploads/upload_manager.hpp"

#include <algorithm>
#include <cinttypes>
#include <cstdint>
#include <string>
#include <utility>

#include "air360/time_utils.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.upload";
constexpr TickType_t kUploadLoopDelay = pdMS_TO_TICKS(1000);
constexpr std::uint32_t kUploadTaskStackSize = 7168U;
constexpr UBaseType_t kUploadTaskPriority = 4U;
constexpr std::size_t kMaxSamplesPerUploadWindow = 32U;
constexpr std::uint32_t kBacklogDrainDelayMs = 5000U;
constexpr std::uint32_t kStopTimeoutMs = 30000U;

std::string defaultDisplayName(
    const BackendDescriptor* descriptor,
    std::uint32_t id) {
    if (descriptor != nullptr && descriptor->display_name != nullptr) {
        return std::string(descriptor->display_name);
    }

    return std::string("Backend #") + std::to_string(id);
}

BackendRuntimeState classifyInitialState(bool enabled, bool configured) {
    if (!enabled) {
        return BackendRuntimeState::kDisabled;
    }
    if (!configured) {
        return BackendRuntimeState::kError;
    }
    return BackendRuntimeState::kIdle;
}

bool isDegraded(const BackendStatusSnapshot& backend) {
    return backend.enabled && backend.state == BackendRuntimeState::kError;
}

}  // namespace

UploadManager::UploadManager() {
    mutex_ = xSemaphoreCreateMutexStatic(&mutex_buffer_);
    lifecycle_events_ = xEventGroupCreateStatic(&lifecycle_events_buffer_);
}

void UploadManager::start(
    const BuildInfo& build_info,
    const DeviceConfig& device_config,
    const SensorManager& sensor_manager,
    MeasurementStore& measurement_store,
    const NetworkManager& network_manager) {
    build_info_ = build_info;
    device_config_ = &device_config;
    sensor_manager_ = &sensor_manager;
    measurement_store_ = &measurement_store;
    network_manager_ = &network_manager;
}

esp_err_t UploadManager::applyConfig(const BackendConfigList& config) {
    std::vector<std::pair<std::uint32_t, std::uint64_t>> acknowledged_by_id;
    lock();
    acknowledged_by_id.reserve(backends_.size());
    for (const auto& backend : backends_) {
        acknowledged_by_id.emplace_back(backend.snapshot.id, backend.acknowledged_sample_id);
    }
    unlock();

    const esp_err_t stop_err = stop();
    if (stop_err != ESP_OK) {
        ESP_LOGE(
            kTag,
            "Upload reconfigure aborted: previous task did not stop within %" PRIu32 " ms",
            kStopTimeoutMs);
        return stop_err;
    }

    std::vector<ManagedBackend> next_backends = buildManagedBackends(config);
    for (auto& backend : next_backends) {
        for (const auto& previous : acknowledged_by_id) {
            if (previous.first == backend.snapshot.id) {
                backend.acknowledged_sample_id = previous.second;
                break;
            }
        }
    }

    lock();
    backends_ = std::move(next_backends);
    cycle_interval_ms_ = config.upload_interval_ms;
    next_cycle_time_ms_ = uptimeMilliseconds();
    const esp_err_t start_err = startLocked();
    unlock();
    return start_err;
}

std::vector<UploadManager::ManagedBackend> UploadManager::buildManagedBackends(
    const BackendConfigList& config) const {
    BackendRegistry registry;
    const std::uint64_t now_ms = uptimeMilliseconds();
    std::vector<ManagedBackend> backends;
    backends.reserve(config.backend_count);

    for (std::size_t index = 0; index < config.backend_count; ++index) {
        const BackendRecord& record = config.backends[index];
        const BackendDescriptor* descriptor = registry.findByType(record.backend_type);

        ManagedBackend managed;
        managed.record = record;
        managed.descriptor = descriptor;
        managed.snapshot.id = record.id;
        managed.snapshot.enabled = record.enabled != 0U;
        managed.snapshot.backend_type = record.backend_type;
        managed.snapshot.backend_key =
            descriptor != nullptr ? descriptor->backend_key : std::string("unknown");
        managed.snapshot.display_name =
            record.display_name[0] != '\0' ? std::string(record.display_name)
                                           : defaultDisplayName(descriptor, record.id);
        std::string validation_error;
        const bool configured =
            descriptor != nullptr && registry.validateRecord(record, validation_error);
        managed.snapshot.configured = configured;
        managed.snapshot.state = classifyInitialState(managed.snapshot.enabled, configured);

        if (!validation_error.empty()) {
            managed.snapshot.last_result = UploadResultClass::kConfigError;
            managed.snapshot.last_error = validation_error;
        }

        if (managed.snapshot.enabled &&
            configured &&
            descriptor != nullptr &&
            descriptor->create_uploader != nullptr) {
            managed.uploader = descriptor->create_uploader();
            if (!managed.uploader) {
                managed.snapshot.state = BackendRuntimeState::kError;
                managed.snapshot.last_result = UploadResultClass::kConfigError;
                managed.snapshot.last_error = "Failed to allocate backend uploader.";
            } else {
                managed.next_action_time_ms = now_ms;
            }
        }

        backends.push_back(std::move(managed));
    }

    return backends;
}

MeasurementBatch UploadManager::buildMeasurementBatch(
    std::uint64_t now_ms,
    const std::vector<MeasurementSample>& samples) const {
    MeasurementBatch batch;
    batch.created_uptime_ms = now_ms;
    batch.created_unix_ms = currentUnixMilliseconds();
    batch.batch_id = batch.created_unix_ms > 0 ? static_cast<std::uint64_t>(batch.created_unix_ms)
                                               : now_ms;
    batch.device_name = device_config_ != nullptr ? device_config_->device_name : "";
    batch.board_name = build_info_.board_name;
    batch.project_version = build_info_.project_version;
    batch.chip_id = build_info_.chip_id;
    batch.short_chip_id = build_info_.short_chip_id;
    batch.esp_mac_id = build_info_.esp_mac_id;

    if (network_manager_ != nullptr) {
        const NetworkState network = network_manager_->state();
        batch.network_mode = network.mode;
        batch.station_connected = network.station_connected;
    }

    for (const auto& sample : samples) {
        for (std::size_t index = 0; index < sample.measurement.value_count; ++index) {
            const SensorValue& value = sample.measurement.values[index];
            batch.points.push_back(
                MeasurementPoint{
                    sample.sensor_id,
                    sample.sensor_type,
                    value.kind,
                    value.value,
                    sample.sample_time_ms,
                });
        }
    }

    return batch;
}

bool UploadManager::hasNetworkForUpload(std::string& last_error) const {
    if (network_manager_ == nullptr) {
        last_error = "Network manager is not available.";
        return false;
    }

    if (network_manager_->uplinkStatus().uplink_ready) {
        last_error.clear();
        return true;
    }

    // Bearer is up but time is not yet valid: provide a specific message.
    const NetworkState network = network_manager_->state();
    const bool bearer_up = (network.mode == NetworkMode::kStation && network.station_connected);
    if (bearer_up) {
        if (!network.time_sync_error.empty()) {
            last_error = "Unix time is not valid yet: " + network.time_sync_error;
        } else {
            last_error = "Unix time is not valid yet.";
        }
    } else {
        last_error = "Uplink is not ready.";
    }
    return false;
}

esp_err_t UploadManager::stop() {
    lock();
    const TaskHandle_t task = task_;
    if (task != nullptr) {
        stop_requested_.store(true, std::memory_order_release);
        xTaskNotifyGive(task);
    }
    unlock();

    if (task != nullptr) {
        const EventBits_t bits = xEventGroupWaitBits(
            lifecycle_events_,
            kTaskStoppedBit,
            pdTRUE,
            pdFALSE,
            pdMS_TO_TICKS(kStopTimeoutMs));
        if ((bits & kTaskStoppedBit) == 0U) {
            ESP_LOGE(
                kTag,
                "Timed out waiting for upload manager task to stop (%" PRIu32 " ms)",
                kStopTimeoutMs);
            return ESP_ERR_TIMEOUT;
        }
    }

    stop_requested_.store(false, std::memory_order_release);
    return ESP_OK;
}

std::vector<BackendStatusSnapshot> UploadManager::backends() const {
    lock();
    std::vector<BackendStatusSnapshot> snapshot;
    snapshot.reserve(backends_.size());
    for (const auto& backend : backends_) {
        snapshot.push_back(backend.snapshot);
    }
    unlock();
    return snapshot;
}

UploadManagerRuntimeSnapshot UploadManager::runtimeSnapshot() const {
    lock();

    UploadManagerRuntimeSnapshot snapshot;
    snapshot.backends.reserve(backends_.size());
    snapshot.upload_interval_ms = cycle_interval_ms_;
    snapshot.last_overall_attempt_uptime_ms = last_overall_attempt_uptime_ms_;
    snapshot.last_overall_attempt_unix_ms = last_overall_attempt_unix_ms_;
    for (const auto& backend : backends_) {
        snapshot.backends.push_back(backend.snapshot);
        if (backend.snapshot.enabled) {
            ++snapshot.enabled_count;
        }
        if (isDegraded(backend.snapshot)) {
            ++snapshot.degraded_count;
        }
    }

    std::vector<std::uint64_t> unique_inflight_ids;
    for (const auto& backend : backends_) {
        for (const auto sample_id : backend.inflight_sample_ids) {
            if (std::find(unique_inflight_ids.begin(), unique_inflight_ids.end(), sample_id) ==
                unique_inflight_ids.end()) {
                unique_inflight_ids.push_back(sample_id);
            }
        }
    }
    snapshot.inflight_sample_count = unique_inflight_ids.size();

    unlock();
    return snapshot;
}

bool UploadManager::backendStatus(BackendType type, BackendStatusSnapshot& out_status) const {
    lock();
    for (const auto& backend : backends_) {
        if (backend.snapshot.backend_type == type) {
            out_status = backend.snapshot;
            unlock();
            return true;
        }
    }
    unlock();
    out_status = BackendStatusSnapshot{};
    return false;
}

std::size_t UploadManager::enabledCount() const {
    lock();
    std::size_t count = 0U;
    for (const auto& backend : backends_) {
        if (backend.snapshot.enabled) {
            ++count;
        }
    }
    unlock();
    return count;
}

std::size_t UploadManager::degradedCount() const {
    lock();
    std::size_t count = 0U;
    for (const auto& backend : backends_) {
        if (isDegraded(backend.snapshot)) {
            ++count;
        }
    }
    unlock();
    return count;
}

std::uint32_t UploadManager::uploadIntervalMs() const {
    lock();
    const std::uint32_t value = cycle_interval_ms_;
    unlock();
    return value;
}

std::uint64_t UploadManager::lastOverallAttemptUptimeMs() const {
    lock();
    const std::uint64_t value = last_overall_attempt_uptime_ms_;
    unlock();
    return value;
}

std::int64_t UploadManager::lastOverallAttemptUnixMs() const {
    lock();
    const std::int64_t value = last_overall_attempt_unix_ms_;
    unlock();
    return value;
}

std::size_t UploadManager::taskStackHighWaterMarkBytes() const {
    lock();
    const TaskHandle_t task = task_;
    unlock();

    if (task == nullptr) {
        return 0U;
    }

    return static_cast<std::size_t>(uxTaskGetStackHighWaterMark(task)) * sizeof(StackType_t);
}

void UploadManager::lock() const {
    xSemaphoreTake(mutex_, portMAX_DELAY);
}

void UploadManager::unlock() const {
    xSemaphoreGive(mutex_);
}

esp_err_t UploadManager::startLocked() {
    if (task_ != nullptr) {
        return ESP_OK;
    }

    bool has_pollable_backend = false;
    for (const auto& backend : backends_) {
        if (backend.snapshot.enabled && backend.uploader != nullptr) {
            has_pollable_backend = true;
            break;
        }
    }

    if (!has_pollable_backend) {
        return ESP_OK;
    }

    stop_requested_.store(false, std::memory_order_release);
    xEventGroupClearBits(lifecycle_events_, kTaskStoppedBit);

    const BaseType_t created = xTaskCreate(
        &UploadManager::taskEntry,
        "air360_upload",
        kUploadTaskStackSize,
        this,
        kUploadTaskPriority,
        &task_);
    if (created != pdPASS) {
        ESP_LOGE(kTag, "Failed to start upload manager task");
        task_ = nullptr;
        for (auto& backend : backends_) {
            if (backend.snapshot.enabled) {
                backend.snapshot.state = BackendRuntimeState::kError;
                backend.snapshot.last_result = UploadResultClass::kConfigError;
                backend.snapshot.last_error = "Failed to start upload manager task.";
            }
        }
        return ESP_FAIL;
    }

    return ESP_OK;
}

bool UploadManager::stopRequested() const {
    return stop_requested_.load(std::memory_order_acquire);
}

void UploadManager::taskEntry(void* arg) {
    static_cast<UploadManager*>(arg)->taskMain();
}

void UploadManager::taskMain() {
    esp_task_wdt_add(nullptr);
    ESP_LOGI(kTag, "TWDT: air360_upload subscribed");

    for (;;) {
        const std::uint64_t now_ms = uptimeMilliseconds();
        bool has_active_backend = false;
        std::vector<std::size_t> due_indices;

        lock();
        const bool stop_requested = stop_requested_.load(std::memory_order_acquire);
        due_indices.reserve(backends_.size());
        for (std::size_t index = 0; index < backends_.size(); ++index) {
            const auto& backend = backends_[index];
            if (!backend.snapshot.enabled || backend.uploader == nullptr) {
                continue;
            }

            has_active_backend = true;
            if (now_ms >= backend.next_action_time_ms) {
                due_indices.push_back(index);
            }
        }
        unlock();

        if (stop_requested) {
            break;
        }

        if (!has_active_backend ||
            due_indices.empty() ||
            measurement_store_ == nullptr ||
            network_manager_ == nullptr) {
            static_cast<void>(ulTaskNotifyTake(pdTRUE, kUploadLoopDelay));
            esp_task_wdt_reset();
            continue;
        }

        for (const std::size_t index : due_indices) {
            if (stopRequested()) {
                break;
            }

            IBackendUploader* uploader = nullptr;
            BackendRecord record{};
            BackendStatusSnapshot base_snapshot{};
            std::uint64_t acknowledged_sample_id = 0U;
            std::uint64_t inflight_last_sample_id = 0U;
            std::vector<std::uint64_t> upload_sample_ids;
            std::vector<MeasurementSample> upload_samples;
            bool had_retry_window = false;

            lock();
            if (index < backends_.size()) {
                auto& backend = backends_[index];
                if (backend.snapshot.enabled &&
                    backend.uploader != nullptr &&
                    now_ms >= backend.next_action_time_ms) {
                    backend.snapshot.state = BackendRuntimeState::kUploading;
                    uploader = backend.uploader.get();
                    record = backend.record;
                    base_snapshot = backend.snapshot;
                    acknowledged_sample_id = backend.acknowledged_sample_id;
                    inflight_last_sample_id = backend.inflight_last_sample_id;
                    upload_sample_ids = backend.inflight_sample_ids;
                    upload_samples = backend.inflight_samples;
                    had_retry_window = !backend.inflight_samples.empty();
                }
            }
            unlock();

            if (uploader == nullptr) {
                continue;
            }

            if (!had_retry_window) {
                MeasurementQueueWindow window =
                    measurement_store_->uploadWindowAfter(
                        acknowledged_sample_id,
                        kMaxSamplesPerUploadWindow);
                if (!window.empty()) {
                    upload_sample_ids = std::move(window.sample_ids);
                    upload_samples = std::move(window.samples);
                    inflight_last_sample_id = upload_sample_ids.back();

                    lock();
                    if (index < backends_.size() && backends_[index].uploader.get() == uploader) {
                        auto& backend = backends_[index];
                        backend.inflight_sample_ids = upload_sample_ids;
                        backend.inflight_samples = upload_samples;
                        backend.inflight_last_sample_id = inflight_last_sample_id;
                    } else {
                        uploader = nullptr;
                    }
                    unlock();

                    if (uploader == nullptr) {
                        continue;
                    }
                }
            }

            const std::int64_t unix_ms = currentUnixMilliseconds();
            UploadResultClass aggregate_result = UploadResultClass::kUnknown;
            BackendRuntimeState next_state = BackendRuntimeState::kIdle;
            std::string last_error;
            int last_http_status = 0;
            std::uint32_t last_response_time_ms = 0U;
            std::uint32_t next_retry_count = base_snapshot.retry_count;
            std::uint64_t next_action_time_ms = now_ms + cycle_interval_ms_;
            bool acknowledge_window = false;
            const std::int64_t attempt_started_us = esp_timer_get_time();

            if (upload_samples.empty()) {
                aggregate_result = UploadResultClass::kNoData;
                next_state = BackendRuntimeState::kIdle;
                next_retry_count = 0U;
            } else {
                lock();
                last_overall_attempt_uptime_ms_ = now_ms;
                last_overall_attempt_unix_ms_ = unix_ms;
                unlock();

                std::string network_error;
                if (!hasNetworkForUpload(network_error)) {
                    aggregate_result = UploadResultClass::kNoNetwork;
                    next_state = BackendRuntimeState::kError;
                    last_error = std::move(network_error);
                    ++next_retry_count;
                } else {
                    MeasurementBatch batch = buildMeasurementBatch(now_ms, upload_samples);
                    if (batch.empty()) {
                        aggregate_result = UploadResultClass::kNoData;
                        next_state = BackendRuntimeState::kIdle;
                        acknowledge_window = true;
                        next_retry_count = 0U;
                    } else {
                        std::vector<UploadRequestSpec> requests;
                        if (!uploader->buildRequests(record, batch, requests, last_error)) {
                            aggregate_result = UploadResultClass::kConfigError;
                            next_state = BackendRuntimeState::kError;
                            ++next_retry_count;
                        } else if (requests.empty()) {
                            aggregate_result = UploadResultClass::kNoData;
                            next_state = BackendRuntimeState::kIdle;
                            acknowledge_window = true;
                            next_retry_count = 0U;
                            last_error.clear();
                        } else {
                            aggregate_result = UploadResultClass::kSuccess;
                            next_state = BackendRuntimeState::kOk;
                            acknowledge_window = true;
                            next_retry_count = 0U;

                            for (const auto& request : requests) {
                                if (stopRequested()) {
                                    aggregate_result = UploadResultClass::kUnknown;
                                    next_state = BackendRuntimeState::kIdle;
                                    acknowledge_window = false;
                                    last_error = "Upload stopped before request completed.";
                                    break;
                                }

                                const UploadTransportResponse response = transport_.execute(request);
                                const UploadResultClass request_result =
                                    uploader->classifyResponse(response);
                                last_http_status = response.http_status;

                                if (request_result != UploadResultClass::kSuccess) {
                                    aggregate_result = request_result;
                                    next_state = BackendRuntimeState::kError;
                                    acknowledge_window = false;
                                    ++next_retry_count;
                                    if (response.transport_err != ESP_OK) {
                                        last_error = esp_err_to_name(response.transport_err);
                                    } else if (!response.body_snippet.empty()) {
                                        last_error = response.body_snippet;
                                    } else if (response.http_status != 0) {
                                        last_error =
                                            std::string("HTTP ") +
                                            std::to_string(response.http_status);
                                    } else {
                                        last_error = "Upload failed.";
                                    }
                                    break;
                                }
                            }
                        }
                    }
                }
            }

            const std::int64_t attempt_finished_us = esp_timer_get_time();
            if (attempt_finished_us > attempt_started_us) {
                last_response_time_ms = static_cast<std::uint32_t>(
                    (attempt_finished_us - attempt_started_us) / 1000LL);
            }

            std::uint64_t updated_acknowledged_sample_id = acknowledged_sample_id;
            if (acknowledge_window && !upload_sample_ids.empty()) {
                updated_acknowledged_sample_id = inflight_last_sample_id;
                if (measurement_store_->hasSamplesAfter(updated_acknowledged_sample_id)) {
                    next_action_time_ms =
                        now_ms +
                        std::min<std::uint64_t>(cycle_interval_ms_, kBacklogDrainDelayMs);
                }
            }

            std::uint64_t prune_up_to = 0U;
            lock();
            if (index >= backends_.size()) {
                unlock();
                continue;
            }

            auto& backend = backends_[index];
            if (backend.uploader.get() != uploader) {
                unlock();
                continue;
            }

            if (acknowledge_window) {
                backend.acknowledged_sample_id = updated_acknowledged_sample_id;
                backend.inflight_last_sample_id = 0U;
                backend.inflight_sample_ids.clear();
                backend.inflight_samples.clear();
            }

            backend.snapshot.last_attempt_uptime_ms = now_ms;
            backend.snapshot.last_attempt_unix_ms = unix_ms;
            backend.snapshot.last_result = aggregate_result;
            backend.snapshot.last_http_status = last_http_status;
            backend.snapshot.last_response_time_ms = last_response_time_ms;
            backend.snapshot.last_error = last_error;
            backend.snapshot.retry_count = next_retry_count;
            backend.snapshot.state = next_state;
            backend.next_action_time_ms = next_action_time_ms;
            backend.snapshot.next_retry_uptime_ms = next_action_time_ms;

            if (aggregate_result == UploadResultClass::kSuccess) {
                backend.snapshot.last_success_uptime_ms = now_ms;
                backend.snapshot.last_success_unix_ms = unix_ms;
                backend.snapshot.last_error.clear();
            } else if (aggregate_result == UploadResultClass::kNoData) {
                backend.snapshot.last_error.clear();
            }

            if (acknowledge_window) {
                bool have_active_cursor = false;
                for (const auto& candidate : backends_) {
                    if (!candidate.snapshot.enabled || candidate.uploader == nullptr) {
                        continue;
                    }

                    if (!have_active_cursor ||
                        candidate.acknowledged_sample_id < prune_up_to) {
                        prune_up_to = candidate.acknowledged_sample_id;
                        have_active_cursor = true;
                    }
                }

                if (!have_active_cursor) {
                    prune_up_to = 0U;
                }
            }
            unlock();

            if (acknowledge_window && prune_up_to > 0U) {
                measurement_store_->discardUpTo(prune_up_to);
            }
        }

        static_cast<void>(ulTaskNotifyTake(pdTRUE, kUploadLoopDelay));
        esp_task_wdt_reset();
    }

    lock();
    task_ = nullptr;
    unlock();
    esp_task_wdt_delete(nullptr);
    xEventGroupSetBits(lifecycle_events_, kTaskStoppedBit);
    vTaskDelete(nullptr);
}

}  // namespace air360
