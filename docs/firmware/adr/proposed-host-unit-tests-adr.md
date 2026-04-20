# Host-Side Unit Tests ADR

## Status

Partially implemented.

## Decision Summary

Add a native host-compiled test suite for firmware business logic, covering `MeasurementStore`, the sensor state machine in `SensorManager`, config validation in the repository layer, and web form parsing.

The first Phase 8 slice is implemented under `firmware/test/host/`: a CMake/CTest target compiles production `main/src/web/web_form.cpp` directly and tests URL decoding plus `application/x-www-form-urlencoded` parsing behavior. It intentionally avoids ESP-IDF stubs because this first target is pure C++17 logic.

## Context

Before Phase 8, the firmware had no automated tests. Most validation was done manually by flashing the device and observing behavior. This works for hardware-dependent code (driver communication, GPIO), but a significant portion of the codebase is pure logic with no hardware dependency:

- `MeasurementStore` — queue semantics, overflow, window mechanics, acknowledge/restore cycle
- `SensorManager` state machine — transitions between `kConfigured`, `kInitialized`, `kPolling`, `kError`, `kAbsent`
- Config validation in `ConfigRepository`, `SensorConfigRepository` — magic/version checks, field bounds
- Batch assembly in `UploadManager` — `MeasurementSample` → `MeasurementBatch` → `MeasurementPoint` expansion
- Web form parsing — URL decoding, duplicate keys, checkbox-style keys without values

Regressions in any of these are only caught by full hardware testing, which is slow and requires physical access to the device.

ESP-IDF includes [Unity](https://github.com/ThrowTheSwitch/Unity) and has first-class support for host-compiled tests (`idf.py -C test build` or direct CMake on host), removing the need to introduce a third-party test framework. The initial implementation uses plain C++ assertions through CTest to avoid pulling Unity into the host harness before ESP-IDF stubs are needed.

## Goals

- Run tests on the development host (macOS / Linux) without hardware.
- Cover the queue mechanics of `MeasurementStore` completely.
- Cover the sensor state machine transitions.
- Integrate into a simple `make test` or `cmake --build && ctest` workflow.
- Not require a complete ESP-IDF mock — only stub the symbols actually used by tested code.

## Non-Goals

- Testing hardware drivers (BME280, GPS, etc.) — these require real hardware.
- Testing FreeRTOS scheduler behavior — stub the relevant primitives only.
- 100% coverage as a goal — focus on the highest-value, highest-risk logic.

## Architectural Decision

### Test directory structure

```
firmware/
  test/
    host/
      CMakeLists.txt
      test_web_form.cpp
      stubs/
        freertos_stubs.cpp   # xSemaphoreCreateMutexStatic, xSemaphoreTake, etc.
        esp_log_stubs.cpp    # ESP_LOGI/W/E/D → printf
        time_stubs.cpp       # esp_timer_get_time, gettimeofday
      test_measurement_store.cpp
      test_sensor_state_machine.cpp
      test_config_validation.cpp
      test_batch_assembly.cpp
```

### Stub strategy

Only stub symbols that the test targets actually link against. The goal is minimal stubs, not a complete ESP-IDF emulation. The current `test_web_form` target needs no stubs.

- FreeRTOS mutex: `xSemaphoreCreateMutexStatic` returns a pointer to a static buffer; `xSemaphoreTake` / `xSemaphoreGive` are no-ops (tests are single-threaded).
- ESP logging: redirect to `printf`.
- `esp_timer_get_time()`: return a controllable counter (allows testing time-based scheduling logic).
- `gettimeofday()`: return a controllable value (allows testing SNTP validity checks).

### CMake host build

The test `CMakeLists.txt` compiles tested `.cpp` files directly as native host binaries (not as an ESP-IDF component). It links against Unity and the stubs. No IDF toolchain is needed.

### Test cases (initial set)

**`test_web_form.cpp`** — implemented
- `urlDecode` converts `+`, uppercase/lowercase hex escapes, and preserves invalid/trailing escapes
- `parseFormBody` decodes names and values
- key-only fields are treated as present with an empty value
- duplicate keys keep first-match `findFormValue` behavior

**`test_measurement_store.cpp`** — planned
- `recordMeasurement` with unix_ms=0 does not enqueue
- `recordMeasurement` with valid unix_ms enqueues
- Queue overflow drops oldest samples, increments dropped count
- `beginUploadWindow` moves correct slice to inflight
- `beginUploadWindow` returns same inflight on re-call
- `acknowledgeInflight` clears inflight
- `restoreInflight` prepends inflight back to pending
- `restoreInflight` respects 256-sample cap on restore

**`test_sensor_state_machine.cpp`**
- State transitions: Configured → Initialized on init success
- State transitions: Initialized → Polling on poll success
- State transitions: Polling → Error on poll failure
- State transitions: Error → Initialized on next init success (re-init cycle)
- `classifyFailureState` maps `ESP_ERR_NOT_FOUND` → `kAbsent`

**`test_config_validation.cpp`**
- Magic mismatch triggers defaults
- Version mismatch triggers defaults
- Valid blob round-trips correctly

## Affected Files

- `firmware/main/include/air360/web_form.hpp` — pure form parsing interface
- `firmware/main/src/web/web_form.cpp` — production URL/form parsing implementation
- `firmware/main/src/web/web_server_helpers.cpp` — HTTP-only helpers after form parsing moved out
- `firmware/test/host/` — native CMake/CTest host harness
- `firmware/test/host/test_web_form.cpp` — first host test target

The test build is fully separate from the ESP-IDF firmware build.

## Alternatives Considered

### Option A. No tests (current state)

Zero maintenance cost. Regressions caught only by hardware testing or production incidents.

### Option B. ESP-IDF on-target Unity tests

ESP-IDF supports running Unity tests on the device via `idf_component_register(... WHOLE_ARCHIVE)`. Requires flashing for every test run. Slower feedback loop. Better for integration tests.

### Option C. Host-compiled with minimal stubs (accepted)

Fast feedback (sub-second), no hardware required, CI-friendly. Covers the highest-value logic. On-target tests can be added separately for hardware integration coverage.

## Practical Conclusion

Create `firmware/test/host/` with a CMake-based host build. The first target covers `web_form.cpp` without ESP-IDF stubs. Future targets should add minimal FreeRTOS/ESP-IDF stubs only when testing `MeasurementStore`, sensor state machine, config validation, or batch assembly requires them.

Run with:

```bash
cmake -S firmware/test/host -B firmware/test/host/build
cmake --build firmware/test/host/build
ctest --test-dir firmware/test/host/build --output-on-failure
```
