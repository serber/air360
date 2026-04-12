#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "air360/uploads/measurement_batch.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace air360 {

struct MeasurementRuntimeInfo {
    std::uint32_t sensor_id = 0U;
    SensorMeasurement measurement{};
    std::uint64_t last_sample_time_ms = 0U;
    std::size_t queued_sample_count = 0U;
};

class MeasurementStore {
  public:
    void recordMeasurement(
        std::uint32_t sensor_id,
        SensorType sensor_type,
        const SensorMeasurement& measurement,
        std::int64_t sample_unix_ms);
    void append(const MeasurementSample& sample);
    std::vector<MeasurementSample> beginUploadWindow(std::size_t max_samples = SIZE_MAX);
    void acknowledgeInflight();
    void restoreInflight();

    MeasurementRuntimeInfo runtimeInfoForSensor(std::uint32_t sensor_id) const;
    std::size_t queuedSampleCountForSensor(std::uint32_t sensor_id) const;
    std::size_t pendingCount() const;
    std::size_t inflightCount() const;
    std::uint32_t droppedSampleCount() const;

  private:
    void ensureMutex() const;
    void lock() const;
    void unlock() const;

    struct LatestMeasurementEntry {
        std::uint32_t sensor_id = 0U;
        SensorMeasurement measurement{};
        std::uint64_t last_sample_time_ms = 0U;
    };

    mutable StaticSemaphore_t mutex_buffer_{};
    mutable SemaphoreHandle_t mutex_ = nullptr;
    std::vector<LatestMeasurementEntry> latest_by_sensor_;
    std::vector<MeasurementSample> pending_;
    std::vector<MeasurementSample> inflight_;
    std::unordered_map<std::uint32_t, std::uint32_t> queued_count_by_sensor_;
    std::uint32_t dropped_sample_count_ = 0U;
};

}  // namespace air360
