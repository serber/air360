# Air360 firmware coding rules (supplement)

The canonical rules live in `firmware/CLAUDE.md`: log tags (`kTag` in an anonymous
namespace, `"air360.<subsystem>"`, registry of subsystem identifiers), return-value
discipline (`[[nodiscard]]`, commented `static_cast<void>(...)`), timer-callback
restrictions, TWDT contract, C-header `extern "C"` rule, co-change expectations.
Read that file first. This page only adds what it does not say.

## `-Werror` traps seen in this repository

| Symptom | Cause | Fix |
|---------|-------|-----|
| `error: no previous declaration for 'std::string air360::foo(...)' [-Werror=missing-declarations]` | A free helper function defined in a `.cpp` at namespace scope without a declaration in a header | Put helpers inside the file's `namespace { ... }` block, above their first use |
| `error: 'foo' was not declared in this scope` right after adding a helper | The helper was inserted below the function that uses it | Anonymous-namespace helpers must precede their callers; the block usually sits near the top of the file |
| `warning: implicit conversion from 'const float' to 'double' [-Wdouble-promotion]` | `float` passed to `snprintf`/`ESP_LOG*` `%f` | `static_cast<double>(value)` at the call |
| `format '%u' expects 'unsigned int' but argument is 'uint32_t'` | Fixed-width integers in format strings | `PRIu32`, `PRIu64`, `PRId32` from `<cinttypes>`; `static_cast<unsigned>(x)` for `size_t` counts |

## C++ style

- C++20, no exceptions, no RTTI, no `dynamic_cast`.
- `enum class : std::uint8_t` for persisted or JSON-exposed enums; every enum gets a
  `...Key()` function returning a stable snake_case token for JSON/forms.
- Fallible operations return `esp_err_t`; `bool` only for pure predicates.
- Constants are `constexpr` in the anonymous namespace with a one-line comment that
  says *why* the value was chosen, not what it is.
- Long-lived objects are non-copyable/non-movable (`= delete` all four).
- Strings for status text are `std::string`; fixed buffers (`char buffer[160]` +
  `std::snprintf`) for formatting, then assign.
- Headers stay light: forward-declare, avoid pulling FreeRTOS or NVS headers into
  public headers when the type is only needed in the `.cpp` (host tests include
  many headers through `test/host/stubs`).

## FreeRTOS patterns in use

- **Locking:** `lock()/unlock()` pair around a static mutex (`xSemaphoreCreateMutexStatic`).
  Copy what you need out under the lock, act outside it. Never call into another
  manager while holding your own lock.
- **Worker notification:** one persistent worker task, level-triggered request bits
  via `xTaskNotify(..., eSetBits)` / `xTaskNotifyWait`. Timers and event handlers
  only set bits. A missed notify is recovered on the next timer tick.
- **Timers:** `armTimer()` = stop + change period + start; `stopTimerIfRunning()`.
  Arm the retry timer *before* setting the event-group bit that wakes a waiter, so
  the waiter sees a scheduled retry.
- **Waiting with TWDT:** slice long waits into ≤ 250–3000 ms `vTaskDelay` chunks and
  call `esp_task_wdt_reset()` per slice (`wdtFeedingDelay()` where available). The
  main task has a 30 s TWDT; the sensor task 250 ms loop cadence.
- **Task lifecycle:** cooperative stop through an atomic flag + notify, acknowledged
  with an event-group bit, then `esp_task_wdt_delete(nullptr)` and `vTaskDelete(nullptr)`
  from inside the task. Document stack size, priority, and loop period next to the
  `xTaskCreate` constants.
- **Deep sleep:** `esp_sleep_enable_timer_wakeup()` then `esp_deep_sleep_start()`
  (noreturn). Only `RTC_DATA_ATTR` survives. Turn the LED off first; nothing else
  needs teardown when sleep happens before the radios start.

## Persistence patterns

- Each blob repository: magic + schema_version + record_size guard, size check on
  load, `isValid()` on load and on save, defaults written on any failure.
- New fields are appended at the end of the struct; add them to
  `makeDefault…()`, the validator, the web form, and the docs (`nvs.md`,
  `configuration-reference.md`). No migration unless the user asks for one.
- Ranges and defaults are `constexpr` in the repository header so the web validator
  and the repository share them (build a probe record and call the repository
  validator instead of duplicating the checks).

## Web UI patterns

- Templates in `main/webui/*.html` use `{{BINDING}}` placeholders filled by a view
  model struct in `web_server.cpp`; one binding per value, HTML-escape user text.
- A card that should be conditionally shown gets a `{{..._HIDDEN}}` binding that
  expands to ` hidden` on the `<section>`; its inputs are still submitted.
- Switches are `.switch` buttons driving a hidden checkbox (`data-drives-checkbox`),
  optionally revealing a body (`data-reveals`).
- Handler flow: `parseFormBody` → `findFormValue` (fallback to stored value for
  numbers) → `validate…Form` → on error re-render with a preview record → on success
  build the updated record, save, apply/reboot.

## Sensor patterns

- Drivers implement `SensorDriver` (`init`, `poll`, `latestMeasurement`, `lastError`);
  the manager owns retries, warmup, and state transitions.
- Warmup readings are recorded with `sample_unix_ms = 0`: they update the latest
  snapshot but are not queued for upload. Anything that needs "the first reading"
  can read `MeasurementStore::runtimeInfoForSensor()` during warmup.
- `SensorRuntimeState` additions need: the `…Key()` switch, both switches in
  `status_service.cpp` (health classification and chip colour), and the state list
  in `web-ui.md` / `ARCHITECTURE.md`.
