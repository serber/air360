# Finding F01: Timer callbacks perform blocking work

## Status

Implemented.

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
**Files / symbols:** `firmware/main/src/app.cpp` (`debugWindowCallback`), `firmware/main/src/web/web_mutating_routes.cpp` (`scheduleRestart`, `restartTask`), `NetworkManager::requestStopStation`, `NetworkManager::stopStation`

## Problem

The firmware used ESP timer callbacks for operations that are not timer-safe:

- `debugWindowCallback()` called `NetworkManager::stopStation()`.
- `restartCallback()` called `esp_restart()` directly.

`NetworkManager::stopStation()` stops FreeRTOS timers, calls Wi-Fi APIs, and takes the network mutex. The project rule says timer callbacks may only set flags, notify existing tasks, or post non-blocking queue events.

## Why it matters

ESP timer callbacks run in the timer service context. Blocking, taking application mutexes, or running Wi-Fi shutdown from that context can stall the timer service, deadlock with network code, or create watchdog resets. Calling `esp_restart()` directly from a timer callback also bypasses orderly shutdown and makes future teardown hooks unsafe.

## Evidence

Original behavior:

- `debugWindowCallback(void* arg)` cast `arg` to `NetworkManager*` and called `nm->stopStation()`.
- `NetworkManager::stopStation()` stops FreeRTOS timers, calls Wi-Fi APIs, and updates shared state under a mutex.
- `restartCallback()` called `esp_restart()` directly from an ESP timer callback.

Current behavior:

- `debugWindowCallback()` calls only `NetworkManager::requestStopStation()`.
- `requestStopStation()` notifies the existing `air360_net` worker with `kWorkerStopStationReq`.
- `air360_net` handles `kWorkerStopStationReq` by calling `NetworkManager::stopStation()` in task context.
- Config-save reboot is scheduled by creating a short one-shot `air360_reboot` task from the HTTP handler after the response has been sent. That task delays briefly, then calls `esp_restart()`.

## Implemented Fix

Both operations were moved out of ESP timer callbacks:

- Wi-Fi debug-window expiry now posts a worker request bit and returns immediately.
- Blocking Wi-Fi shutdown runs on the existing TWDT-subscribed `air360_net` worker.
- Config-save reboot no longer uses an ESP timer; it runs from a short one-shot reboot task created by the HTTP handler.

## Where Changed

- `firmware/main/include/air360/network_manager.hpp`
- `firmware/main/src/network_manager.cpp`
- `firmware/main/src/app.cpp`
- `firmware/main/src/web/web_mutating_routes.cpp`
- `docs/firmware/startup-pipeline.md`
- `docs/firmware/network-manager.md`
- `docs/firmware/web-ui.md`
- `docs/firmware/watchdog.md`
- `docs/firmware/ARCHITECTURE.md`

## Implementation Notes

Code changes:

- `firmware/main/include/air360/network_manager.hpp`
- `firmware/main/src/network_manager.cpp`
- `firmware/main/src/app.cpp`
- `firmware/main/src/web/web_mutating_routes.cpp`

The reboot task is intentionally short-lived and is not subscribed to TWDT because it sleeps for 400 ms and then calls `esp_restart()`.

```cpp
// app.cpp timer callback
void debugWindowCallback(void* arg) {
    auto* nm = static_cast<NetworkManager*>(arg);
    if (nm != nullptr) {
        nm->requestStopStation();
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
