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

struct MeasurementQueueWindow {
    std::vector<std::uint64_t> sample_ids;
    std::vector<MeasurementSample> samples;

    bool empty() const {
        return samples.empty();
    }
};

struct MeasurementStoreSnapshot {
    std::vector<MeasurementRuntimeInfo> measurements;
    std::size_t pending_count = 0U;
    std::size_t inflight_count = 0U;
    std::uint32_t dropped_sample_count = 0U;
};

class MeasurementStore {
  public:
    MeasurementStore();
    MeasurementStore(const MeasurementStore&) = delete;
    MeasurementStore& operator=(const MeasurementStore&) = delete;
    MeasurementStore(MeasurementStore&&) = delete;
    MeasurementStore& operator=(MeasurementStore&&) = delete;

    void recordMeasurement(
        std::uint32_t sensor_id,
        SensorType sensor_type,
        const SensorMeasurement& measurement,
        std::int64_t sample_unix_ms);
    void append(const MeasurementSample& sample);
    MeasurementQueueWindow uploadWindowAfter(
        std::uint64_t after_sample_id,
        std::size_t max_samples = SIZE_MAX) const;
    bool hasSamplesAfter(std::uint64_t after_sample_id) const;
    void discardUpTo(std::uint64_t sample_id);

    MeasurementRuntimeInfo runtimeInfoForSensor(std::uint32_t sensor_id) const;
    std::vector<MeasurementRuntimeInfo> allLatestMeasurements() const;
    MeasurementStoreSnapshot snapshot() const;
    std::size_t queuedSampleCountForSensor(std::uint32_t sensor_id) const;
    std::size_t pendingCount() const;
    std::uint32_t droppedSampleCount() const;

  private:
    void lock() const;
    void unlock() const;

    struct LatestMeasurementEntry {
        std::uint32_t sensor_id = 0U;
        SensorMeasurement measurement{};
        std::uint64_t last_sample_time_ms = 0U;
    };

    struct QueuedMeasurementEntry {
        std::uint64_t id = 0U;
        MeasurementSample sample{};
    };

    mutable StaticSemaphore_t mutex_buffer_{};
    mutable SemaphoreHandle_t mutex_ = nullptr;
    std::vector<LatestMeasurementEntry> latest_by_sensor_;
    std::vector<QueuedMeasurementEntry> queued_;
    std::unordered_map<std::uint32_t, std::uint32_t> queued_count_by_sensor_;
    std::uint64_t next_sample_id_ = 1U;
    std::uint32_t dropped_sample_count_ = 0U;
};

}  // namespace air360
