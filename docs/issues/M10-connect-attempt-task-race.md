# M10 — `connect_attempt_task` handle read without synchronization

- **Severity:** Medium (subsumed by C6 if that refactor is done)
- **Area:** Concurrency / data race
- **Files:**
  - `firmware/main/src/network_manager.cpp` (`connect_attempt_task`, read in timer callbacks, written on task exit)

## What is wrong

`connect_attempt_task` is a plain `TaskHandle_t`. It is read in timer callbacks to decide whether to spawn a new connect attempt, and set to nullptr on task exit — without synchronization.

A timer callback and a task-exit can race. Two timers on different cores can both observe `nullptr`.

## Why it matters

- Two concurrent connect-attempt tasks may be spawned, each calling `esp_wifi_connect`.
- The Wi-Fi driver gets stacked events; state-machine behavior is undefined.

## Consequences on real hardware

- Rare; manifests under rapid Wi-Fi flapping.
- Logs show duplicate connect-attempt messages.

## Fix plan

**Preferred:** complete the C6 refactor (one persistent worker task, timers notify, no dynamic task creation). This issue disappears entirely.

**Interim, if C6 is delayed:**
1. Make the handle `std::atomic<TaskHandle_t>` (or wrap with a mutex).
2. Use compare-exchange:
   ```cpp
   TaskHandle_t expected = nullptr;
   if (!connect_attempt_task_.compare_exchange_strong(expected, reinterpret_cast<TaskHandle_t>(1))) {
       return;  // someone else is spawning
   }
   TaskHandle_t h = nullptr;
   if (xTaskCreate(..., &h) == pdPASS) {
       connect_attempt_task_.store(h);
   } else {
       connect_attempt_task_.store(nullptr);
   }
   ```
3. On task exit, `connect_attempt_task_.store(nullptr)` is atomic.

## Verification

- Stress test: trigger rapid disconnect/reconnect 100×; exactly one connect-attempt task at any time.
- After C6 refactor: `grep connect_attempt_task` returns no results.

## Related

- C6 — the proper fix.
