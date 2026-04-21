# C6 — Timer daemon callbacks spawn full tasks

- **Severity:** Critical
- **Area:** Concurrency / task lifecycle
- **Files:**
  - `firmware/main/src/network_manager.cpp` (`reconnectTimerCallback`, `setupApRetryTimerCallback`, `connect_attempt_task` handle)

## What is wrong

`reconnectTimerCallback` and `setupApRetryTimerCallback` run on the FreeRTOS timer service task. Both invoke `xTaskCreate(...)` to spawn a new connect-attempt task. The only guard against overlap is a racy null-check on `connect_attempt_task`:

```cpp
if (connect_attempt_task != nullptr) return;
xTaskCreate(..., &connect_attempt_task);
```

The pointer is read and written from multiple tasks without synchronization.

## Why it matters

1. **Timer daemon is shared and stack-constrained.** Allocating tasks inside a timer callback:
   - Consumes the daemon's stack budget.
   - Holds the daemon long enough that other timers fire late or are dropped.
2. **Race on `connect_attempt_task`.** Two timers firing nearly simultaneously (different cores, rapid AP flapping) can both observe `nullptr` and both spawn. Result: two `esp_wifi_connect()` callers racing.
3. **Resource accounting.** Each orphan spawn leaks stack and TCB until the task exits; under sustained flapping this fragments the heap.

## Consequences on real hardware

- Under repeated Wi-Fi disconnect storms, the device can enter a state where multiple connect attempts overlap, producing spurious Wi-Fi driver events, missed scan results, and eventually heap exhaustion.
- Intermittent; invisible in a calm network.

## Fix plan

1. **One long-lived worker task per subsystem.** `NetworkManager` should own a single `network_worker` task created at `init()` time. All reconnect / setup-AP / scan requests are deposited into that task via notifications or a queue.
2. **Timer callbacks become notifications:**
   ```cpp
   void NetworkManager::reconnectTimerCallback(TimerHandle_t) {
       xTaskNotifyIndexed(network_worker_, 0, kReconnectReq, eSetBits);
   }
   ```
3. **The worker task centralizes state transitions:**
   ```cpp
   void NetworkManager::workerLoop() {
       esp_task_wdt_add(nullptr);
       for (;;) {
           uint32_t bits = 0;
           xTaskNotifyWaitIndexed(0, 0, ULONG_MAX, &bits, pdMS_TO_TICKS(5000));
           esp_task_wdt_reset();
           if (bits & kReconnectReq)   handleReconnect();
           if (bits & kSetupApRetry)   handleSetupApRetry();
           if (bits & kScanReq)        handleScan();
       }
   }
   ```
4. **Drop `connect_attempt_task` entirely.** There is only one worker now; no need for a separate task per attempt.
5. **Subscribe the worker to TWDT** (see C4).
6. **Document** in `network-manager.md` that timer callbacks never allocate or create tasks.
7. **Static guard.** Add a project rule (`AGENTS.md`): "Timer callbacks MUST only set flags, notify tasks, or call `xQueueSendFromISR`-style non-allocating primitives."

## Verification

- Bench: simulate 100 disconnect/reconnect cycles in 60 s. Heap high-water mark stays flat; no orphan tasks appear in `uxTaskGetSystemState`.
- Code search: `grep -rn "xTaskCreate" main/src/network_manager.cpp` yields only the worker creation.
- Timer callbacks are one-line notify calls.

## Related

- C4 (watchdog) — the new worker must be subscribed.
- M10 (`connect_attempt_task` race) is fully subsumed by this refactor.

## Implemented

Implemented.
