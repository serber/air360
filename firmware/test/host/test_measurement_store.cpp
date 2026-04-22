#include "air360/uploads/measurement_store.hpp"

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

void requireEqual(
    std::uint64_t actual,
    std::uint64_t expected,
    const char* message) {
    if (actual != expected) {
        std::cerr << "FAIL: " << message << "\n"
                  << "  expected: " << expected << "\n"
                  << "  actual:   " << actual << "\n";
        std::exit(1);
    }
}

air360::MeasurementSample makeSample(std::uint32_t sensor_id, std::uint64_t sample_time_ms) {
    air360::SensorMeasurement measurement;
    measurement.sample_time_ms = sample_time_ms;
    const bool added =
        measurement.addValue(air360::SensorValueKind::kTemperatureC, 22.5F);
    require(added, "measurement value can be added");

    return air360::MeasurementSample{
        sensor_id,
        air360::SensorType::kBme280,
        sample_time_ms,
        measurement,
    };
}

void testRecordWithoutUnixTimeDoesNotEnqueue() {
    air360::MeasurementStore store;
    const air360::MeasurementSample sample = makeSample(1U, 1000U);

    requireEqual(store.latestSampleId(), 0U, "empty queue has no latest sample id");
    require(!store.hasSamplesAfter(0U), "empty queue has no samples after any cursor");
    store.recordMeasurement(1U, sample.sensor_type, sample.measurement, 0);

    requireEqual(store.pendingCount(), 0U, "sample without unix time is not queued");
    const air360::MeasurementRuntimeInfo info = store.runtimeInfoForSensor(1U);
    requireEqual(info.last_sample_time_ms, 1000U, "latest measurement still updates");
    requireEqual(info.queued_sample_count, 0U, "sensor queued count remains zero");
}

void testRecordWithUnixTimeSnapshotsLatestAndQueue() {
    air360::MeasurementStore store;
    const air360::MeasurementSample first = makeSample(1U, 1000U);
    const air360::MeasurementSample second = makeSample(2U, 2000U);
    const air360::MeasurementSample replacement = makeSample(1U, 3000U);

    store.recordMeasurement(1U, first.sensor_type, first.measurement, 1700000000000LL);
    store.recordMeasurement(2U, second.sensor_type, second.measurement, 1700000001000LL);
    store.recordMeasurement(1U, replacement.sensor_type, replacement.measurement, 1700000002000LL);

    requireEqual(store.pendingCount(), 3U, "valid unix time enqueues samples");
    requireEqual(store.latestSampleId(), 3U, "latest sample id follows appended records");
    require(store.hasSamplesAfter(2U), "hasSamplesAfter finds newer queued sample");
    require(!store.hasSamplesAfter(3U), "hasSamplesAfter is false at latest cursor");

    air360::MeasurementRuntimeInfo latest[2]{};
    requireEqual(store.allLatestMeasurements(nullptr, 2U), 0U, "null latest buffer is ignored");
    requireEqual(store.allLatestMeasurements(latest, 0U), 0U, "zero latest capacity is ignored");
    requireEqual(store.allLatestMeasurements(latest, 2U), 2U, "all latest entries are copied");
    requireEqual(latest[0].sensor_id, 1U, "first latest entry keeps sensor id");
    requireEqual(latest[0].last_sample_time_ms, 3000U, "existing latest entry is updated");
    requireEqual(latest[0].queued_sample_count, 2U, "latest entry includes queued count");
    requireEqual(latest[1].sensor_id, 2U, "second latest entry is retained");

    const air360::MeasurementStoreSnapshot snapshot = store.snapshot();
    requireEqual(snapshot.pending_count, 3U, "snapshot exposes pending count");
    requireEqual(snapshot.inflight_count, 0U, "snapshot has no store-owned inflight queue");
    requireEqual(snapshot.measurements.size(), 2U, "snapshot includes latest measurements");
}

void testOverflowWindowCountAndDiscard() {
    air360::MeasurementStore store;
    for (std::uint64_t index = 1U; index <= 300U; ++index) {
        store.append(makeSample(7U, index * 1000U));
    }

    requireEqual(store.pendingCount(), 256U, "queue retains bounded capacity");
    requireEqual(store.droppedSampleCount(), 44U, "overflow drops oldest samples");
    requireEqual(store.queuedSampleCountForSensor(7U), 256U, "per-sensor count tracks retained samples");

    const air360::MeasurementQueueWindow first_window = store.uploadWindowAfter(0U, 32U);
    requireEqual(first_window.sample_ids.size(), 32U, "window is bounded by requested size");
    requireEqual(first_window.sample_ids.front(), 45U, "first retained sample id follows overflow");
    requireEqual(first_window.sample_ids.back(), 76U, "window preserves monotonic ids");

    const air360::MeasurementQueueWindow capped_window =
        store.uploadWindowAfterUntil(60U, 65U, 32U);
    requireEqual(capped_window.sample_ids.size(), 5U, "until cursor stops window scan");
    requireEqual(capped_window.sample_ids.front(), 61U, "until window starts after cursor");
    requireEqual(capped_window.sample_ids.back(), 65U, "until window includes upper bound");

    requireEqual(
        store.queuedCountAfterUntil(100U, 200U),
        100U,
        "queuedCountAfterUntil counts retained ids in the half-open cursor interval");

    store.discardUpTo(200U);
    requireEqual(store.pendingCount(), 100U, "discard removes common acknowledged prefix");
    requireEqual(store.queuedSampleCountForSensor(7U), 100U, "per-sensor count follows discard");

    store.discardUpTo(1000U);
    requireEqual(store.pendingCount(), 0U, "discard can empty the queue");
    requireEqual(store.queuedSampleCountForSensor(7U), 0U, "per-sensor count clears at zero");
}

void testMeasurementStorePruneWrapper() {
    const air360::PruneDecision decision = air360::MeasurementStore::prune({
        air360::BackendPruneCursor{1U, true, true, true, false, 30U},
        air360::BackendPruneCursor{2U, true, true, true, false, 18U},
        air360::BackendPruneCursor{3U, false, true, true, false, 0U},
    });

    require(decision.hasQuorum(), "enabled backends form a prune quorum");
    requireEqual(decision.prune_up_to, 18U, "prune wrapper returns minimum quorum cursor");
}

}  // namespace

int main() {
    testRecordWithoutUnixTimeDoesNotEnqueue();
    testRecordWithUnixTimeSnapshotsLatestAndQueue();
    testOverflowWindowCountAndDiscard();
    testMeasurementStorePruneWrapper();
    std::cout << "measurement_store tests passed\n";
    return 0;
}
