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
