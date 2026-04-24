# Task Watchdog (TWDT)

## Status

Implemented. Keep this document aligned with the current `firmware/` tree.

## Scope

Describes the Task Watchdog Timer (TWDT) configuration, per-task subscription contract, and feeding discipline used in Air360 firmware. Out of scope: panic handler details, hardware watchdog (RWDT), and flash/NVS watchdog interactions.

## Source of truth in code

- `firmware/main/src/app.cpp` — `initWatchdog()`, maintenance loop
- `firmware/main/src/sensors/sensor_manager.cpp` — `taskMain()`
- `firmware/main/src/uploads/upload_manager.cpp` — `taskMain()`
- `firmware/main/src/cellular_manager.cpp` — `taskBody()`, `wdtFeedingDelay()`
- `firmware/main/src/ble_advertiser.cpp` — `taskMain()`
- `firmware/main/src/network_manager.cpp` — `workerLoop()`

## Configuration

TWDT is initialized in `initWatchdog()` (`app.cpp`) with:

| Parameter | Value | Rationale |
|-----------|-------|-----------|
| Timeout | 30 s | Fits the 10 s maintenance sleep with comfortable margin; long enough for healthy cellular reconnect attempts |
| `trigger_panic` | `true` | On timeout the device panics and reboots — the correct behavior for an unattended field device |
| `idle_core_mask` | all cores | FreeRTOS idle tasks on both cores are also supervised |

If ESP-IDF pre-initializes the TWDT (via `sdkconfig`), `initWatchdog()` attaches to the existing instance. The code-side values take effect only when TWDT is not yet running.

## Per-task subscription contract

**Every task that runs for more than ~5 seconds MUST be subscribed to the TWDT.**

Pattern:

```cpp
void MySubsystem::taskMain() {
    esp_task_wdt_add(nullptr);
    ESP_LOGI(kTag, "TWDT: <task-name> subscribed");

    for (;;) {
        if (stopRequested()) break;

        // ... work ...

        ulTaskNotifyTake(pdTRUE, kLoopDelay);
        esp_task_wdt_reset();   // feed after each sleep
    }

    esp_task_wdt_delete(nullptr);
    vTaskDelete(nullptr);
}
```

Rules:
- Call `esp_task_wdt_add(nullptr)` immediately on task entry, before any blocking call.
- Call `esp_task_wdt_reset()` at the natural checkpoint of each loop iteration — after the sleep/wait, not before.
- Call `esp_task_wdt_delete(nullptr)` before `vTaskDelete(nullptr)` so the TWDT record is cleaned up even if the task exits normally.
- Log the subscription so post-mortem logs make coverage auditable.

## Current subscription status

| Task | Subscribed | Feed point | Notes |
|------|-----------|------------|-------|
| `app_main` | ✓ | After `status_service` update, before `vTaskDelay` | Stays subscribed for lifetime |
| `air360_sensor` | ✓ | After `ulTaskNotifyTake` | 250 ms loop |
| `air360_upload` | ✓ | After `ulTaskNotifyTake`, between drained backend windows, and before/after each blocking HTTP upload request | 1 s loop; individual HTTP requests may block up to 15 s |
| `cellular` | ✓ | During setup waits, PPP monitoring, connectivity checks, backoff, and PWRKEY waits | Bounded waits replace infinite PPP blocking |
| `air360_ble` | ✓ | After sync waits and notification/timeout wakeups | 5 s advertisement refresh loop; cooperative shutdown via task notification |
| `air360_net` | ✓ | After worker notification waits | Handles reconnect, setup-AP retry, debug-window station stop, and Wi-Fi scan requests; timer callbacks only notify it |
| `esp_httpd` | ✗ IDF-managed | — | httpd has its own internal timeout handling |

## Long-blocking tasks: `wdtFeedingDelay`

`cellular_manager.cpp` includes helpers for sleeps and event waits that can exceed the TWDT timeout:

```cpp
// Sleep for total_ms while feeding the TWDT every kWdtFeedSliceMs (2 s).
void wdtFeedingDelay(std::uint32_t total_ms) {
    while (total_ms > 0U) {
        const std::uint32_t slice = std::min(total_ms, kWdtFeedSliceMs);
        vTaskDelay(pdMS_TO_TICKS(slice));
        esp_task_wdt_reset();
        total_ms -= slice;
    }
}
```

`waitEventBitsWithWatchdog()` uses the same bounded-slice pattern for PPP IP assignment and PPP lost-IP monitoring. `ConnectivityChecker` also waits for ping completion in 1 s slices and feeds TWDT for the cellular caller.

Use these helpers (or a similar pattern) whenever a task must sleep or wait longer than `timeout / 2`. Do **not** raise the TWDT timeout to accommodate long waits — that defeats the purpose.

The upload task cannot chunk `esp_http_client_perform()` internally because `UploadTransport::execute()` is synchronous. Instead, `UploadManager` feeds the TWDT immediately before and after each request. This keeps multi-request backend batches from starving the TWDT while preserving the existing per-request timeout and retry semantics.

## Adding a new task

When adding any new FreeRTOS task to the firmware:

1. Subscribe on entry with `esp_task_wdt_add(nullptr)`.
2. Feed with `esp_task_wdt_reset()` at the loop checkpoint.
3. Deregister with `esp_task_wdt_delete(nullptr)` before `vTaskDelete(nullptr)`.
4. Add the task to the table above.
5. If the task can sleep longer than 15 s, use `wdtFeedingDelay` or an equivalent chunked wait.
6. Add a checklist item to `firmware-change-checklist` if it's a new category.

## Verification

To confirm the TWDT actually fires on a real hang:

1. Insert `vTaskDelay(portMAX_DELAY)` in the target task's loop (guarded by a `#ifdef CONFIG_AIR360_WDT_TEST`).
2. Flash and connect serial monitor.
3. After the configured timeout (30 s) the panic handler should trigger and print the offending task name.
4. Device reboots.
5. Remove the test code.

## Read next

- [`startup-pipeline.md`](startup-pipeline.md) — full boot sequence and task spawn order
- [`ARCHITECTURE.md`](ARCHITECTURE.md) — runtime task map
