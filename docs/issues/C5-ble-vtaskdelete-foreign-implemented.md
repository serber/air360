# C5 — `BleAdvertiser::stop()` kills BLE task from foreign context

- **Severity:** Critical
- **Area:** Concurrency / NimBLE lifecycle
- **Files:**
  - `firmware/main/src/ble_advertiser.cpp` (lines 112–120 for `stop()`, 130–144 for `taskMain()`)
  - `firmware/main/include/air360/ble_advertiser.hpp` (`bool enabled_`, `bool running_`)

## What is wrong

```cpp
void BleAdvertiser::stop() {
    enabled_ = false;
    running_ = false;
    ble_gap_adv_stop();
    if (task_handle_ != nullptr) {
        vTaskDelete(static_cast<TaskHandle_t>(task_handle_));
        task_handle_ = nullptr;
    }
}
```

Two independent defects:

1. **Foreign `vTaskDelete`.** The stop-caller is a task different from the BLE advertiser task. `vTaskDelete(other_task)` terminates the victim regardless of what it is doing — including while it is inside a NimBLE host call that holds internal mutexes or event-list resources.
2. **Non-atomic shared flags.** `enabled_` and `running_` are plain `bool` members written by `stop()` and read by `taskMain()`. This is a data race by the C++ memory model (and the FreeRTOS one). Compilers are free to hoist or reorder the read.

## Why it matters

- Deleting a task mid-host-call leaks or corrupts NimBLE internals. The next `start()` may hang, crash, or silently refuse to advertise.
- Non-deterministic: the failure depends on exactly where the victim task was when the caller fired.
- Config-apply flows that toggle BLE (interval change, disable/enable) exercise this code path.

## Consequences on real hardware

- Occasional boot-into-hung-BLE after a configuration change.
- Difficult to reproduce; end up with a "just power-cycle it" bug.

## Fix plan

1. **Cooperative shutdown.** The task exits itself; `stop()` just signals and waits.
2. **Make the flags atomic.**
   ```cpp
   std::atomic<bool> enabled_{false};
   std::atomic<bool> running_{false};
   ```
   Prefer `std::atomic<bool>` over `volatile`.
3. **Add a stop-acknowledge semaphore.**
   ```cpp
   SemaphoreHandle_t stop_done_ = xSemaphoreCreateBinary();
   ```
4. **Kick the task out of `vTaskDelay`.** The loop in `taskMain()` sleeps 5 s between updates. Either:
   - Use `xTaskNotifyWait` with a timeout instead of `vTaskDelay`, notify from `stop()`; or
   - Use an event group bit `kStopBit` and `xEventGroupWaitBits` for wakeup.
5. **Rewrite `stop()`:**
   ```cpp
   void BleAdvertiser::stop() {
       if (!running_.load()) return;
       enabled_.store(false);
       ble_gap_adv_stop();
       xTaskNotifyGive(task_handle_);          // wake taskMain
       xSemaphoreTake(stop_done_, pdMS_TO_TICKS(2000));
       task_handle_ = nullptr;
       // Tear down NimBLE port only after task has exited.
       nimble_port_stop();
       nimble_port_deinit();
       running_.store(false);
   }
   ```
6. **Rewrite `taskMain()`:**
   ```cpp
   void BleAdvertiser::taskMain() {
       xEventGroupWaitBits(g_sync_event, kSyncedBit, pdFALSE, pdTRUE, portMAX_DELAY);
       updateAdvertisement();
       while (enabled_.load()) {
           ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kUpdateIntervalMs));
           if (!enabled_.load()) break;
           updateAdvertisement();
       }
       xSemaphoreGive(stop_done_);
       vTaskDelete(nullptr);  // self-delete is safe
   }
   ```
7. **Subscribe the BLE task to TWDT** (see C4). Feed on each wake cycle.

## Verification

- Stress test: toggle BLE on/off 1 000 times in a loop via the web UI; no hangs, no crashes, consistent restart.
- Static check: `grep vTaskDelete firmware/main/src/ble_advertiser.cpp` shows only `vTaskDelete(nullptr)` (self-delete).
- `enabled_` and `running_` are `std::atomic<bool>` everywhere.

## Related

- C4 (watchdog audit) applies to the BLE task too.
- M8 (manual advertisement build) is adjacent but independent.

## Implemented

Implemented.
