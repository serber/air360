# Measurement Queue SPIFFS Persistence ADR

## Status

Proposed.

## Decision Summary

Persist `MeasurementStore::pending_` to SPIFFS so that queued samples survive device reboot or power loss and are uploaded after the device comes back online.

## Context

The current measurement pipeline provides at-least-once delivery within a single uptime session: the `inflight_` / `restoreInflight()` cycle ensures that a sample is retried until it is acknowledged. However, any samples in `pending_` or `inflight_` at the moment of power loss or reboot are silently discarded. The device starts the next session with an empty queue.

For deployments with intermittent network access or unreliable power, this means measurement gaps whenever the device reboots. The `storage` SPIFFS partition (384 KB at `0x1a0000`) is already allocated in `partitions.csv` and is currently unused.

Rough capacity: 256 samples × ~100 bytes each ≈ 25 KB. The SPIFFS partition can hold the full queue with significant margin.

This ADR is intentionally narrower than the broader “local history and diagnostics” problem. User-visible history endpoints and queue visibility are captured separately in [proposed-local-history-and-diagnostics-adr.md](proposed-local-history-and-diagnostics-adr.md).

## Goals

- Survive device reboot without losing queued measurements.
- Restore the pending queue on next boot before the upload task starts.
- Not block the boot sequence on SPIFFS I/O failures.

## Non-Goals

- Persisting `latest_by_sensor_` (display-only, no upload value).
- Persisting `inflight_` (in-flight samples on crash are restored to `pending_` on load, which covers the same case).
- Real-time write-through on every `recordMeasurement()` call (too frequent; use periodic flush instead).

## Architectural Decision

### Storage format

A single flat binary file `/spiffs/queue.bin` containing a header followed by a sequence of serialized `MeasurementSample` structs.

Header (fixed size):
- Magic number (4 bytes): `0x41513032` ("AQ02")
- Version (1 byte)
- Sample count (2 bytes)
- CRC32 of payload (4 bytes)

Each `MeasurementSample` is serialized as a fixed-size binary record (sensor_id, sensor_type, sample_time_ms, value_count, values array). Total per-sample size: ~96 bytes.

### Write strategy: periodic flush

`MeasurementStore` gains a `flush()` method that serializes the current `pending_` to `/spiffs/queue.bin` under the existing mutex. The flush is called:

1. From the **upload task** after each successful `acknowledgeInflight()` — queue shrunk, persist the smaller queue.
2. From the **sensor task** every N new samples (e.g., every 10 samples) — bound the maximum data loss window.

This avoids write-through on every 250 ms poll while limiting data loss to at most N samples on sudden power cut.

### Load strategy: restore on boot

`App::run()` calls `MeasurementStore::loadFromSpiffs()` between step 5 (sensor config load) and `SensorManager::applyConfig()`. The load:

1. Opens `/spiffs/queue.bin`.
2. Validates magic, version, CRC.
3. Deserializes samples and appends to `pending_` via the existing `append()` method.
4. Deletes the file on successful load (next flush will create a fresh one).

If the file is absent, corrupt, or the SPIFFS mount fails: log a warning and continue with an empty queue. The boot sequence is not affected.

### SPIFFS initialization

SPIFFS must be initialized before `loadFromSpiffs()`. Add `esp_vfs_spiffs_register()` to `App::run()` as a new optional step between step 2 (NVS init) and step 4 (device config load). Failure is non-fatal.

## Affected Files

- `firmware/main/src/uploads/measurement_store.cpp` — add `flush()`, `loadFromSpiffs()`, serialization helpers
- `firmware/main/include/air360/uploads/measurement_store.hpp` — add method declarations
- `firmware/main/src/app.cpp` — add SPIFFS init step, call `loadFromSpiffs()` at step 5, call `flush()` from upload task path
- `firmware/main/src/uploads/upload_manager.cpp` — call `measurement_store_->flush()` after `acknowledgeInflight()`
- `firmware/main/src/sensors/sensor_manager.cpp` — call `measurement_store_->flush()` every N recordings
- `firmware/main/CMakeLists.txt` — add `spiffs` to REQUIRES

## Alternatives Considered

### Option A. No persistence (current state)

Simple. Loses all pending data on reboot. Acceptable only for always-on deployments with stable power.

### Option B. Write-through on every recordMeasurement

Maximum durability. SPIFFS write on every 250 ms poll cycle is too aggressive — SPIFFS has limited write endurance and the operation takes ~10 ms, which would affect sensor polling timing.

### Option C. Periodic flush (accepted)

Good balance: bounded data loss (at most N samples), low write frequency, no impact on sensor polling timing, SPIFFS endurance remains healthy.

## Reference Links

- [Sensor.Community ecosystem review for Air360](../../ecosystem/sensor-community-issues-prs-review-2026-04-14.md)
- [Measurement Pipeline](../measurement-pipeline.md)
- [Project Structure](../PROJECT_STRUCTURE.md)
- [Proposed local history and diagnostics ADR](proposed-local-history-and-diagnostics-adr.md)
- [#112 Improve in case of WiFi down - cache some of the values](https://github.com/opendata-stuttgart/sensors-software/issues/112)
- [#940 Store the last 40 measurements in JSON file](https://github.com/opendata-stuttgart/sensors-software/issues/940)

## Practical Conclusion

Add periodic SPIFFS flush of `pending_` and restore on boot. The maximum data loss window is bounded by the flush interval. Boot sequence gains one optional SPIFFS init step that is non-fatal on failure.
