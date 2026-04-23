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
// The upload task wakes every second so per-backend timers react quickly
// without spending most of the idle period burning CPU.
constexpr TickType_t kUploadLoopDelay = pdMS_TO_TICKS(1000);
// 7 KB covers request construction, backend adapter calls, and status
// bookkeeping in a single task without forcing heap allocations for stack spill.
constexpr std::uint32_t kUploadTaskStackSize = 7168U;
// Keep uploads below the sensor task priority while still above passive idle work.
constexpr UBaseType_t kUploadTaskPriority = 4U;
// Limit each backend batch to 32 samples to bound HTTP body size and retry
// memory while still making progress through backlog on slow links.
constexpr std::size_t kMaxSamplesPerUploadWindow = 32U;
// 30 s stop timeout matches the TWDT budget and the worst-case in-flight
// network operation window before config apply gives up.
constexpr std::uint32_t kStopTimeoutMs = 30000U;

bool isBackendFailure(UploadResultClass result) {
    return result != UploadResultClass::kSuccess &&
           result != UploadResultClass::kNoData &&
           result != UploadResultClass::kNoNetwork &&
           result != UploadResultClass::kUnknown;
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

PerBackendCursor UploadManager::pruneCursorsLocked() const {
    PerBackendCursor cursors;
    cursors.reserve(backends_.size());
    for (const auto& backend : backends_) {
        cursors.push_back(BackendPruneCursor{
            backend.snapshot.id,
            backend.snapshot.enabled,
            backend.snapshot.configured,
            backend.uploader != nullptr,
            backend.snapshot.best_effort,
            backend.acknowledged_sample_id,
        });
    }
    return cursors;
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
            // The notification count only wakes the upload loop; queue state is read explicitly.
            static_cast<void>(ulTaskNotifyTake(pdTRUE, kUploadLoopDelay));
            esp_task_wdt_reset();
            continue;
        }

        for (const std::size_t index : due_indices) {
            if (stopRequested()) {
                break;
            }

            const std::uint64_t drain_until_sample_id = measurement_store_->latestSampleId();
            for (;;) {
                const std::uint64_t attempt_now_ms = uptimeMilliseconds();
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
                        attempt_now_ms >= backend.next_action_time_ms) {
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
                    break;
                }

                if (!had_retry_window) {
                    MeasurementQueueWindow window =
                        measurement_store_->uploadWindowAfterUntil(
                            acknowledged_sample_id,
                            drain_until_sample_id,
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
                            break;
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
                std::uint64_t next_action_time_ms = attempt_now_ms + cycle_interval_ms_;
                bool acknowledge_window = false;
                const std::int64_t attempt_started_us = esp_timer_get_time();

                if (upload_samples.empty()) {
                    aggregate_result = UploadResultClass::kNoData;
                    next_state = BackendRuntimeState::kIdle;
                    next_retry_count = 0U;
                } else {
                    lock();
                    last_overall_attempt_uptime_ms_ = attempt_now_ms;
                    last_overall_attempt_unix_ms_ = unix_ms;
                    unlock();

                    std::string network_error;
                    if (!hasNetworkForUpload(network_error)) {
                        aggregate_result = UploadResultClass::kNoNetwork;
                        next_state = BackendRuntimeState::kError;
                        last_error = std::move(network_error);
                        ++next_retry_count;
                    } else {
                        MeasurementBatch batch = buildMeasurementBatch(attempt_now_ms, upload_samples);
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
                                std::uint32_t retry_after_seconds = 0U;

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
                                        retry_after_seconds = response.retry_after_seconds;
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

                                if (retry_after_seconds > 0U) {
                                    next_action_time_ms =
                                        attempt_now_ms +
                                        static_cast<std::uint64_t>(retry_after_seconds) * 1000U;
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
                }
                const bool continue_drain =
                    acknowledge_window &&
                    !upload_sample_ids.empty() &&
                    updated_acknowledged_sample_id < drain_until_sample_id;
                if (continue_drain) {
                    next_action_time_ms = attempt_now_ms;
                }

                std::uint64_t prune_up_to = 0U;
                struct BestEffortMissCandidate {
                    std::uint32_t backend_id = 0U;
                    std::uint64_t after_sample_id = 0U;
                };
                std::vector<BestEffortMissCandidate> best_effort_miss_candidates;
                bool count_current_best_effort_window_as_missed = false;
                lock();
                if (index >= backends_.size()) {
                    unlock();
                    break;
                }

                auto& backend = backends_[index];
                if (backend.uploader.get() != uploader) {
                    unlock();
                    break;
                }

                if (acknowledge_window) {
                    backend.acknowledged_sample_id = updated_acknowledged_sample_id;
                    backend.inflight_last_sample_id = 0U;
                    backend.inflight_sample_ids.clear();
                    backend.inflight_samples.clear();
                }

                const bool backend_failure = isBackendFailure(aggregate_result);
                if (backend_failure) {
                    if (backend.first_failure_uptime_ms == 0U) {
                        backend.first_failure_uptime_ms = attempt_now_ms > 0U ? attempt_now_ms : 1U;
                    }
                    if (shouldDemoteBackendToBestEffort(
                            next_retry_count,
                            backend.first_failure_uptime_ms,
                            attempt_now_ms)) {
                        backend.snapshot.best_effort = true;
                        if (backend.snapshot.best_effort_since_uptime_ms == 0U) {
                            backend.snapshot.best_effort_since_uptime_ms = attempt_now_ms;
                        }
                    }
                } else if (
                    aggregate_result == UploadResultClass::kSuccess ||
                    aggregate_result == UploadResultClass::kNoData) {
                    backend.first_failure_uptime_ms = 0U;
                    backend.snapshot.best_effort = false;
                    backend.snapshot.best_effort_since_uptime_ms = 0U;
                }

                if (!acknowledge_window &&
                    backend.snapshot.best_effort &&
                    backend_failure &&
                    !upload_sample_ids.empty()) {
                    count_current_best_effort_window_as_missed = true;
                    backend.acknowledged_sample_id =
                        std::max(backend.acknowledged_sample_id, inflight_last_sample_id);
                    backend.inflight_last_sample_id = 0U;
                    backend.inflight_sample_ids.clear();
                    backend.inflight_samples.clear();
                }

                backend.snapshot.last_attempt_uptime_ms = attempt_now_ms;
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
                    backend.snapshot.last_success_uptime_ms = attempt_now_ms;
                    backend.snapshot.last_success_unix_ms = unix_ms;
                    backend.snapshot.last_error.clear();
                } else if (aggregate_result == UploadResultClass::kNoData) {
                    backend.snapshot.last_error.clear();
                }

                if (count_current_best_effort_window_as_missed) {
                    const std::uint32_t missed_count =
                        upload_sample_ids.size() > UINT32_MAX - backend.snapshot.missed_sample_count
                            ? UINT32_MAX - backend.snapshot.missed_sample_count
                            : static_cast<std::uint32_t>(upload_sample_ids.size());
                    backend.snapshot.missed_sample_count += missed_count;
                }

                if (acknowledge_window || count_current_best_effort_window_as_missed) {
                    const PruneDecision decision = MeasurementStore::prune(pruneCursorsLocked());
                    prune_up_to = decision.prune_up_to;
                    if (decision.hasQuorum() && prune_up_to > 0U) {
                        for (const auto& candidate : backends_) {
                            if (!candidate.snapshot.enabled ||
                                !candidate.snapshot.configured ||
                                candidate.uploader == nullptr ||
                                !candidate.snapshot.best_effort ||
                                candidate.acknowledged_sample_id >= prune_up_to) {
                                continue;
                            }

                            best_effort_miss_candidates.push_back(BestEffortMissCandidate{
                                candidate.snapshot.id,
                                candidate.acknowledged_sample_id,
                            });
                        }
                    } else {
                        prune_up_to = 0U;
                    }
                }
                unlock();

                if (prune_up_to > 0U) {
                    std::vector<std::pair<std::uint32_t, std::uint32_t>> missed_by_backend;
                    missed_by_backend.reserve(best_effort_miss_candidates.size());
                    for (const auto& candidate : best_effort_miss_candidates) {
                        const std::size_t missed =
                            measurement_store_->queuedCountAfterUntil(
                                candidate.after_sample_id,
                                prune_up_to);
                        if (missed == 0U) {
                            continue;
                        }

                        missed_by_backend.push_back(std::make_pair(
                            candidate.backend_id,
                            missed > UINT32_MAX ? UINT32_MAX
                                                : static_cast<std::uint32_t>(missed)));
                    }

                    measurement_store_->discardUpTo(prune_up_to);

                    if (!missed_by_backend.empty()) {
                        lock();
                        for (const auto& missed : missed_by_backend) {
                            for (auto& candidate : backends_) {
                                if (candidate.snapshot.id != missed.first) {
                                    continue;
                                }

                                candidate.snapshot.missed_sample_count =
                                    missed.second >
                                            UINT32_MAX - candidate.snapshot.missed_sample_count
                                        ? UINT32_MAX
                                        : candidate.snapshot.missed_sample_count + missed.second;
                                candidate.acknowledged_sample_id =
                                    std::max(candidate.acknowledged_sample_id, prune_up_to);
                                break;
                            }
                        }
                        unlock();
                    }
                }

                if (!continue_drain) {
                    break;
                }

                esp_task_wdt_reset();
            }
        }

        // The notification count only wakes the upload loop; queue state is read explicitly.
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
