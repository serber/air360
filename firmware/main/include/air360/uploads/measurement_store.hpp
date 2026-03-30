#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "air360/uploads/measurement_batch.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace air360 {

class MeasurementStore {
  public:
    void append(const MeasurementSample& sample);
    std::vector<MeasurementSample> beginUploadWindow(std::uint64_t cutoff_time_ms);
    void acknowledgeInflight();
    void restoreInflight();

    std::size_t pendingCount() const;
    std::size_t inflightCount() const;
    std::uint32_t droppedSampleCount() const;

  private:
    void ensureMutex() const;
    void lock() const;
    void unlock() const;

    mutable StaticSemaphore_t mutex_buffer_{};
    mutable SemaphoreHandle_t mutex_ = nullptr;
    std::vector<MeasurementSample> pending_;
    std::vector<MeasurementSample> inflight_;
    std::uint32_t dropped_sample_count_ = 0U;
};

}  // namespace air360
