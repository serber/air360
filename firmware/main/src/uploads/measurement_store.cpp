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

std::vector<MeasurementSample> MeasurementStore::beginUploadWindow(std::uint64_t cutoff_time_ms) {
    ensureMutex();
    lock();

    if (!inflight_.empty()) {
        const auto snapshot = inflight_;
        unlock();
        return snapshot;
    }

    std::size_t prefix_count = 0U;
    while (prefix_count < pending_.size() && pending_[prefix_count].sample_time_ms <= cutoff_time_ms) {
        ++prefix_count;
    }

    if (prefix_count > 0U) {
        inflight_.assign(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(prefix_count));
        pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(prefix_count));
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
