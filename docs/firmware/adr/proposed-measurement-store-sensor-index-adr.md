# MeasurementStore Per-Sensor Index ADR

## Status

Proposed.

## Decision Summary

Replace O(n) linear scans over `pending_` and `inflight_` with an O(1) per-sensor counter map in `MeasurementStore`.

## Context

`runtimeInfoForSensor()` and `queuedSampleCountForSensor()` iterate the full `pending_` and `inflight_` queues on every call:

```cpp
for (const auto& sample : pending_) {
    if (sample.sensor_id == sensor_id) ++info.queued_sample_count;
}
for (const auto& sample : inflight_) {
    if (sample.sensor_id == sensor_id) ++info.queued_sample_count;
}
```

These methods are called from the web server task during every `/` and `/status` response — once per configured sensor. With 256 pending samples and 6 sensors, each status page render performs ~1 536 comparisons while holding the store mutex, blocking both the sensor task and the upload task for the entire duration.

## Goals

- Make per-sensor count queries O(1).
- Reduce mutex hold time during status page rendering.
- Keep all existing public API unchanged.

## Non-Goals

- Changing the vector-based `pending_` / `inflight_` storage (separate concern).
- Per-sensor access to individual sample values from the index.

## Architectural Decision

Add a single `std::unordered_map<uint32_t, uint32_t> queued_count_by_sensor_` to `MeasurementStore`. Update it in every method that modifies `pending_` or `inflight_`:

| Method | Action |
|--------|--------|
| `recordMeasurement()` | `++queued_count_by_sensor_[sensor_id]` |
| `beginUploadWindow()` | decrement counts for moved samples |
| `acknowledgeInflight()` | decrement counts for cleared samples |
| `restoreInflight()` | re-increment counts for re-prepended samples |

`queuedSampleCountForSensor()` becomes a single map lookup. `runtimeInfoForSensor()` reads the count from the map instead of scanning.

The map holds counts for `pending_` + `inflight_` combined (same as the current scan). A sensor is removed from the map when its count reaches zero.

## Affected Files

- `firmware/main/src/uploads/measurement_store.cpp` — add map maintenance to all modifying methods, simplify read methods
- `firmware/main/include/air360/uploads/measurement_store.hpp` — add `queued_count_by_sensor_` field

## Alternatives Considered

### Option A. Keep linear scan

Acceptable at low sensor count and slow status refresh. Degrades silently as sensor count or queue depth grows.

### Option B. Per-sensor sub-queues

Replace the shared `pending_` with `unordered_map<uint32_t, vector<MeasurementSample>>`. Solves the count problem but complicates `beginUploadWindow()` (must interleave samples from multiple sub-queues in arrival order).

### Option C. Parallel counter map (accepted)

Minimal change, zero impact on existing queue semantics. The map is always consistent with the queues as long as every code path that modifies `pending_` or `inflight_` updates the map under the same mutex.

## Practical Conclusion

Add `queued_count_by_sensor_` maintained incrementally alongside the existing queues. Query methods become O(1). Mutex hold time during status rendering drops from O(n_samples) to O(n_sensors).
