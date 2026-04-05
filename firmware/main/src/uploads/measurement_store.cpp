#include "air360/uploads/measurement_store.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace air360 {

namespace {

constexpr std::size_t kMaxQueuedSamples = 256U;

}  // namespace

void MeasurementStore::append(const MeasurementSample& sample) {
    ensureMutex();
    lock();

    pending_.push_back(sample);
    if (pending_.size() > kMaxQueuedSamples) {
        const std::size_t overflow = pending_.size() - kMaxQueuedSamples;
        pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(overflow));
        dropped_sample_count_ += static_cast<std::uint32_t>(overflow);
    }

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
