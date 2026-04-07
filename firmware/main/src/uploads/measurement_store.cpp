#include "air360/uploads/measurement_store.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace air360 {

namespace {

constexpr std::size_t kMaxQueuedSamples = 256U;

void appendPendingLocked(
    std::vector<MeasurementSample>& pending,
    const MeasurementSample& sample,
    std::uint32_t& dropped_sample_count) {
    pending.push_back(sample);
    if (pending.size() > kMaxQueuedSamples) {
        const std::size_t overflow = pending.size() - kMaxQueuedSamples;
        pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(overflow));
        dropped_sample_count += static_cast<std::uint32_t>(overflow);
    }
}

}  // namespace

void MeasurementStore::recordMeasurement(
    std::uint32_t sensor_id,
    SensorType sensor_type,
    const SensorMeasurement& measurement,
    std::int64_t sample_unix_ms) {
    ensureMutex();
    lock();

    LatestMeasurementEntry* latest_entry = nullptr;
    for (auto& entry : latest_by_sensor_) {
        if (entry.sensor_id == sensor_id) {
            latest_entry = &entry;
            break;
        }
    }

    if (latest_entry == nullptr) {
        latest_by_sensor_.push_back(LatestMeasurementEntry{sensor_id, measurement, measurement.sample_time_ms});
    } else {
        latest_entry->measurement = measurement;
        latest_entry->last_sample_time_ms = measurement.sample_time_ms;
    }

    if (sample_unix_ms > 0 && !measurement.empty()) {
        appendPendingLocked(
            pending_,
            MeasurementSample{
                sensor_id,
                sensor_type,
                static_cast<std::uint64_t>(sample_unix_ms),
                measurement,
            },
            dropped_sample_count_);
    }

    unlock();
}

void MeasurementStore::append(const MeasurementSample& sample) {
    ensureMutex();
    lock();

    appendPendingLocked(pending_, sample, dropped_sample_count_);

    unlock();
}

std::vector<MeasurementSample> MeasurementStore::beginUploadWindow(std::size_t max_samples) {
    ensureMutex();
    lock();

    if (!inflight_.empty()) {
        const auto snapshot = inflight_;
        unlock();
        return snapshot;
    }

    if (!pending_.empty()) {
        const std::size_t batch_size = std::min(max_samples, pending_.size());
        inflight_.assign(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(batch_size));
        pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(batch_size));
    }

    const auto snapshot = inflight_;
    unlock();
    return snapshot;
}

void MeasurementStore::acknowledgeInflight() {
    ensureMutex();
    lock();
    inflight_.clear();
    unlock();
}

void MeasurementStore::restoreInflight() {
    ensureMutex();
    lock();
    if (!inflight_.empty()) {
        pending_.insert(pending_.begin(), inflight_.begin(), inflight_.end());
        inflight_.clear();
        if (pending_.size() > kMaxQueuedSamples) {
            const std::size_t overflow = pending_.size() - kMaxQueuedSamples;
            pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(overflow));
            dropped_sample_count_ += static_cast<std::uint32_t>(overflow);
        }
    }
    unlock();
}

MeasurementRuntimeInfo MeasurementStore::runtimeInfoForSensor(std::uint32_t sensor_id) const {
    ensureMutex();
    lock();

    MeasurementRuntimeInfo info;
    info.sensor_id = sensor_id;
    for (const auto& entry : latest_by_sensor_) {
        if (entry.sensor_id == sensor_id) {
            info.measurement = entry.measurement;
            info.last_sample_time_ms = entry.last_sample_time_ms;
            break;
        }
    }

    for (const auto& sample : pending_) {
        if (sample.sensor_id == sensor_id) {
            ++info.queued_sample_count;
        }
    }
    for (const auto& sample : inflight_) {
        if (sample.sensor_id == sensor_id) {
            ++info.queued_sample_count;
        }
    }

    unlock();
    return info;
}

std::size_t MeasurementStore::queuedSampleCountForSensor(std::uint32_t sensor_id) const {
    ensureMutex();
    lock();

    std::size_t count = 0U;
    for (const auto& sample : pending_) {
        if (sample.sensor_id == sensor_id) {
            ++count;
        }
    }
    for (const auto& sample : inflight_) {
        if (sample.sensor_id == sensor_id) {
            ++count;
        }
    }

    unlock();
    return count;
}

std::size_t MeasurementStore::pendingCount() const {
    ensureMutex();
    lock();
    const std::size_t count = pending_.size();
    unlock();
    return count;
}

std::size_t MeasurementStore::inflightCount() const {
    ensureMutex();
    lock();
    const std::size_t count = inflight_.size();
    unlock();
    return count;
}

std::uint32_t MeasurementStore::droppedSampleCount() const {
    ensureMutex();
    lock();
    const std::uint32_t count = dropped_sample_count_;
    unlock();
    return count;
}

void MeasurementStore::ensureMutex() const {
    if (mutex_ == nullptr) {
        mutex_ = xSemaphoreCreateMutexStatic(&mutex_buffer_);
    }
}

void MeasurementStore::lock() const {
    xSemaphoreTake(mutex_, portMAX_DELAY);
}

void MeasurementStore::unlock() const {
    xSemaphoreGive(mutex_);
}

}  // namespace air360
