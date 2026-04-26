#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "air360/sensors/sensor_types.hpp"
#include "air360/tuning.hpp"
#include "air360/uploads/measurement_batch.hpp"
#include "air360/uploads/upload_prune_policy.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace air360 {

constexpr std::size_t kMaxQueuedSamples = tuning::upload::kMeasurementQueueDepth;

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

    static PruneDecision prune(const PerBackendCursor& cursors);

    void recordMeasurement(
        std::uint32_t sensor_id,
        SensorType sensor_type,
        const SensorMeasurement& measurement,
        std::int64_t sample_unix_ms);
    void append(const MeasurementSample& sample);
    MeasurementQueueWindow uploadWindowAfter(
        std::uint64_t after_sample_id,
        std::size_t max_samples = SIZE_MAX) const;
    MeasurementQueueWindow uploadWindowAfterUntil(
        std::uint64_t after_sample_id,
        std::uint64_t until_sample_id,
        std::size_t max_samples = SIZE_MAX) const;
    bool hasSamplesAfter(std::uint64_t after_sample_id) const;
    std::uint64_t latestSampleId() const;
    std::size_t queuedCountAfterUntil(
        std::uint64_t after_sample_id,
        std::uint64_t until_sample_id) const;
    void discardUpTo(std::uint64_t sample_id);

    MeasurementRuntimeInfo runtimeInfoForSensor(std::uint32_t sensor_id) const;
    std::size_t allLatestMeasurements(
        MeasurementRuntimeInfo* out,
        std::size_t out_cap) const;
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

    struct PerSensorCount {
        std::uint32_t sensor_id = 0U;
        std::uint32_t count = 0U;
    };

    struct QueuedMeasurementEntry {
        std::uint64_t id = 0U;
        MeasurementSample sample{};
    };

    std::size_t queuedIndex(std::size_t offset) const;
    void dropOldestQueuedLocked();
    std::uint32_t queuedCountForSensorLocked(std::uint32_t sensor_id) const;
    void incrementQueuedCountLocked(std::uint32_t sensor_id);
    void decrementQueuedCountLocked(std::uint32_t sensor_id);

    mutable StaticSemaphore_t mutex_buffer_{};
    mutable SemaphoreHandle_t mutex_ = nullptr;
    std::array<LatestMeasurementEntry, kMaxConfiguredSensors> latest_by_sensor_{};
    std::size_t latest_count_ = 0U;
    std::array<PerSensorCount, kMaxConfiguredSensors> queued_count_by_sensor_{};
    std::array<QueuedMeasurementEntry, kMaxQueuedSamples> queued_{};
    std::size_t queued_head_ = 0U;
    std::size_t queued_size_ = 0U;
    std::uint64_t next_sample_id_ = 1U;
    std::uint32_t dropped_sample_count_ = 0U;
};

}  // namespace air360
