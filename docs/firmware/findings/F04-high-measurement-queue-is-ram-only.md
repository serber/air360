# Finding F04: Measurement queue is RAM-only

## Status

Confirmed audit finding. Not implemented.

## Scope

Task file for adding durable storage to the firmware measurement upload queue.

## Source of truth in code

- `firmware/main/include/air360/uploads/measurement_store.hpp`
- `firmware/main/src/uploads/measurement_store.cpp`
- `firmware/main/src/sensors/sensor_manager.cpp`

## Read next

- `docs/firmware/measurement-pipeline.md`
- `docs/firmware/upload-transport.md`
- `docs/firmware/adr/proposed-measurement-queue-persistence-adr.md`

**Priority:** High
**Category:** Reliability / Architecture
**Files / symbols:** `MeasurementStore`, `SensorManager::taskMain`, `UploadManager::taskMain`, `firmware/partitions.csv`

## Problem

Unuploaded measurements live only in RAM. `MeasurementStore` uses fixed arrays for queued samples and latest measurements, but it never persists queued upload data to flash.

## Why it matters

Any reset, brownout, watchdog panic, OTA reboot, or power loss drops all queued but unuploaded data. This is a field reliability issue for an air-quality monitor that may run through network outages and later need to backfill telemetry.

## Evidence

- `firmware/main/include/air360/uploads/measurement_store.hpp` stores queued samples in:
  - `std::array<QueuedMeasurementEntry, kMaxQueuedSamples> queued_`
  - `queued_head_`
  - `queued_size_`
  - `next_sample_id_`
- `firmware/main/src/uploads/measurement_store.cpp:80` enqueues samples only into RAM.
- `firmware/main/src/uploads/measurement_store.cpp:206` discards queued samples from RAM after upload quorum.
- `firmware/main/src/sensors/sensor_manager.cpp:440` records measurements directly into `MeasurementStore`.
- `firmware/partitions.csv` includes a `storage` partition, but no code mounts or uses it for measurement queue persistence.

## Recommended Fix

Add a small flash-backed write-ahead queue for upload samples. Keep the current RAM ring as the fast in-memory index, but persist samples before acknowledging them to the queue.

## Where To Change

- `firmware/main/include/air360/uploads/measurement_store.hpp`
- `firmware/main/src/uploads/measurement_store.cpp`
- New storage module, for example `firmware/main/src/uploads/persistent_measurement_log.cpp`
- `firmware/main/CMakeLists.txt`
- `firmware/partitions.csv` if the current SPIFFS partition is not the chosen storage format
- `docs/firmware/measurement-pipeline.md`
- `docs/firmware/PROJECT_STRUCTURE.md`
- `docs/firmware/configuration-reference.md` if storage settings are added

## How To Change

1. Pick a simple persistence format:
   - append-only binary records with CRC and monotonic sample id, or
   - NVS is not recommended for high-frequency samples because of write amplification.
2. Mount LittleFS/SPIFFS or use a raw partition with explicit record framing.
3. On `recordMeasurement()`:
   - validate sample
   - write record to flash
   - then expose it through the RAM queue index
4. On boot:
   - scan records
   - rebuild pending queue up to `kMaxQueuedSamples`
   - drop or quarantine corrupt tail records
5. On prune:
   - compact or advance a durable read cursor only after backend quorum.
6. Track persistence failures in status and logs.

## Example Fix

```cpp
struct PersistedMeasurementRecord {
    uint32_t magic;
    uint16_t version;
    uint16_t length;
    uint64_t sample_id;
    MeasurementSample sample;
    uint32_t crc32;
};
```

## Validation

- Host test with a fake block/file store:
  - append samples
  - simulate power loss mid-record
  - reload and verify no duplicate acknowledged sample and no corrupt tail exposure
- Hardware test:
  - queue samples with network disabled
  - power cycle
  - re-enable network
  - verify queued samples upload
- Brownout/reset test if hardware setup allows it.
- Run `source ~/.espressif/v6.0/esp-idf/export.sh && idf.py build` from `firmware/`.
- Run `python3 scripts/check_firmware_docs.py`.

## Risk Of Change

High. Flash persistence affects wear, boot time, and upload correctness.

## Dependencies

F07 matters if OTA storage layout changes. Do not finalize storage partition use until OTA layout is decided.

## Suggested Agent Type

ESP-IDF agent / C++ refactoring agent / testing agent
