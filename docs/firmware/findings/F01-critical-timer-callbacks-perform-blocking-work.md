# Finding F01: Timer callbacks perform blocking work

## Status

Confirmed audit finding. Not implemented.

## Scope

Task file for fixing timer-callback misuse in firmware runtime control paths.

## Source of truth in code

- `firmware/main/src/app.cpp`
- `firmware/main/src/web/web_mutating_routes.cpp`
- `firmware/main/src/network_manager.cpp`

## Read next

- `docs/firmware/watchdog.md`
- `docs/firmware/network-manager.md`
- `docs/firmware/web-ui.md`

**Priority:** Critical
**Category:** FreeRTOS / ESP-IDF / Reliability
**Files / symbols:** `firmware/main/src/app.cpp` (`debugWindowCallback`), `firmware/main/src/web/web_mutating_routes.cpp` (`restartCallback`, `scheduleRestart`), `NetworkManager::stopStation`

## Problem

The firmware uses ESP timer callbacks for operations that are not timer-safe:

- `debugWindowCallback()` calls `NetworkManager::stopStation()`.
- `restartCallback()` calls `esp_restart()` directly.

`NetworkManager::stopStation()` stops FreeRTOS timers, calls Wi-Fi APIs, and takes the network mutex. The project rule says timer callbacks may only set flags, notify existing tasks, or post non-blocking queue events.

## Why it matters

ESP timer callbacks run in the timer service context. Blocking, taking application mutexes, or running Wi-Fi shutdown from that context can stall the timer service, deadlock with network code, or create watchdog resets. Calling `esp_restart()` directly from a timer callback also bypasses orderly shutdown and makes future teardown hooks unsafe.

## Evidence

- `firmware/main/src/app.cpp:87`:
  - `debugWindowCallback(void* arg)` casts `arg` to `NetworkManager*` and calls `nm->stopStation()`.
- `firmware/main/src/app.cpp:354`:
  - `timer_args.callback = debugWindowCallback`.
- `firmware/main/src/network_manager.cpp:1469`:
  - `NetworkManager::stopStation()` calls `stopTimerIfRunning()`, `esp_wifi_disconnect()`, `esp_wifi_stop()`, and updates shared state under a mutex.
- `firmware/main/src/web/web_mutating_routes.cpp:40`:
  - `restartCallback()` calls `esp_restart()`.
- `firmware/main/src/web/web_mutating_routes.cpp:50`:
  - `scheduleRestart()` installs `restartCallback` as an ESP timer callback.

## Recommended Fix

Move both operations out of ESP timer callbacks:

- For Wi-Fi debug-window expiry, notify the existing `air360_net` worker task with a new request bit such as `kWorkerStopStationReq`.
- For reboot, notify the main app task or a small existing control task to perform `esp_restart()` after the HTTP response has drained.

## Where To Change

- `firmware/main/include/air360/network_manager.hpp`
- `firmware/main/src/network_manager.cpp`
- `firmware/main/src/app.cpp`
- `firmware/main/src/web/web_mutating_routes.cpp`
- `docs/firmware/startup-pipeline.md`
- `docs/firmware/network-manager.md`
- `docs/firmware/web-ui.md`
- `docs/firmware/watchdog.md` if a new long-lived task is introduced

## How To Change

1. Add a non-blocking `NetworkManager::requestStopStation()` method that only notifies `air360_net`.
2. Add `kWorkerStopStationReq` and handle it inside `NetworkManager::workerLoop()`.
3. Change `debugWindowCallback()` so it only calls `requestStopStation()`.
4. Replace the reboot timer callback with a notification to a task context, or use `httpd_queue_work()` if the work only needs to be serialized after response send and is safe from that context.
5. Keep the actual `esp_restart()` outside the timer callback.

## Example Fix

```cpp
// app.cpp timer callback
void debugWindowCallback(void* arg) {
    auto* nm = static_cast<NetworkManager*>(arg);
    if (nm != nullptr) {
        nm->requestStopStationFromTimer();
    }
}

// network worker
if ((bits & kWorkerStopStationReq) != 0U) {
    static_cast<void>(stopStation());
}
```

## Validation

- Enable cellular debug Wi-Fi with a short window, for example 5 seconds.
- Confirm the timer callback only notifies and returns.
- Confirm `air360_net` performs the station stop.
- Run `source ~/.espressif/v6.0/esp-idf/export.sh && idf.py build` from `firmware/`.
- Run `python3 scripts/check_firmware_docs.py`.
- Hardware test: let the debug window expire while Wi-Fi is connecting and while it is already connected; verify no WDT reset.

## Risk Of Change

Medium. The behavior is simple, but network state transitions are concurrency-sensitive.

## Dependencies

None.

## Suggested Agent Type

FreeRTOS agent / ESP-IDF agent
