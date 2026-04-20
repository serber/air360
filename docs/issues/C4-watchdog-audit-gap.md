# C4 — Main task removes itself from watchdog, subsystem coverage incomplete

- **Severity:** Critical — **RESOLVED** (branch `code_review`)
- **Area:** Reliability / watchdog discipline
- **Files:**
  - `firmware/main/src/app.cpp` (line 393: `esp_task_wdt_delete(nullptr);`)
  - Every long-running task file: `sensor_manager.cpp`, `upload_manager.cpp`, `network_manager.cpp`, `cellular_manager.cpp`, `ble_advertiser.cpp`, `web_server.cpp`

## What is wrong

At the tail of `App::run()` the main task deletes itself from the Task Watchdog (`esp_task_wdt_delete(nullptr)`) before entering the runtime maintenance loop.

The maintenance loop uses `vTaskDelay(kRuntimeMaintenanceDelay)` and calls `status_service_.set*State(...)` periodically — it is not stuck.

The real problem is coverage: the code does not systematically subscribe every long-lived subsystem task to the watchdog. Without that, a stuck sensor poll, a stuck BLE task, a stuck upload worker, or a stuck cellular loop (see C3) will not trigger a TWDT-driven reboot.

## Why it matters

- The watchdog exists precisely to recover from "bench-works-field-hangs" situations.
- Remote devices cannot be hand-rebooted. Silent hangs are the worst failure mode.
- Removing the main task from TWDT is fine *only* if every subsystem task is subscribed. Right now that premise is unverified.

## Consequences on real hardware

- A single subsystem hang (e.g. the cellular `portMAX_DELAY` of C3) leaves the device alive but useless indefinitely.
- Field ops teams chase ghosts because the device still responds on the web portal but stops uploading.

## Fix plan

1. **Inventory every long-lived task.** Expected list:
   - `SensorManager` task
   - `UploadManager` worker(s)
   - `NetworkManager` connect-attempt tasks (spawned — see C6; that refactor simplifies this)
   - `CellularManager` taskBody
   - `BleAdvertiser` taskMain and NimBLE host task
   - `web_server` httpd internal tasks (these have their own watchdog considerations)
   - The `app` maintenance loop
2. **Adopt a uniform pattern.** At each task entry:
   ```cpp
   ESP_ERROR_CHECK(esp_task_wdt_add(nullptr));
   // ... loop that calls esp_task_wdt_reset() on every iteration
   ESP_ERROR_CHECK(esp_task_wdt_delete(nullptr));
   ```
3. **Pick a TWDT timeout that bounds user impact.** Current `sdkconfig.defaults` should be reviewed — recommend 30 s. Document in `startup-pipeline.md`.
4. **Either keep the main task subscribed *or* prove every other task is.** Simpler and safer: leave the main task subscribed. Remove the `esp_task_wdt_delete(nullptr)` at `app.cpp:393`. The maintenance loop runs often enough to feed the watchdog; if not, add an `esp_task_wdt_reset()` call inside the loop.
5. **Add a startup log line** listing every TWDT-subscribed task, so post-mortem logs make coverage auditable.
6. **Document the invariant** in `AGENTS.md` or a new `docs/firmware/watchdog.md`: "Every task declared with `xTaskCreate` that runs longer than 5 s MUST be subscribed to TWDT." Add a checklist item to `firmware-change-checklist`.

## Verification

- Inject a `vTaskDelay(portMAX_DELAY)` into each subsystem's loop in turn; confirm TWDT fires and the device reboots within the timeout.
- Boot log shows TWDT subscribers; count matches the inventory.
- No regressions: the system-wide maintenance loop continues to feed successfully.

## Related

- C3 (cellular portMAX_DELAY) depends on this discipline being real.
- C5 (BLE shutdown) should also subscribe/unsubscribe the BLE task cleanly.

## Resolved

**What was changed:**

- `app.cpp` — removed `esp_task_wdt_delete(nullptr)` before the runtime loop; added `esp_task_wdt_reset()` inside the loop; changed TWDT timeout from 10 000 ms to 30 000 ms; changed `trigger_panic = false` → `true`.
- `sensors/sensor_manager.cpp` — added `esp_task_wdt_add(nullptr)` on task entry; `esp_task_wdt_reset()` after `ulTaskNotifyTake`; `esp_task_wdt_delete(nullptr)` before self-delete.
- `uploads/upload_manager.cpp` — same TWDT subscribe/reset/delete pattern; reset in both the early-continue path and the end-of-loop path.
- `cellular_manager.cpp` — added TWDT subscribe on entry; `esp_task_wdt_reset()` after `attemptConnect()` and after `doHardwareReset()`; replaced `vTaskDelay(pdMS_TO_TICKS(backoff_ms))` with `wdtFeedingDelay(backoff_ms)` (feeds the watchdog every 5 s during backoffs up to 5 min).

**Outstanding tasks (not subscribed):**
- `air360_ble` — deferred to C5; its watchdog subscription is coupled to the cooperative-shutdown redesign.
- `wifi_reconnect` / `wifi_ap_retry` helper tasks spawned by `NetworkManager::reconnectTimerCallback` and `setupApRetryTimerCallback` — these are short-lived, but `resetCurrentTaskWatchdogIfSubscribed()` (`network_manager.cpp:165`) silently becomes a no-op because they are not subscribed. Full fix is part of C6 (persistent worker-task refactor). Until C6 lands, a hung Wi-Fi reconnect task will not trigger TWDT.
