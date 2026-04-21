# C3 — Cellular reconnect blocks on `portMAX_DELAY` with no watchdog

- **Severity:** Critical
- **Area:** Concurrency / reliability
- **Files:**
  - `firmware/main/src/cellular_manager.cpp` (lines ~339-340 in the `taskBody` reconnect loop)

## What is wrong

After PPP comes up, the cellular task blocks indefinitely waiting for `kLostIpBit`:

```cpp
xEventGroupWaitBits(
    ppp_event_group_, kLostIpBit, pdTRUE, pdFALSE, portMAX_DELAY);
```

- No timeout.
- The task is not subscribed to the task watchdog.
- No liveness probe runs while waiting.

## Why it matters

`esp_modem` / the SIM7600E can end up in states where PPP is de-facto dead but no `LOST_IP` event fires: stuck UART, DCE firmware hiccup, carrier silent disconnect (very common on flaky 2G/LTE fallback), or an internal `esp_modem` bug that swallows the event.

When that happens:
- The cellular task stays blocked forever.
- `CellularManager::state()` still reports "connected" because the state machine never moves.
- `NetworkManager` may also keep the PPP netif as default.
- Uplink is silently dead; device looks healthy.

## Consequences on real hardware

- The single most common production failure mode for cellular-uplink devices.
- No automatic recovery — requires a user power-cycle.
- Fleet health dashboards show "last seen hours ago" with no error signature.

## Fix plan

1. **Replace the infinite wait with a bounded, periodic wait** (e.g. 30 s):
   ```cpp
   esp_task_wdt_add(nullptr);
   for (;;) {
       EventBits_t bits = xEventGroupWaitBits(
           ppp_event_group_, kLostIpBit,
           pdTRUE, pdFALSE,
           pdMS_TO_TICKS(30'000));
       esp_task_wdt_reset();

       if (bits & kLostIpBit) {
           break;  // handler already ran; exit loop to reconnect
       }
       if (!probeLink()) {
           ESP_LOGW(kTag, "Probe failed, forcing reconnect");
           forceDisconnect();
           break;
       }
   }
   esp_task_wdt_delete(nullptr);
   ```
2. **Implement `probeLink()`.** Cheapest option: trigger the existing `ConnectivityChecker` against the configured host, treat single-failure as inconclusive, two-failures as dead. Alternative: modem AT heartbeat (`AT` OK within 2 s).
3. **Implement `forceDisconnect()`.** Return the DCE to command mode (`esp_modem_set_mode(ESP_MODEM_MODE_COMMAND)`), mark netif down, post `kLostIpBit` internally to unify downstream cleanup.
4. **Subscribe to the task watchdog.** `esp_task_wdt_add(nullptr)` on entry, `esp_task_wdt_reset()` on every wait cycle, `esp_task_wdt_delete(nullptr)` on exit. Reuse the pattern for the reconnect attempt loop.
5. **Audit the rest of `cellular_manager.cpp`** for other `portMAX_DELAY` uses. Wrap them all.
6. **Document the recovery timeline** in `cellular-manager.md`: probe cadence, max time before forced disconnect, TWDT timeout.

## Verification

- Bench test: kill the DTE UART pins mid-PPP, assert the task recovers within one probe cycle (≤ 60 s).
- Soak: 48 h cellular session with artificially injected silent drops; the task should attempt reconnect every time instead of hanging.
- Fleet monitor: alert on any device reporting `last_upload_ms > 5 min && cellular_state == connected`.

## Related

- C4 (watchdog coverage gap) — fixing this issue partially addresses C4 for the cellular task.
- H8 (PWRKEY cadence) — once this loop unblocks reliably, the PWRKEY escalation becomes meaningful again.

## implemented

Implementation status:

- Fix plan 1 — implemented. The PPP connected state no longer waits with `portMAX_DELAY`; it waits in bounded 25 s windows, feeds TWDT, and exits on `kLostIpBit`.
- Fix plan 2 — implemented. `probeLink()` uses the existing `ConnectivityChecker` against `connectivity_check_host`; one failed probe is inconclusive and two consecutive failed probes mark the link dead.
- Fix plan 3 — implemented. `forceDisconnect()` returns the DCE to command mode, marks the PPP netif disconnected, publishes the same disconnected state as a normal PPP loss, and posts `kLostIpBit`.
- Fix plan 4 — implemented. The cellular task subscribes to TWDT on entry and feeds it during setup waits, PPP monitoring, connectivity checks, reconnect backoff, and hardware reset waits.
- Fix plan 5 — implemented. `cellular_manager.cpp` no longer contains `portMAX_DELAY`.
- Fix plan 6 — implemented. `docs/firmware/cellular-manager.md`, `docs/firmware/sensors/sim7600e.md`, and `docs/firmware/watchdog.md` document the recovery timeline and watchdog behavior.

Verification performed in code/docs:

- Static audit: `rg "portMAX_DELAY" firmware/main/src/cellular_manager.cpp firmware/main/include/air360/cellular_manager.hpp` returns no matches.
- Pending hardware verification: bench UART-kill test, 48 h injected-drop soak, and fleet alert validation require real device/lab access.
