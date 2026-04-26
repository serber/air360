#include "air360/uploads/upload_manager.hpp"

#include <algorithm>

namespace air360 {

namespace {

bool isDegraded(const BackendStatusSnapshot& backend) {
    return backend.enabled && backend.state == BackendRuntimeState::kError;
}

}  // namespace

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

}  // namespace air360
