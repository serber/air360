#include "air360/uploads/measurement_store.hpp"

#include <algorithm>
#include <cstddef>
#include <unordered_map>
#include <utility>

namespace air360 {

namespace {

constexpr std::size_t kMaxQueuedSamples = 256U;

void removeQueuedSampleLocked(
    const MeasurementSample& sample,
    std::unordered_map<std::uint32_t, std::uint32_t>& count_map) {
    auto it = count_map.find(sample.sensor_id);
    if (it != count_map.end()) {
        if (--it->second == 0U) {
            count_map.erase(it);
        }
    }
}

}  // namespace

MeasurementStore::MeasurementStore() {
    mutex_ = xSemaphoreCreateMutexStatic(&mutex_buffer_);
}

void MeasurementStore::recordMeasurement(
    std::uint32_t sensor_id,
    SensorType sensor_type,
    const SensorMeasurement& measurement,
    std::int64_t sample_unix_ms) {
    lock();

    LatestMeasurementEntry* latest_entry = nullptr;
    for (auto& entry : latest_by_sensor_) {
        if (entry.sensor_id == sensor_id) {
            latest_entry = &entry;
            break;
        }
    }

    if (latest_entry == nullptr) {
        latest_by_sensor_.push_back(
            LatestMeasurementEntry{sensor_id, measurement, measurement.sample_time_ms});
    } else {
        latest_entry->measurement = measurement;
        latest_entry->last_sample_time_ms = measurement.sample_time_ms;
    }

    if (sample_unix_ms > 0 && !measurement.empty()) {
        const MeasurementSample sample{
            sensor_id,
            sensor_type,
            static_cast<std::uint64_t>(sample_unix_ms),
            measurement,
        };
        queued_.push_back(QueuedMeasurementEntry{
            next_sample_id_++,
            sample,
        });
        ++queued_count_by_sensor_[sample.sensor_id];

        if (queued_.size() > kMaxQueuedSamples) {
            const std::size_t overflow = queued_.size() - kMaxQueuedSamples;
            for (std::size_t i = 0U; i < overflow; ++i) {
                removeQueuedSampleLocked(queued_[i].sample, queued_count_by_sensor_);
            }
            queued_.erase(
                queued_.begin(),
                queued_.begin() + static_cast<std::ptrdiff_t>(overflow));
            dropped_sample_count_ += static_cast<std::uint32_t>(overflow);
        }
    }

    unlock();
}

void MeasurementStore::append(const MeasurementSample& sample) {
    lock();

    queued_.push_back(QueuedMeasurementEntry{
        next_sample_id_++,
        sample,
    });
    ++queued_count_by_sensor_[sample.sensor_id];

    if (queued_.size() > kMaxQueuedSamples) {
        const std::size_t overflow = queued_.size() - kMaxQueuedSamples;
        for (std::size_t i = 0U; i < overflow; ++i) {
            removeQueuedSampleLocked(queued_[i].sample, queued_count_by_sensor_);
        }
        queued_.erase(
            queued_.begin(),
            queued_.begin() + static_cast<std::ptrdiff_t>(overflow));
        dropped_sample_count_ += static_cast<std::uint32_t>(overflow);
    }

    unlock();
}

MeasurementQueueWindow MeasurementStore::uploadWindowAfter(
    std::uint64_t after_sample_id,
    std::size_t max_samples) const {
    return uploadWindowAfterUntil(after_sample_id, UINT64_MAX, max_samples);
}

MeasurementQueueWindow MeasurementStore::uploadWindowAfterUntil(
    std::uint64_t after_sample_id,
    std::uint64_t until_sample_id,
    std::size_t max_samples) const {
    lock();

    MeasurementQueueWindow window;
    window.sample_ids.reserve(std::min(max_samples, queued_.size()));
    window.samples.reserve(std::min(max_samples, queued_.size()));
    for (const auto& entry : queued_) {
        if (entry.id <= after_sample_id) {
            continue;
        }
        if (entry.id > until_sample_id) {
            break;
        }

        window.sample_ids.push_back(entry.id);
        window.samples.push_back(entry.sample);
        if (window.samples.size() >= max_samples) {
            break;
        }
    }

    unlock();
    return window;
}

bool MeasurementStore::hasSamplesAfter(std::uint64_t after_sample_id) const {
    lock();
    bool found = false;
    for (const auto& entry : queued_) {
        if (entry.id > after_sample_id) {
            found = true;
            break;
        }
    }
    unlock();
    return found;
}

std::uint64_t MeasurementStore::latestSampleId() const {
    lock();
    const std::uint64_t sample_id = queued_.empty() ? 0U : queued_.back().id;
    unlock();
    return sample_id;
}

void MeasurementStore::discardUpTo(std::uint64_t sample_id) {
    lock();

    std::size_t remove_count = 0U;
    while (remove_count < queued_.size() && queued_[remove_count].id <= sample_id) {
        removeQueuedSampleLocked(queued_[remove_count].sample, queued_count_by_sensor_);
        ++remove_count;
    }

    if (remove_count > 0U) {
        queued_.erase(
            queued_.begin(),
            queued_.begin() + static_cast<std::ptrdiff_t>(remove_count));
    }

    unlock();
}

MeasurementRuntimeInfo MeasurementStore::runtimeInfoForSensor(std::uint32_t sensor_id) const {
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

    const auto it = queued_count_by_sensor_.find(sensor_id);
    if (it != queued_count_by_sensor_.end()) {
        info.queued_sample_count = it->second;
    }

    unlock();
    return info;
}

std::vector<MeasurementRuntimeInfo> MeasurementStore::allLatestMeasurements() const {
    lock();

    std::vector<MeasurementRuntimeInfo> result;
    result.reserve(latest_by_sensor_.size());
    for (const auto& entry : latest_by_sensor_) {
        MeasurementRuntimeInfo info;
        info.sensor_id = entry.sensor_id;
        info.measurement = entry.measurement;
        info.last_sample_time_ms = entry.last_sample_time_ms;
        const auto queued = queued_count_by_sensor_.find(entry.sensor_id);
        info.queued_sample_count =
            queued != queued_count_by_sensor_.end() ? queued->second : 0U;
        result.push_back(info);
    }

    unlock();
    return result;
}

MeasurementStoreSnapshot MeasurementStore::snapshot() const {
    lock();

    MeasurementStoreSnapshot snapshot;
    snapshot.measurements.reserve(latest_by_sensor_.size());
    for (const auto& entry : latest_by_sensor_) {
        MeasurementRuntimeInfo info;
        info.sensor_id = entry.sensor_id;
        info.measurement = entry.measurement;
        info.last_sample_time_ms = entry.last_sample_time_ms;
        const auto queued = queued_count_by_sensor_.find(entry.sensor_id);
        info.queued_sample_count =
            queued != queued_count_by_sensor_.end() ? queued->second : 0U;
        snapshot.measurements.push_back(std::move(info));
    }
    snapshot.pending_count = queued_.size();
    snapshot.inflight_count = 0U;
    snapshot.dropped_sample_count = dropped_sample_count_;

    unlock();
    return snapshot;
}

std::size_t MeasurementStore::queuedSampleCountForSensor(std::uint32_t sensor_id) const {
    lock();

    const auto it = queued_count_by_sensor_.find(sensor_id);
    const std::size_t count = (it != queued_count_by_sensor_.end()) ? it->second : 0U;

    unlock();
    return count;
}

std::size_t MeasurementStore::pendingCount() const {
    lock();
    const std::size_t count = queued_.size();
    unlock();
    return count;
}

std::uint32_t MeasurementStore::droppedSampleCount() const {
    lock();
    const std::uint32_t count = dropped_sample_count_;
    unlock();
    return count;
}

void MeasurementStore::lock() const {
    xSemaphoreTake(mutex_, portMAX_DELAY);
}

void MeasurementStore::unlock() const {
    xSemaphoreGive(mutex_);
}

}  // namespace air360
