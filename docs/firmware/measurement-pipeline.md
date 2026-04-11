# Measurement Pipeline

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
       ├─ latest_by_sensor_   ← always updated (for web UI / /status)
       │
       └─ pending_            ← appended only when unix_ms > 0
            │
            └─ air360_upload task (1 s loop, fires every upload_interval_ms)
                 │
                 ├─ beginUploadWindow(32)  → moves up to 32 samples to inflight_
                 ├─ buildMeasurementBatch()
                 ├─ uploader->buildRequests() + transport_.execute()
                 │
                 ├─ success → acknowledgeInflight()  → inflight_ cleared
                 └─ failure → restoreInflight()       → inflight_ prepended back to pending_
```

---

## Stage 1 — Sensor polling (`air360_sensor` task)

The sensor task runs every **250 ms**. On each iteration it walks the list of managed sensors and checks each one against its scheduled `next_action_time_ms`.

For each sensor due for action:

| Condition | Action |
|-----------|--------|
| `driver_ready == false` | Call `driver->init(record, context)` |
| `driver_ready == true` | Call `driver->poll()` |

### After `init()`

- On success: `driver_ready = true`, state → `kInitialized`, `next_action_time_ms = now` (poll immediately on the next loop iteration)
- On failure: `driver_ready = false`, state → `kAbsent` or `kError`, `next_action_time_ms = now + min(poll_interval_ms, 5000)`

### After `poll()`

- On success: state → `kPolling`, `next_action_time_ms = now + poll_interval_ms`
- On failure: `driver_ready = false` (forces re-init on next action), state → `kAbsent` or `kError`, `next_action_time_ms = now + min(poll_interval_ms, 5000)`

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

`recordMeasurement()` does two things under a single mutex lock:

### 2a — Update `latest_by_sensor_`

An unbounded list of `LatestMeasurementEntry` (one entry per sensor ID). Always updated regardless of unix time. Used by the web UI (`/` and `/status`) to show the most recent reading for each sensor.

```cpp
struct LatestMeasurementEntry {
    uint32_t          sensor_id;
    SensorMeasurement measurement;
    uint64_t          last_sample_time_ms;
};
```

This data is **never queued for upload** — it is a display-only snapshot.

### 2b — Append to `pending_`

A sample is appended to the upload queue **only if `sample_unix_ms > 0`**, i.e., only when SNTP has provided valid UTC time:

```cpp
if (sample_unix_ms > 0 && !measurement.empty()) {
    pending_.push_back(MeasurementSample{ sensor_id, sensor_type,
                                          sample_unix_ms, measurement });
}
```

If time is not synchronized, readings still update `latest_by_sensor_` (visible in the UI) but are **not enqueued for upload**.

```cpp
struct MeasurementSample {
    uint32_t          sensor_id;
    SensorType        sensor_type;
    uint64_t          sample_time_ms;   // unix timestamp in ms
    SensorMeasurement measurement;
};
```

---

## Stage 3 — `pending_` queue

### Capacity

Maximum size: **256 samples** (`kMaxQueuedSamples`).

All sensors share a single queue — there is no per-sensor limit.

### Overflow behavior

When `pending_.size()` exceeds 256 after an append, the **oldest samples are dropped** from the front:

```cpp
if (pending_.size() > kMaxQueuedSamples) {
    const size_t overflow = pending_.size() - kMaxQueuedSamples;
    pending_.erase(pending_.begin(), pending_.begin() + overflow);
    dropped_sample_count_ += overflow;
}
```

`dropped_sample_count_` is a monotonic counter — visible in `/status` JSON.

### When the queue fills up

With 3 sensors each polling every 5 seconds, the queue accumulates ~36 samples/minute. At that rate it takes about **7 minutes** of failed uploads to overflow 256 samples. With more frequent polling or more sensors, overflow can happen sooner.

---

## Stage 4 — Upload window (`beginUploadWindow`)

The upload task calls `beginUploadWindow(32)` at the start of each upload cycle:

```cpp
vector<MeasurementSample> beginUploadWindow(size_t max_samples);
```

Behavior:

1. If `inflight_` is **not empty** (previous window was not acknowledged): returns the existing `inflight_` snapshot — the same samples are retried
2. If `inflight_` is empty and `pending_` is not empty: moves up to `max_samples` samples from the front of `pending_` into `inflight_` and returns them

The window size is **32 samples** (`kMaxSamplesPerUploadWindow`). This limits the payload size per upload request.

`pending_` and `inflight_` are **mutually exclusive**: a sample is either pending, in-flight, or acknowledged (gone).

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
    string      chip_id;
    string      short_chip_id;
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

## Stage 6 — Upload and queue acknowledgement

After uploading to all enabled backends:

| Outcome | Action |
|---------|--------|
| **All backends succeeded** | `acknowledgeInflight()` — `inflight_` is cleared |
| **Any backend failed** | `restoreInflight()` — `inflight_` is prepended back to the front of `pending_` |

`restoreInflight()` also applies the 256-sample cap, dropping oldest samples if the re-prepended inflight data pushes the queue over the limit.

If a backend fails, the **same 32 samples** will be retried on the next upload cycle. This guarantees at-least-once delivery per sample, at the cost of potential duplicates if a backend accepted the data but the response was lost.

---

## Upload cycle timing

The upload task loop runs every **1 second** (`kUploadLoopDelay`). The actual upload fires when `uptime >= next_cycle_time_ms`.

| Condition | Next cycle delay |
|-----------|-----------------|
| No data | `upload_interval_ms` (default 145 s) |
| Upload succeeded, no backlog | `upload_interval_ms` |
| Upload succeeded, backlog > 0 | `min(upload_interval_ms, 5000 ms)` |
| Upload failed | `upload_interval_ms` |
| No network / no time | 1 s (re-check next iteration) |

The **backlog drain** shortens the next cycle to 5 seconds when there are still pending samples after a successful upload. This drains the queue faster without changing the configured interval under normal conditions.

---

## Conditions that block enqueueing

A measurement is silently dropped from the upload queue (but still updates the UI) if:

- `currentUnixMilliseconds()` returns `<= 0` — SNTP has not synchronized yet
- `measurement.empty()` — the driver returned no values (e.g., SCD30 waiting for first sample)

---

## Conditions that block uploading

The upload task skips the upload cycle (delays 1 s and retries) if:

- Network mode is not `kStation`
- Station is not connected
- `hasValidTime()` returns false — unix time not yet valid

---

## Thread safety

`MeasurementStore` is protected by a single static `FreeRTOS` mutex (`xSemaphoreCreateMutexStatic`). Every public method acquires it for the full duration of the operation.

The sensor task and the upload task access `MeasurementStore` concurrently:

| Task | Operations |
|------|------------|
| `air360_sensor` | `recordMeasurement()` |
| `air360_upload` | `beginUploadWindow()`, `acknowledgeInflight()`, `restoreInflight()`, `pendingCount()` |
| Web server task | `runtimeInfoForSensor()`, `pendingCount()`, `inflightCount()` |

---

## Key constants

| Constant | Value | Location |
|----------|-------|----------|
| Sensor task loop period | 250 ms | `sensor_manager.cpp` |
| Upload task loop period | 1 000 ms | `upload_manager.cpp` |
| Max queue size | 256 samples | `measurement_store.cpp` |
| Upload window size | 32 samples | `upload_manager.cpp` |
| Max values per measurement | 16 | `sensor_types.hpp` |
| Backlog drain interval | 5 000 ms | `upload_manager.cpp` |
| Default upload interval | 145 000 ms | `backend_config.hpp` |
| Sensor retry delay on error | 5 000 ms | `sensor_manager.cpp` |
