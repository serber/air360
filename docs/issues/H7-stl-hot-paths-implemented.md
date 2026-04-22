# H7 — STL containers on hot paths, heap fragmentation risk

- **Severity:** High
- **Area:** Performance / memory footprint
- **Files:**
  - `firmware/main/src/uploads/measurement_store.cpp` (`std::unordered_map<uint32_t, uint32_t>`, `std::deque`)
  - `firmware/main/src/web_server.cpp` (std::string concatenation rendering)
  - `firmware/main/src/sensors/sensor_manager.cpp` (latest-measurements passed as `std::vector`)
  - `firmware/main/src/sensors/drivers/gps_nmea_sensor.cpp` (`std::make_unique<TinyGPSPlus>` per init)

## What is wrong

Heap-allocating STL containers are used in paths that run often and/or in reconnect/re-init loops:

- `std::unordered_map` has per-bucket allocations; insert/erase churn creates fragmentation.
- `std::string` grows geometrically — repeated `+=` produces many realloc events.
- `std::vector<MeasurementRuntimeInfo>` returned by value from sensor enumeration copies data into a fresh heap allocation on every call.
- `std::make_unique<TinyGPSPlus>` inside a driver's `init()` allocates on every re-init (see H6).

## Why it matters

- ESP32-S3 heap is not hostile to STL, but the fragmentation pattern — many small allocations of varying sizes — defeats the best-fit allocator over days.
- "Plenty of free heap but no contiguous block" is the classic embedded STL failure mode.
- Visible only after long uptime, *exactly* the case that bench testing misses.

## Consequences on real hardware

- After 5–14 days, allocations of ~4–8 KB (TLS handshake buffer, large HTTP response body) start failing while total free heap is well above that size.
- Recovery requires reboot; the device looks fine in telemetry right up until it stops uploading.

## Fix plan

1. **`measurement_store.cpp`:**
   - Replace `std::unordered_map<uint32_t, uint32_t>` with a fixed-size `std::array<PerSensorCount, kMaxSensors>` where `PerSensorCount = { uint32_t sensor_id; uint32_t count; }`. Linear scan is faster than hashing for small N (typical < 20).
   - Replace `std::deque<Sample>` with a fixed-capacity ring buffer (`std::array<Sample, kMaxQueuedSamples>` + head/tail indices).
   - Zero heap allocations on the hot path.
2. **`web_server.cpp`:**
   - Pre-allocate the output `std::string` with `.reserve(4096)` in each handler.
   - Where responses are short and known-bounded, render directly into a `char[N]` with `snprintf`.
   - Covered further in H3 (splitting the monolith) and M7 (stack/heap trade-off).
3. **Sensor enumeration:**
   - Change `allLatestMeasurements()` to accept an output buffer:
     ```cpp
     size_t MeasurementStore::allLatestMeasurements(
         MeasurementRuntimeInfo* out, size_t out_cap) const;
     ```
   - Callers pass `std::array<..., kMaxSensors>` on the stack.
4. **GPS driver:**
   - Move `TinyGPSPlus` to a data member; construct once.
   - In `init()`, `parser_.~TinyGPSPlus(); new (&parser_) TinyGPSPlus();` if a reset is needed — placement-new avoids heap churn.
5. **Project-wide grep pass.** Flag all `std::vector<...> func()` returns in hot paths; replace with out-parameters or `std::span`.
6. **Document the rule** in `AGENTS.md`: "No heap allocations in sensor poll, upload worker, or HTTP handler hot paths. If unavoidable, justify and measure."
7. **Add heap telemetry.** Log `heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)` alongside free heap on the status page — fragmentation shows up there first.

## Verification

- Add a CI metric for largest-free-block after a synthetic 24 h soak (can be simulated: 1 M poll cycles, 10 K web requests, 1 K cellular reconnects).
- Largest free block drift stays within 10 % of the boot value.
- `heap_caps_print_heap_info()` on command shows no small-block explosion.

## Related

- C2 — queue ring-buffer design is part of this fix.
- H3 — web server rendering is part of this fix.

## Resolved

Implemented in the `code_review` branch. Per-plan-step summary:

1. **`measurement_store.cpp`** — `std::unordered_map<uint32_t, uint32_t>` replaced
   with `std::array<PerSensorCount, kMaxConfiguredSensors>` and linear scan.
   `std::deque<Sample>` (originally backed by `std::vector<QueuedMeasurementEntry>`)
   replaced with `std::array<QueuedMeasurementEntry, kMaxQueuedSamples>` + `queued_head_`
   / `queued_size_` ring-buffer indices. `latest_by_sensor_` likewise moved to
   `std::array<LatestMeasurementEntry, kMaxConfiguredSensors>` + `latest_count_`.
   `recordMeasurement`, `append`, `discardUpTo`, and `uploadWindowAfterUntil` now
   walk the ring buffer via `queuedIndex(offset)` with zero heap churn on the
   sensor poll path.
2. **`web_server.cpp`** — Added `.reserve(...)` calls to the HTML rendering
   helpers that grow by `+=`: `renderBackendCard` blocks (https/endpoint/device-id
   /status), `renderSensorCard` (runtime-error / latest-reading / i2c / gpio),
   `renderSensorCategorySection` (cards_html already reserved; now also
   `add_form_block`, `i2c_field_block`, `gpio_field_block`, `section_html`),
   plus `buildBleIntervalOptions`, `renderHttpsCheckbox`, `renderEndpointFields`,
   `renderAuthFields`, `sensorTypeOptionHtml`, and the `/check-sntp` JSON
   response in `web/web_runtime_routes.cpp`. `status_service.cpp` rendering
   helpers (`measurementListHtml`, `measurementArrayJson`,
   `renderBackendOverviewBlock`, `renderSensorOverviewBlock`,
   `renderConnectionBlock`, `renderDiagnosticsNetworkBlock`,
   `renderDiagnosticsTaskBlock`, BLE block) now size-hint their output buffers.
3. **Sensor enumeration** — `MeasurementStore::allLatestMeasurements()` is now
   `std::size_t allLatestMeasurements(MeasurementRuntimeInfo* out, size_t out_cap)`.
   The only caller (`BleAdvertiser::buildPayload` in `ble_advertiser.cpp`) uses
   a stack-allocated `std::array<MeasurementRuntimeInfo, kMaxConfiguredSensors>`.
4. **GPS driver** — `gps_nmea_sensor.{hpp,cpp}` embed `TinyGPSPlus parser_{}` as
   a data member instead of `std::unique_ptr<TinyGPSPlus>`. `init()` resets via
   placement-new (`parser_.~TinyGPSPlus(); new (&parser_) TinyGPSPlus();`) so
   re-initialization does not touch the heap.
5. **Project-wide grep pass** — documented below under "Outstanding". Core hot
   paths (sensor poll, GPS re-init, BLE advert build) are allocation-free; the
   bounded per-request vector copies in `SensorManager::sensors()` and
   `UploadManager::backends()` remain (one bounded allocation each per HTTP
   request or upload cycle) and are flagged for follow-up.
6. **`AGENTS.md` rule** — added under `## Firmware concurrency rules`:
   "Sensor poll, upload worker, and HTTP handler hot paths MUST NOT add heap
   allocations. If an allocation is unavoidable, justify it in the code/docs
   and measure the heap impact, including largest free 8-bit heap block."
7. **Heap telemetry** — `heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)` is
   already wired into `RuntimeDiagnosticsSnapshot` (`largest_heap_block_bytes`,
   status_service.cpp:314/803) and exposed on `/diagnostics`
   (`{{LARGEST_BLOCK}}` in `page_diagnostics.html`) and in
   `/status.json` (`heap_largest_block_bytes`).

## Outstanding

Remaining bounded-heap allocations identified by the project-wide grep pass,
deferred from this change because they are bounded per HTTP request / upload
cycle rather than per poll:

- `SensorManager::sensors()` returns `std::vector<SensorRuntimeInfo>` (bounded
  by `kMaxConfiguredSensors` = 8). Called once per status render.
- `UploadManager::backends()` returns `std::vector<BackendStatusSnapshot>`
  (bounded by the configured backend count). Called once per status render.
- `UploadManager::buildManagedBackends` / `SensorManager::buildManagedSensors`
  allocate on `applyConfig` — rare, cold path.
- `UploadManager::taskMain` local vectors (`due_indices`, `upload_sample_ids`,
  `upload_samples`, `acknowledged_by_id`, `unique_inflight_ids`, `requests`)
  and the per-adapter `SampleGroup` / `SensorCommunityGroup` staging vectors
  in the upload worker. These run once per upload cycle (minutes apart) rather
  than per sample, but should be revisited as part of a dedicated upload-path
  refactor if heap fragmentation is still observed after soak testing.
- `MeasurementStore::snapshot()` still returns a `MeasurementStoreSnapshot`
  containing `std::vector<MeasurementRuntimeInfo>` (bounded). Conversion to a
  fixed-size struct was intentionally skipped to avoid ripple-effect churn
  across `status_service.cpp` callers; the underlying storage is already
  allocation-free.
