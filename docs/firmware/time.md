# Time

The firmware operates with two independent time domains: **uptime** and **wall clock (Unix time)**. They serve different purposes and have different availability guarantees.

---

## Two time domains

| Domain | Source | Always available | Unit | Used for |
|--------|--------|-----------------|------|---------|
| Uptime | `esp_timer_get_time()` | Yes — from power-on | milliseconds (uint64) | Scheduling, timeouts, durations |
| Unix time | SNTP → `gettimeofday()` | No — requires network sync | milliseconds (int64) | Sample timestamps, upload payloads |

```cpp
// time_utils.hpp

inline uint64_t uptimeMilliseconds() {
    return esp_timer_get_time() / 1000ULL;
}

inline bool hasValidUnixTime() {
    struct timeval tv {};
    gettimeofday(&tv, nullptr);
    return tv.tv_sec >= kMinValidUnixTimeSeconds;
}

inline int64_t currentUnixMilliseconds() {
    struct timeval tv {};
    gettimeofday(&tv, nullptr);
    if (tv.tv_sec < kMinValidUnixTimeSeconds) return 0;
    return tv.tv_sec * 1000LL + tv.tv_usec / 1000;
}
```

---

## Uptime

`uptimeMilliseconds()` wraps `esp_timer_get_time()` which counts microseconds since chip power-on. It is available immediately, monotonic, and never returns 0 after the first tick.

Used by:
- `SensorManager` — `next_action_time_ms` scheduling for each sensor
- `UploadManager` — `next_cycle_time_ms` for upload interval
- `UploadTransport` — measuring request wall-clock duration
- `NetworkManager` — `waitForStationResult()` polling loop, `lastScanUptimeMs()`
- `MeasurementStore` — `SensorMeasurement.sample_time_ms` (uptime-based timestamp inside the measurement struct; not the upload timestamp)

---

## Unix time (wall clock)

`currentUnixMilliseconds()` reads the system `timeval` via `gettimeofday()`. The ESP32-S3 has no hardware RTC — the clock is only valid after SNTP synchronization. Before sync, `gettimeofday()` returns a near-zero value from epoch, which is always below `kMinValidUnixTimeSeconds`.

### Validity threshold

```cpp
constexpr time_t kMinValidUnixTimeSeconds = 1700000000;  // 2023-11-14 UTC
```

Any `tv_sec` below this constant is treated as not-yet-set. `currentUnixMilliseconds()` returns `0` in this case; `hasValidUnixTime()` returns `false`. This threshold guards against the chip's default epoch value and clock drift to unreasonably old timestamps.

---

## SNTP synchronisation

SNTP runs over the station Wi-Fi interface. It is not attempted in setup AP mode.

### When it runs

| Trigger | Timeout |
|---------|---------|
| Immediately after `connectStation()` succeeds | 15 000 ms |
| Maintenance loop retry (if station connected, time still invalid) | 10 000 ms |

### Sync sequence (`NetworkManager::synchronizeTime`)

1. Requires `station_connected == true`; returns `ESP_ERR_INVALID_STATE` otherwise.
2. If `hasValidUnixTime()` is already true — marks `time_synchronized = true` and returns `ESP_OK` immediately (no SNTP traffic sent).
3. First call: `esp_netif_sntp_init()` with `DeviceConfig.sntp_server` if non-empty, otherwise `pool.ntp.org`. Sets `sntp_initialized = true`.
4. Subsequent calls: `esp_netif_sntp_start()` (re-arms the already-initialised client).
5. Polls `hasValidUnixTime()` every **250 ms**, resetting the task watchdog on each iteration, until the threshold is crossed or the timeout expires.
6. On success: sets `time_synchronized = true`, records `last_time_sync_unix_ms`.
7. On timeout: sets `time_sync_error = "time is still invalid after SNTP sync"`, returns `ESP_ERR_TIMEOUT`.

### Maintenance loop retry

`App::run()` retries time sync on every maintenance loop iteration as long as the station is connected but `hasValidTime()` returns false:

```cpp
if (mode == kStation && station_connected && !hasValidTime()) {
    ensureStationTime(10000);
}
```

This covers the case where SNTP timed out during the initial boot (e.g., NTP server temporarily unreachable) but the station connection itself is healthy.

### SNTP constants

| Constant | Value |
|----------|-------|
| Server | `DeviceConfig.sntp_server` if non-empty, otherwise `pool.ntp.org` |
| Poll interval (during sync wait) | 250 ms |
| Initial sync timeout | 15 000 ms |
| Maintenance retry timeout | 10 000 ms |

---

## Time state in `NetworkState`

`NetworkManager::state()` exposes the following time-related fields:

| Field | Type | Meaning |
|-------|------|---------|
| `time_sync_attempted` | `bool` | `synchronizeTime()` was called at least once |
| `time_synchronized` | `bool` | `hasValidUnixTime()` returned true after a sync attempt |
| `time_sync_error` | `string` | Error message if the last sync attempt failed |
| `last_time_sync_unix_ms` | `int64_t` | Unix timestamp (ms) at which sync was confirmed; 0 if never |

---

## Where valid time is required

Unix time gates several parts of the system. Without SNTP sync, measurements are still taken and displayed in the web UI, but they are not queued for upload.

### `MeasurementStore::recordMeasurement()`

```cpp
if (sample_unix_ms > 0 && !measurement.empty()) {
    pending_.push_back(...);
}
```

`sample_unix_ms` is set to `currentUnixMilliseconds()` at the moment the sensor task calls `recordMeasurement()`. If this returns `0` (time not valid), the sample updates `latest_by_sensor_` for the web UI but is **not** added to the upload queue.

### `UploadManager` upload cycle

The upload task skips the upload cycle entirely if any of these conditions are true:

- Network mode is not `kStation`
- Station is not connected
- `hasValidTime()` returns `false`

In this case the task waits 1 second and checks again.

### `Air360ApiUploader::buildRequests()`

The Air360 API adapter enforces an explicit precondition:

```
batch.created_unix_ms <= 0  →  returns false with error
```

This prevents sending a batch with an invalid timestamp to the server. The Sensor.Community adapter has no equivalent check — it does not include a batch timestamp in its payload.

### `MeasurementBatch.created_unix_ms`

Set during batch assembly from `currentUnixMilliseconds()` at the time the upload cycle begins. This is the timestamp used in the Air360 API `sent_at_unix_ms` field. Individual sample timestamps (`sample_time_ms` in each `MeasurementPoint`) come from the original `MeasurementSample.sample_time_ms`, recorded at poll time.

---

## Summary: time flow

```
SNTP sync (configured server or pool.ntp.org)
  └─ NetworkManager::synchronizeTime()
       └─ gettimeofday() returns tv_sec ≥ 1700000000
            │
            ├─ hasValidUnixTime() = true
            │
            ├─ MeasurementStore: samples enqueued (sample_unix_ms > 0)
            │
            ├─ UploadManager: upload cycle unblocked
            │
            └─ Air360ApiUploader: batch accepted (created_unix_ms > 0)
```

Until SNTP sync completes, the device polls sensors, updates the web UI with live readings, but accumulates no data in the upload queue.
