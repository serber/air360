#include "air360/uploads/measurement_store.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace air360 {

namespace {

// The queue is a fixed-size RAM ring buffer, so overflow must drop oldest
// samples instead of allocating; kMaxQueuedSamples is chosen in tuning.hpp.

}  // namespace

MeasurementStore::MeasurementStore() {
    mutex_ = xSemaphoreCreateMutexStatic(&mutex_buffer_);
}

PruneDecision MeasurementStore::prune(const PerBackendCursor& cursors) {
    return computePruneDecision(cursors);
}

std::size_t MeasurementStore::queuedIndex(std::size_t offset) const {
    return (queued_head_ + offset) % queued_.size();
}

void MeasurementStore::dropOldestQueuedLocked() {
    if (queued_size_ == 0U) {
        return;
    }

    QueuedMeasurementEntry& oldest = queued_[queued_head_];
    decrementQueuedCountLocked(oldest.sample.sensor_id);
    oldest = QueuedMeasurementEntry{};
    queued_head_ = (queued_head_ + 1U) % queued_.size();
    --queued_size_;
}

std::uint32_t MeasurementStore::queuedCountForSensorLocked(
    std::uint32_t sensor_id) const {
    for (const auto& entry : queued_count_by_sensor_) {
        if (entry.count > 0U && entry.sensor_id == sensor_id) {
            return entry.count;
        }
    }
    return 0U;
}

void MeasurementStore::incrementQueuedCountLocked(std::uint32_t sensor_id) {
    for (auto& entry : queued_count_by_sensor_) {
        if (entry.count > 0U && entry.sensor_id == sensor_id) {
            ++entry.count;
            return;
        }
    }
    for (auto& entry : queued_count_by_sensor_) {
        if (entry.count == 0U) {
            entry.sensor_id = sensor_id;
            entry.count = 1U;
            return;
        }
    }
    // All slots occupied by other sensors — degrade gracefully and skip
    // tracking this sensor's count. Display will show 0 queued samples
    // for this sensor until queue drains enough to free a slot.
}

void MeasurementStore::decrementQueuedCountLocked(std::uint32_t sensor_id) {
    for (auto& entry : queued_count_by_sensor_) {
        if (entry.count > 0U && entry.sensor_id == sensor_id) {
            if (--entry.count == 0U) {
                entry.sensor_id = 0U;
            }
            return;
        }
    }
}

void MeasurementStore::recordMeasurement(
    std::uint32_t sensor_id,
    SensorType sensor_type,
    const SensorMeasurement& measurement,
    std::int64_t sample_unix_ms) {
    lock();

    LatestMeasurementEntry* latest_entry = nullptr;
    for (std::size_t index = 0U; index < latest_count_; ++index) {
        if (latest_by_sensor_[index].sensor_id == sensor_id) {
            latest_entry = &latest_by_sensor_[index];
            break;
        }
    }

    if (latest_entry == nullptr) {
        if (latest_count_ < latest_by_sensor_.size()) {
            latest_entry = &latest_by_sensor_[latest_count_++];
            latest_entry->sensor_id = sensor_id;
        }
    }

    if (latest_entry != nullptr) {
        latest_entry->measurement = measurement;
        latest_entry->last_sample_time_ms = measurement.sample_time_ms;
    }

    if (sample_unix_ms > 0 && !measurement.empty()) {
        if (queued_size_ == queued_.size()) {
            dropOldestQueuedLocked();
            ++dropped_sample_count_;
        }

        const std::size_t insert_index = queuedIndex(queued_size_);
        queued_[insert_index].id = next_sample_id_++;
        queued_[insert_index].sample = MeasurementSample{
            sensor_id,
            sensor_type,
            static_cast<std::uint64_t>(sample_unix_ms),
            measurement,
        };
        ++queued_size_;
        incrementQueuedCountLocked(sensor_id);
    }

    unlock();
}

void MeasurementStore::append(const MeasurementSample& sample) {
    lock();

    if (queued_size_ == queued_.size()) {
        dropOldestQueuedLocked();
        ++dropped_sample_count_;
    }

    const std::size_t insert_index = queuedIndex(queued_size_);
    queued_[insert_index].id = next_sample_id_++;
    queued_[insert_index].sample = sample;
    ++queued_size_;
    incrementQueuedCountLocked(sample.sensor_id);

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
    const std::size_t reserve_hint = std::min(max_samples, queued_size_);
    window.sample_ids.reserve(reserve_hint);
    window.samples.reserve(reserve_hint);
    for (std::size_t offset = 0U; offset < queued_size_; ++offset) {
        const QueuedMeasurementEntry& entry = queued_[queuedIndex(offset)];
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
    for (std::size_t offset = 0U; offset < queued_size_; ++offset) {
        if (queued_[queuedIndex(offset)].id > after_sample_id) {
            found = true;
            break;
        }
    }
    unlock();
    return found;
}

std::uint64_t MeasurementStore::latestSampleId() const {
    lock();
    std::uint64_t sample_id = 0U;
    if (queued_size_ > 0U) {
        sample_id = queued_[queuedIndex(queued_size_ - 1U)].id;
    }
    unlock();
    return sample_id;
}

std::size_t MeasurementStore::queuedCountAfterUntil(
    std::uint64_t after_sample_id,
    std::uint64_t until_sample_id) const {
    lock();

    std::size_t count = 0U;
    for (std::size_t offset = 0U; offset < queued_size_; ++offset) {
        const QueuedMeasurementEntry& entry = queued_[queuedIndex(offset)];
        if (entry.id <= after_sample_id) {
            continue;
        }
        if (entry.id > until_sample_id) {
            break;
        }
        ++count;
    }

    unlock();
    return count;
}

void MeasurementStore::discardUpTo(std::uint64_t sample_id) {
    lock();

    while (queued_size_ > 0U &&
           queued_[queued_head_].id <= sample_id) {
        dropOldestQueuedLocked();
    }

    unlock();
}

MeasurementRuntimeInfo MeasurementStore::runtimeInfoForSensor(std::uint32_t sensor_id) const {
    lock();

    MeasurementRuntimeInfo info;
    info.sensor_id = sensor_id;
    for (std::size_t index = 0U; index < latest_count_; ++index) {
        if (latest_by_sensor_[index].sensor_id == sensor_id) {
            info.measurement = latest_by_sensor_[index].measurement;
            info.last_sample_time_ms = latest_by_sensor_[index].last_sample_time_ms;
            break;
        }
    }

    info.queued_sample_count = queuedCountForSensorLocked(sensor_id);

    unlock();
    return info;
}

std::size_t MeasurementStore::allLatestMeasurements(
    MeasurementRuntimeInfo* out,
    std::size_t out_cap) const {
    if (out == nullptr || out_cap == 0U) {
        return 0U;
    }

    lock();

    const std::size_t limit = std::min(latest_count_, out_cap);
    for (std::size_t index = 0U; index < limit; ++index) {
        const LatestMeasurementEntry& entry = latest_by_sensor_[index];
        out[index].sensor_id = entry.sensor_id;
        out[index].measurement = entry.measurement;
        out[index].last_sample_time_ms = entry.last_sample_time_ms;
        out[index].queued_sample_count = queuedCountForSensorLocked(entry.sensor_id);
    }

    unlock();
    return limit;
}

MeasurementStoreSnapshot MeasurementStore::snapshot() const {
    lock();

    MeasurementStoreSnapshot snapshot;
    snapshot.measurements.reserve(latest_count_);
    for (std::size_t index = 0U; index < latest_count_; ++index) {
        const LatestMeasurementEntry& entry = latest_by_sensor_[index];
        MeasurementRuntimeInfo info;
        info.sensor_id = entry.sensor_id;
        info.measurement = entry.measurement;
        info.last_sample_time_ms = entry.last_sample_time_ms;
        info.queued_sample_count = queuedCountForSensorLocked(entry.sensor_id);
        snapshot.measurements.push_back(std::move(info));
    }
    snapshot.pending_count = queued_size_;
    snapshot.inflight_count = 0U;
    snapshot.dropped_sample_count = dropped_sample_count_;

    unlock();
    return snapshot;
}

std::size_t MeasurementStore::queuedSampleCountForSensor(std::uint32_t sensor_id) const {
    lock();
    const std::size_t count = queuedCountForSensorLocked(sensor_id);
    unlock();
    return count;
}

std::size_t MeasurementStore::pendingCount() const {
    lock();
    const std::size_t count = queued_size_;
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
