# Measurement Pipeline

## Status

Implemented. Keep this document aligned with the current runtime queueing and upload flow.

## Scope

This document covers how sensor readings become queued measurements and how those queued samples are scheduled, batched, uploaded, acknowledged, or restored.

## Source of truth in code

- `firmware/main/src/sensors/sensor_manager.cpp`
- `firmware/main/src/uploads/upload_manager.cpp`
- `firmware/main/src/uploads/measurement_store.cpp`
- `firmware/main/src/uploads/backend_registry.cpp`

## Read next

- [upload-adapters.md](upload-adapters.md)
- [upload-transport.md](upload-transport.md)
- [time.md](time.md)

This document describes how sensor readings move from hardware acquisition through in-memory queuing to upload — covering the data structures, queue mechanics, upload window, and all the conditions that affect the flow.

---

## Pipeline overview

```
air360_sensor task (250 ms loop)
  │
  ├─ driver->init()    on first action for each sensor
  ├─ driver->poll()    on subsequent actions
  │
  └─ MeasurementStore::recordMeasurement()
       │
       ├─ latest_by_sensor_   ← always updated (for web UI / diagnostics raw dump)
       │
       └─ queued_             ← appended only when unix_ms > 0, one shared copy per sample
            │
            └─ air360_upload task (1 s loop)
                 │
                 ├─ per backend: select window after acknowledged_sample_id
                 ├─ buildMeasurementBatch()
                 ├─ uploader->deliver(batch, context)
                 │    └─ backend owns protocol-specific delivery
                 │
                 ├─ success/no_data → advance only that backend cursor
                 └─ failure         → keep only that backend window for retry
```

---

## Stage 1 — Sensor polling (`air360_sensor` task)

The sensor task runs every **250 ms**. On each iteration it walks the list of managed sensors and checks each one against its scheduled `next_action_time_ms`.

For each sensor due for action:

| Condition | Action |
|-----------|--------|
| `driver_ready == false` | Call `driver->init(record, context)` |
| `driver_ready == true` | Call `driver->poll()` |

Drivers access hardware exclusively through `SensorDriverContext`, which carries `I2cBusManager` and `UartPortManager` references. See [transport-binding.md](transport-binding.md) for bus initialisation, transfer API, and per-transport constants.

### After `init()`

- On success: `driver_ready = true`, state → `kInitialized`, `next_action_time_ms = now` (poll immediately on the next loop iteration)
- On failure: `driver_ready = false`, state → `kAbsent` or `kError`, `failures++`, and the next init attempt is delayed with exponential backoff: 1 s, 2 s, 4 s, up to a 5 min cap. `next_retry_ms` exposes the scheduled uptime.

### After `poll()`

- On success: state → `kPolling`, `failures = 0`, `next_retry_ms = 0`, `next_action_time_ms = now + poll_interval_ms`
- On the first two consecutive poll failures: keep `driver_ready = true` and retry polling after `min(poll_interval_ms, 5000)`. This absorbs short bus glitches without a full driver teardown.
- On the third consecutive poll failure: set `driver_ready = false`, increment `failures`, and enter the same exponential init backoff used by `init()` failures.

After 16 consecutive init/poll failures, the manager marks the sensor `kFailed`, clears `next_retry_ms`, and stops automatic retry attempts. A config reload or manual re-enable rebuilds the runtime entry and permits a new attempt.

### What a driver returns

Each driver stores a `SensorMeasurement` internally and returns it via `latestMeasurement()`:

```cpp
struct SensorMeasurement {
    uint64_t sample_time_ms;           // uptime-based timestamp (esp_timer_get_time / 1000)
    uint8_t  value_count;              // number of values in this measurement
    SensorValue values[16];            // up to 16 values
};

struct SensorValue {
    SensorValueKind kind;   // e.g. kTemperatureC, kHumidityPercent
    float value;
};
```

Maximum values per measurement: **16** (`kMaxMeasurementValues`).

---

## Stage 2 — Recording into `MeasurementStore`

After a successful poll, the sensor task calls:

```cpp
measurement_store_->recordMeasurement(
    record.id,
    record.sensor_type,
    measurement,          // SensorMeasurement from driver
    currentUnixMilliseconds()
);
```

`recordMeasurement()` does three things under a single mutex lock:

### 2a — Update `latest_by_sensor_`

An unbounded list of `LatestMeasurementEntry` (one entry per sensor ID). Always updated regardless of unix time. Used by the web UI (`/`) and the Diagnostics page raw dump to show the most recent reading for each sensor.

```cpp
struct LatestMeasurementEntry {
    uint32_t          sensor_id;
    SensorMeasurement measurement;
    uint64_t          last_sample_time_ms;
};
```

This data is **never queued for upload** — it is a display-only snapshot.

### 2b — Append to `queued_`

A sample is appended to the upload queue **only if `sample_unix_ms > 0`**, i.e., only when SNTP has provided valid UTC time:

```cpp
if (sample_unix_ms > 0 && !measurement.empty()) {
    queued_.push_back(QueuedMeasurementEntry{
        next_sample_id_++,
        MeasurementSample{ sensor_id, sensor_type, sample_unix_ms, measurement },
    });
}
```

If time is not synchronized, readings still update `latest_by_sensor_` (visible in the UI) but are **not enqueued for upload**.

### 2c — Update `queued_count_by_sensor_`

When a sample is appended to `queued_`, its `sensor_id` counter is incremented in `queued_count_by_sensor_` — an `unordered_map<sensor_id, count>` that tracks the number of retained queued samples per sensor.

The map is decremented only when a sample actually leaves the shared queue:

- normal retirement after every active backend has acknowledged it
- overflow dropping from the front of `queued_`

This map is the backing store for `runtimeInfoForSensor()` and `queuedSampleCountForSensor()`, giving both O(1) lookup instead of a full scan of the queues.

```cpp
struct MeasurementSample {
    uint32_t          sensor_id;
    SensorType        sensor_type;
    uint64_t          sample_time_ms;   // unix timestamp in ms
    SensorMeasurement measurement;
};
```

---

## Stage 3 — Shared queue (`queued_`)

### Capacity

Maximum size: **256 samples by default** (`kMaxQueuedSamples`).

The queue depth is a build-time tuning value: `CONFIG_AIR360_MEASUREMENT_QUEUE_DEPTH` via [`tuning::upload::kMeasurementQueueDepth`](../../firmware/main/include/air360/tuning.hpp). Raising it increases short-outage tolerance but also increases the amount of RAM retained by `MeasurementStore`.

All sensors share a single queue — there is no per-sensor limit.

### Overflow behavior

When `queued_.size()` exceeds the configured queue depth after an append, the **oldest samples are dropped** from the front:

```cpp
if (queued_.size() > kMaxQueuedSamples) {
    const size_t overflow = queued_.size() - kMaxQueuedSamples;
    queued_.erase(queued_.begin(), queued_.begin() + overflow);
    dropped_sample_count_ += overflow;
}
```

`dropped_sample_count_` is a monotonic counter — visible in the Diagnostics page raw JSON dump.

### When the queue fills up

With 3 sensors each polling every 5 seconds, the queue accumulates ~36 samples/minute. At the default depth of 256 samples, it takes about **7 minutes** of failed uploads to overflow. With more frequent polling, more sensors, or a lower build-time queue depth, overflow happens sooner.

---

## Stage 4 — Per-backend upload windows

`MeasurementStore` no longer owns one global `inflight_` batch. Instead, `UploadManager` tracks progress separately for each backend:

- `acknowledged_sample_id` — highest sample ID that backend has fully accepted
- `inflight_sample_ids` / `inflight_samples` — retry window currently reserved for that backend
- `next_action_time_ms` — real next attempt time for that backend

For a backend without an existing retry window, `UploadManager` asks the store for:

```cpp
MeasurementQueueWindow uploadWindowAfter(uint64_t after_sample_id, size_t max_samples);
```

Behavior:

1. scan the shared queue in ID order
2. collect up to `max_samples` entries with `sample_id > acknowledged_sample_id`
3. keep those samples only in the backend's runtime state, not in `MeasurementStore`

The window size is still **32 samples** (`kMaxSamplesPerUploadWindow`). This limits the payload size per backend request sequence.

---

## Stage 5 — Batch assembly

The upload task assembles a `MeasurementBatch` from the inflight samples:

```cpp
struct MeasurementBatch {
    uint64_t    batch_id;
    uint64_t    created_uptime_ms;
    int64_t     created_unix_ms;
    string      device_name;
    string      board_name;
    string      project_version;
    string      device_id;
    string      short_device_id;
    string      esp_mac_id;
    NetworkMode network_mode;
    bool        station_connected;
    vector<MeasurementPoint> points;
};
```

Each `MeasurementSample` is expanded into one `MeasurementPoint` per value:

```cpp
struct MeasurementPoint {
    uint32_t      sensor_id;
    SensorType    sensor_type;
    SensorValueKind value_kind;
    float         value;
    uint64_t      sample_time_ms;
};
```

A single sample from a BME280 (3 values) becomes 3 `MeasurementPoint` entries in the batch.

---

## Stage 6 — Backend-aware acknowledgement

After a backend finishes its request sequence:

| Outcome | Action |
|---------|--------|
| `kSuccess` | advance only that backend cursor; clear only that backend retry window |
| `kNoData` | also advance only that backend cursor; used when the adapter has nothing to send for that sample set |
| shared uplink failure (`kNoNetwork`) | keep only that backend retry window; does not count toward best-effort demotion |
| backend-specific failure (`kTransportError`, `kHttpError`, `kConfigError`, `kUnsupported`) | keep only that backend retry window; counts toward best-effort demotion |

Healthy backends therefore continue consuming newer queue entries even while one backend is degraded.

Queue retirement is driven by the **minimum acknowledged sample ID across all quorum backends**. Once every quorum backend has acknowledged sample IDs up to `N`, `MeasurementStore::discardUpTo(N)` removes those entries from the shared queue.

The prune decision is centralized in `upload_prune_policy` and exposed through `MeasurementStore::prune(const PerBackendCursor&)`. The invariants are:

- a sample is retired only after every quorum backend has acknowledged it
- disabled, unconfigured, missing-uploader, and best-effort backends are outside the quorum
- best-effort demotion happens after 5 consecutive backend-specific failures spanning at least 10 minutes
- best-effort backends keep a `missed_sample_count` for windows they skipped or that were retired while they were outside the quorum
- backend acknowledgement cursors and the shared prune cursor only move forward

`kNoNetwork` is not counted as a backend-specific failure for best-effort demotion because it reflects the shared uplink state rather than one broken destination.

If a backend fails, the **same samples for that backend** are retried on the next scheduled attempt. This keeps at-least-once delivery semantics per backend, while avoiding the previous global restore that blocked healthy backends behind one degraded destination.

After a backend is demoted to best-effort, it no longer blocks queue retirement. Backend-specific upload failures for a best-effort backend skip that backend's current window, increment `missed_sample_count`, and move its local cursor forward so the next attempt can try newer retained samples. A later `kSuccess` or `kNoData` clears best-effort status and returns the backend to the quorum.

How each backend delivers a `MeasurementBatch` and maps protocol responses into upload results is covered in [upload-adapters.md](upload-adapters.md).

---

## Upload cycle timing

The upload task loop runs every **1 second** (`kUploadLoopDelay`). Each backend has its own `next_action_time_ms`, so due backends are processed independently.

| Per-backend condition | Next attempt delay |
|-----------------------|--------------------|
| No data after cursor | `upload_interval_ms` (default 145 s) |
| Upload succeeded, queued samples from cycle start remain | immediate next upload window |
| Upload succeeded, cycle-start queue drained | `upload_interval_ms` |
| Shared uplink failure (`kNoNetwork`) | `upload_interval_ms`; does not count toward best-effort demotion |
| Backend-specific failure (`kTransportError`, `kHttpError`, `kConfigError`, `kUnsupported`) | `upload_interval_ms`; counts toward best-effort demotion |

When a backend becomes due, the upload task snapshots the latest queued sample ID as the cycle high-water mark. It then drains windows of up to 32 samples as quickly as each HTTP request sequence completes until that high-water mark is acknowledged. Samples recorded while this drain is running are left for the next scheduled interval, so continuous sensor writes cannot keep the upload task in an infinite drain loop.

---

## Runtime reconfiguration and shutdown

Sensor and backend configuration can be applied at runtime from the web UI. Both managers use explicit task synchronization instead of polling a task handle in a loop:

| Manager | Stop signal | Stop acknowledgement | Stop timeout |
|---------|-------------|----------------------|--------------|
| `SensorManager` | `stop_requested_ = true` plus task notification | `lifecycle_events_` bit from `air360_sensor` exit path | 5 000 ms |
| `UploadManager` | `stop_requested_ = true` plus task notification | `lifecycle_events_` bit from `air360_upload` exit path | 30 000 ms |

`applyConfig()` first asks the existing task to stop and waits for the acknowledgement bit. If the task does not stop before the timeout, reconfiguration is aborted and the existing runtime objects are left untouched. This prevents replacing `sensors_`, backend uploaders, UART handles, or inflight upload windows while the old task may still be executing against them.

The stop request wakes idle task waits immediately. It does not forcibly kill a task that is currently inside a driver call or HTTP request:

- sensor shutdown is bounded by the current driver's `init()` / `poll()` behavior; normal bus transfers use the transport timeouts documented in [transport-binding.md](transport-binding.md)
- upload shutdown is bounded by the current backend delivery call; HTTP-backed adapters are bounded by the current `UploadTransport::execute()` call and its 15 000 ms per-request timeout, and stop handling prevents starting another request after a stop request is observed

`air360_upload` passes watchdog callbacks through `BackendDeliveryContext`, and HTTP-backed adapters feed the TWDT before and after each blocking HTTP request. Multi-request batches therefore keep the TWDT fed between requests, but one in-flight HTTP request still runs synchronously until `esp_http_client_perform()` returns or the request timeout expires.

If runtime apply fails after the config has been saved, the web UI reports that the saved config will apply on reboot.

---

## Conditions that block enqueueing

A measurement is silently dropped from the upload queue (but still updates the UI) if:

- `currentUnixMilliseconds()` returns `<= 0` — SNTP has not synchronized yet
- `measurement.empty()` — the driver returned no values (e.g., SCD30 waiting for first sample)

For how SNTP synchronisation works and how time validity is determined, see [time.md](time.md).

---

## Conditions that block uploading

The upload task does not upload for a backend if:

- no samples exist after that backend's current cursor
- network uplink is not ready
- unix time is not yet valid

---

## Thread safety

`MeasurementStore` is protected by a single static `FreeRTOS` mutex (`xSemaphoreCreateMutexStatic`). Every public method acquires it for the full duration of the operation.

The sensor task and the upload task access `MeasurementStore` concurrently:

| Task | Operations |
|------|------------|
| `air360_sensor` | `recordMeasurement()` |
| `air360_upload` | `uploadWindowAfter()`, `queuedCountAfterUntil()`, `discardUpTo()`, `hasSamplesAfter()`, `pendingCount()` |
| Web server task | `runtimeInfoForSensor()`, `queuedSampleCountForSensor()`, `pendingCount()` |

`runtimeInfoForSensor()` and `queuedSampleCountForSensor()` use the `queued_count_by_sensor_` map for O(1) lookup — the mutex is held for a map lookup rather than a full scan of `queued_`.

---

## Key constants

| Constant | Value | Location |
|----------|-------|----------|
| Sensor task loop period | 250 ms | `sensor_manager.cpp` |
| Upload task loop period | 1 000 ms | `upload_manager.cpp` |
| Sensor reconfigure stop timeout | 5 000 ms | `sensor_manager.cpp` |
| Upload reconfigure stop timeout | 30 000 ms | `upload_manager.cpp` |
| Max queue size | 256 samples | `measurement_store.cpp` |
| Upload window size | 32 samples | `upload_manager.cpp` |
| Max values per measurement | 16 | `sensor_types.hpp` |
| Default upload interval | 145 000 ms | `backend_config.hpp` |
| Sensor retry delay on error | 5 000 ms | `sensor_manager.cpp` |
