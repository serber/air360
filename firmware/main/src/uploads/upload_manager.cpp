#include "air360/uploads/upload_manager.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

#include "air360/time_utils.hpp"
#include "esp_err.h"
#include "esp_log.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.upload";
constexpr TickType_t kUploadLoopDelay = pdMS_TO_TICKS(1000);
constexpr std::uint32_t kUploadTaskStackSize = 7168U;
constexpr UBaseType_t kUploadTaskPriority = 4U;

std::string defaultDisplayName(
    const BackendDescriptor* descriptor,
    std::uint32_t id) {
    if (descriptor != nullptr && descriptor->display_name != nullptr) {
        return std::string(descriptor->display_name);
    }

    return std::string("Backend #") + std::to_string(id);
}

BackendRuntimeState classifyInitialState(
    bool enabled,
    bool implemented,
    bool configured) {
    if (!enabled) {
        return BackendRuntimeState::kDisabled;
    }
    if (!implemented) {
        return BackendRuntimeState::kNotImplemented;
    }
    if (!configured) {
        return BackendRuntimeState::kError;
    }
    return BackendRuntimeState::kIdle;
}

bool isDegraded(const BackendStatusSnapshot& backend) {
    if (!backend.enabled) {
        return false;
    }

    return backend.state == BackendRuntimeState::kError ||
           backend.state == BackendRuntimeState::kNotImplemented;
}

}  // namespace

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

void UploadManager::applyConfig(const BackendConfigList& config) {
    stop();
    ensureMutex();

    std::vector<ManagedBackend> next_backends = buildManagedBackends(config);

    lock();
    backends_ = std::move(next_backends);
    cycle_interval_ms_ = config.upload_interval_ms;
    next_cycle_time_ms_ = uptimeMilliseconds();
    startLocked();
    unlock();
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
        managed.snapshot.implemented = descriptor != nullptr && descriptor->implemented;

        std::string validation_error;
        const bool configured =
            descriptor != nullptr && registry.validateRecord(record, validation_error);
        managed.snapshot.configured = configured;
        managed.snapshot.state = classifyInitialState(
            managed.snapshot.enabled,
            managed.snapshot.implemented,
            configured);

        if (!validation_error.empty()) {
            managed.snapshot.last_result = UploadResultClass::kConfigError;
            managed.snapshot.last_error = validation_error;
        } else if (managed.snapshot.enabled && !managed.snapshot.implemented) {
            managed.snapshot.last_result = UploadResultClass::kUnsupported;
            managed.snapshot.last_error = "Backend is not implemented yet.";
        }

        if (managed.snapshot.enabled &&
            managed.snapshot.implemented &&
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
        const NetworkState& network = network_manager_->state();
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

bool UploadManager::hasNetworkForUpload() const {
    if (network_manager_ == nullptr) {
        return false;
    }

    const NetworkState& network = network_manager_->state();
    return network.mode == NetworkMode::kStation && network.station_connected &&
           network_manager_->hasValidTime();
}

void UploadManager::stop() {
    ensureMutex();

    lock();
    const bool had_task = task_ != nullptr;
    stop_requested_ = true;
    unlock();

    if (had_task) {
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(10));
            lock();
            const bool task_stopped = task_ == nullptr;
            unlock();
            if (task_stopped) {
                break;
            }
        }
    }

    lock();
    stop_requested_ = false;
    unlock();
}

std::vector<BackendStatusSnapshot> UploadManager::backends() const {
    ensureMutex();
    lock();
    std::vector<BackendStatusSnapshot> snapshot;
    snapshot.reserve(backends_.size());
    for (const auto& backend : backends_) {
        snapshot.push_back(backend.snapshot);
    }
    unlock();
    return snapshot;
}

bool UploadManager::backendStatus(BackendType type, BackendStatusSnapshot& out_status) const {
    ensureMutex();
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
    ensureMutex();
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
    ensureMutex();
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
    ensureMutex();
    lock();
    const std::uint32_t value = cycle_interval_ms_;
    unlock();
    return value;
}

std::uint64_t UploadManager::lastOverallAttemptUptimeMs() const {
    ensureMutex();
    lock();
    const std::uint64_t value = last_overall_attempt_uptime_ms_;
    unlock();
    return value;
}

std::int64_t UploadManager::lastOverallAttemptUnixMs() const {
    ensureMutex();
    lock();
    const std::int64_t value = last_overall_attempt_unix_ms_;
    unlock();
    return value;
}

void UploadManager::ensureMutex() const {
    if (mutex_ == nullptr) {
        mutex_ = xSemaphoreCreateMutexStatic(&mutex_buffer_);
    }
}

void UploadManager::lock() const {
    xSemaphoreTake(mutex_, portMAX_DELAY);
}

void UploadManager::unlock() const {
    xSemaphoreGive(mutex_);
}

void UploadManager::startLocked() {
    if (task_ != nullptr) {
        return;
    }

    bool has_pollable_backend = false;
    for (const auto& backend : backends_) {
        if (backend.snapshot.enabled && backend.uploader != nullptr) {
            has_pollable_backend = true;
            break;
        }
    }

    if (!has_pollable_backend) {
        return;
    }

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
    }
}

void UploadManager::taskEntry(void* arg) {
    static_cast<UploadManager*>(arg)->taskMain();
}

void UploadManager::taskMain() {
    for (;;) {
        lock();
        const bool stop_requested = stop_requested_;
        const std::size_t backend_count = backends_.size();
        const std::uint64_t next_cycle_time_ms = next_cycle_time_ms_;
        bool has_active_backend = false;
        for (const auto& backend : backends_) {
            if (backend.snapshot.enabled && backend.uploader != nullptr) {
                has_active_backend = true;
                break;
            }
        }
        unlock();
        if (stop_requested) {
            break;
        }

        const std::uint64_t now_ms = uptimeMilliseconds();
        if (now_ms < next_cycle_time_ms) {
            vTaskDelay(kUploadLoopDelay);
            continue;
        }

        std::vector<MeasurementSample> upload_samples;
        if (has_active_backend && measurement_store_ != nullptr) {
            upload_samples = measurement_store_->beginUploadWindow();
        }

        MeasurementBatch batch{};
        if (!upload_samples.empty()) {
            batch = buildMeasurementBatch(now_ms, upload_samples);
        }

        bool had_active_backend = false;
        bool all_uploads_succeeded = true;

        for (std::size_t index = 0; index < backend_count; ++index) {
            IBackendUploader* uploader = nullptr;
            BackendRecord record{};
            BackendStatusSnapshot base_snapshot{};

            lock();
            if (index < backends_.size()) {
                auto& backend = backends_[index];
                if (backend.snapshot.enabled && backend.uploader != nullptr) {
                    backend.snapshot.state = BackendRuntimeState::kUploading;
                    uploader = backend.uploader.get();
                    record = backend.record;
                    base_snapshot = backend.snapshot;
                    had_active_backend = true;
                }
            }
            unlock();

            if (uploader == nullptr) {
                continue;
            }

            const std::int64_t unix_ms = currentUnixMilliseconds();
            UploadResultClass aggregate_result = UploadResultClass::kUnknown;
            BackendRuntimeState next_state = BackendRuntimeState::kIdle;
            std::string last_error;
            int last_http_status = 0;
            std::uint32_t last_response_time_ms = 0U;
            std::uint32_t next_retry_count = base_snapshot.retry_count;
            const std::int64_t attempt_started_us = esp_timer_get_time();

            lock();
            last_overall_attempt_uptime_ms_ = now_ms;
            last_overall_attempt_unix_ms_ = unix_ms;
            unlock();

            if (!hasNetworkForUpload()) {
                aggregate_result = UploadResultClass::kNoNetwork;
                next_state = BackendRuntimeState::kError;
                last_error = "Station uplink is not connected.";
                ++next_retry_count;
                all_uploads_succeeded = false;
            } else {
                std::vector<UploadRequestSpec> requests;
                if (!uploader->buildRequests(record, batch, requests, last_error)) {
                    aggregate_result = UploadResultClass::kConfigError;
                    next_state = BackendRuntimeState::kError;
                    ++next_retry_count;
                    all_uploads_succeeded = false;
                } else if (requests.empty()) {
                    aggregate_result = UploadResultClass::kNoData;
                    next_state = BackendRuntimeState::kIdle;
                    last_error = "No uploadable measurements available.";
                } else {
                    aggregate_result = UploadResultClass::kSuccess;
                    next_state = BackendRuntimeState::kOk;

                    for (const auto& request : requests) {
                        const UploadTransportResponse response = transport_.execute(request);
                        const UploadResultClass request_result =
                            uploader->classifyResponse(response);
                        last_http_status = response.http_status;

                        if (request_result != UploadResultClass::kSuccess) {
                            aggregate_result = request_result;
                            next_state = BackendRuntimeState::kError;
                            if (response.transport_err != ESP_OK) {
                                last_error = esp_err_to_name(response.transport_err);
                            } else if (!response.body_snippet.empty()) {
                                last_error = response.body_snippet;
                            } else if (response.http_status != 0) {
                                last_error =
                                    std::string("HTTP ") + std::to_string(response.http_status);
                            } else {
                                last_error = "Upload failed.";
                            }
                            ++next_retry_count;
                            all_uploads_succeeded = false;
                        }
                    }
                }
            }

            const std::int64_t attempt_finished_us = esp_timer_get_time();
            if (attempt_finished_us > attempt_started_us) {
                last_response_time_ms = static_cast<std::uint32_t>(
                    (attempt_finished_us - attempt_started_us) / 1000LL);
            }

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

            backend.snapshot.last_attempt_uptime_ms = now_ms;
            backend.snapshot.last_attempt_unix_ms = unix_ms;
            backend.snapshot.last_result = aggregate_result;
            backend.snapshot.last_http_status = last_http_status;
            backend.snapshot.last_response_time_ms = last_response_time_ms;
            backend.snapshot.last_error = last_error;
            backend.snapshot.retry_count = next_retry_count;
            backend.snapshot.state = next_state;
            backend.next_action_time_ms = now_ms + cycle_interval_ms_;
            backend.snapshot.next_retry_uptime_ms = backend.next_action_time_ms;

            if (aggregate_result == UploadResultClass::kSuccess) {
                backend.snapshot.last_success_uptime_ms = now_ms;
                backend.snapshot.last_success_unix_ms = unix_ms;
                backend.snapshot.retry_count = 0U;
                backend.snapshot.last_error.clear();
            }
            unlock();
        }

        if (had_active_backend && measurement_store_ != nullptr) {
            if (all_uploads_succeeded) {
                measurement_store_->acknowledgeInflight();
            } else {
                measurement_store_->restoreInflight();
            }
        }

        lock();
        next_cycle_time_ms_ = now_ms + cycle_interval_ms_;
        unlock();

        vTaskDelay(kUploadLoopDelay);
    }

    lock();
    task_ = nullptr;
    unlock();
    vTaskDelete(nullptr);
}

}  // namespace air360
