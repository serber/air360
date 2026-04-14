# Wi-Fi Recovery And Auto-Reconnect ADR

## Status

Proposed.

## Decision Summary

Extend the current Wi-Fi runtime so the device can recover from transient network failures without requiring a manual reboot.

The first version should add:

- automatic station reconnect after unexpected disconnects
- periodic station retry while setup AP is active after a failed boot-time station connect
- bounded retry backoff instead of tight reconnect loops
- clearer Wi-Fi failure diagnostics in the UI and `/status`

This ADR intentionally focuses on unattended recovery, not on captive portal or enterprise-auth support.

## Context

The current firmware documents two behaviors that are acceptable for lab bring-up but weak for unattended deployments:

1. no reconnect attempt after `WIFI_EVENT_STA_DISCONNECTED`
2. no automatic retry path once the device has fallen back to setup AP

The current disconnect behavior is:

```cpp
// handleWifiEvent — current behavior
case WIFI_EVENT_STA_DISCONNECTED:
    state_.station_connected = false;
    state_.mode = NetworkMode::kOffline;
    // kStationFailedBit set → connectStation() returns → App falls back to AP or stays offline
```

Once offline, the device stays in `kOffline` until the next reboot. The upload task pauses, the maintenance loop retries SNTP (which also fails), and the device is effectively dead for measurement upload until power-cycled.

On initial boot, the documented sequence is also rigid:

- station connect attempt
- failure
- fallback to setup AP
- remain there until the user intervenes or power-cycles the device

This directly overlaps with long-running Sensor.Community pain points:

- [#851 Add the function of finding a Wi-Fi network for some time](https://github.com/opendata-stuttgart/sensors-software/issues/851)
- [#841 Network re-connecting](https://github.com/opendata-stuttgart/sensors-software/issues/841)
- [#817 Hotspot mode timeout](https://github.com/opendata-stuttgart/sensors-software/issues/817)
- [PR #1042 Patch/better wifi debug](https://github.com/opendata-stuttgart/sensors-software/pull/1042)
- [PR #947 Make WiFi maximum TX power configurable](https://github.com/opendata-stuttgart/sensors-software/pull/947)

Common real-world causes: router reboot, brief ISP outage, DHCP lease renewal, momentary RF interference. All of these are transient and would resolve within seconds to minutes — but the current firmware treats them as permanent.

## Goals

- Automatically attempt reconnection after a station disconnect.
- Retry station mode from setup AP when credentials exist but upstream Wi-Fi was unavailable during boot.
- Use backoff to avoid hammering the router on persistent failures.
- Reset to normal connected state on success (SNTP, uploads resume automatically).
- Improve visibility of why Wi-Fi is failing.
- Not attempt station reconnect if the device intentionally has no station config.

## Non-Goals

- Reconnecting after a deliberate `esp_wifi_stop()` call (only after unexpected disconnects).
- WPA2-Enterprise support.
- Captive portal acceptance flows.
- Reworking the entire Wi-Fi settings model in the first version.

## Architectural Decision

### 1. Reconnect timer for unexpected disconnects

Add a FreeRTOS one-shot timer (`TimerHandle_t reconnect_timer_`) to `NetworkManager`'s `RuntimeContext`. The timer callback calls `connectStation()` with a short timeout (15 s, same as boot).

### 2. Backoff

Track `uint32_t reconnect_attempt_count` in `RuntimeContext`. On each `WIFI_EVENT_STA_DISCONNECTED` event:

1. Increment `reconnect_attempt_count`.
2. Compute delay: `min(10s * 2^(attempt - 1), 300s)` — sequence: 10 s, 20 s, 40 s, 80 s, 160 s, 300 s (capped).
3. Start or reset the one-shot timer with the computed delay.

Reset `reconnect_attempt_count` to `0` on `IP_EVENT_STA_GOT_IP`.

### 3. Timer callback

The callback runs on the FreeRTOS timer task (not the main task). It calls `NetworkManager::connectStation()` — which is already safe to call from any task. On success: IP event fires, state updates to `kStation`, uploads and SNTP resume automatically through existing code paths. On failure: `WIFI_EVENT_STA_DISCONNECTED` fires again, incrementing the counter and scheduling the next attempt.

### 4. Guard: only reconnect if station config is present

Before starting the timer, check that `station_config_present` is true in `NetworkState`. If the device has no Wi-Fi credentials it should never attempt station reconnection.

### 5. Setup AP retry loop when station config exists

If the device is in `kSetupAp` but persisted station credentials are present, it should periodically retry station connection in the background instead of waiting forever for a reboot.

The first version should:

- keep the setup AP available for recovery and provisioning
- perform a bounded retry every few minutes while AP is active
- stop the retry loop immediately when station mode succeeds

This closes the “router boots slower than sensor” failure mode without sacrificing setup access.

### 6. User-facing Wi-Fi diagnostics

The runtime status already carries `last_error`. Extend the presentation so users can distinguish:

- authentication failure
- AP not found
- DHCP timeout
- disconnect reason code
- reconnect backoff currently active

The Overview page and `/status` should make the recovery state visible instead of just showing `offline`.

### 7. Optional follow-up knobs

This ADR keeps room for two small follow-ups that match upstream requests and do not require a design reset:

- configurable Wi-Fi TX power
- a temporary debug window with more detailed connection diagnostics

Those can reuse the same runtime status model and do not need to block the reconnect work.

## State transition update

```
kStation
  │ DISCONNECTED event
  ▼
kOffline + reconnect timer armed
  │ timer fires → connectStation()
  ├─ SUCCESS → kStation (reconnect_count reset)
  └─ FAILURE → kOffline + timer rescheduled (backoff)

kSetupAp + stored station config present
  │ periodic retry timer fires
  ├─ SUCCESS → kStation
  └─ FAILURE → remain in kSetupAp + retry later
```

## Affected Files

- `firmware/main/src/network_manager.cpp` — add reconnect and station-retry timers, retry counters, richer disconnect diagnostics, and setup-AP retry path
- `firmware/main/include/air360/network_manager.hpp` — no public API change required
- `firmware/main/src/app.cpp` — if needed, surface recovery state more explicitly through the maintenance loop and status updates
- `firmware/main/src/status_service.cpp`
- `firmware/main/src/web_ui.cpp`

## Alternatives Considered

### Option A. No reconnect (current state)

Simple. Requires manual reboot on any disconnect. Unacceptable for unattended deployments.

### Option B. Immediate reconnect in event handler

Call `esp_wifi_connect()` directly from the disconnect handler. Fast but runs on the Wi-Fi event task, blocks it, and has no backoff — hammers the router on persistent failure.

### Option C. Reconnect after disconnect only

Better than current behavior, but still leaves the device trapped in setup AP when the router was just slow to boot.

### Option D. Timers plus setup-AP retry loop (accepted)

Clean separation from the event handler. Backoff prevents router flooding. The additional retry loop while setup AP is active covers the most common unattended boot race.

## Reference Links

- [Sensor.Community ecosystem review for Air360](../../ecosystem/sensor-community-issues-prs-review-2026-04-14.md)
- [Network Manager](../network-manager.md)
- [Web UI](../web-ui.md)
- [Implemented static IP configuration ADR](implemented-static-ip-configuration-adr.md)
- [#851 Add the function of finding a Wi-Fi network for some time](https://github.com/opendata-stuttgart/sensors-software/issues/851)
- [#841 Network re-connecting](https://github.com/opendata-stuttgart/sensors-software/issues/841)
- [#817 Hotspot mode timeout](https://github.com/opendata-stuttgart/sensors-software/issues/817)
- [PR #1042 Patch/better wifi debug](https://github.com/opendata-stuttgart/sensors-software/pull/1042)
- [PR #947 Make WiFi maximum TX power configurable](https://github.com/opendata-stuttgart/sensors-software/pull/947)

## Practical Conclusion

Air360 should move from one-shot Wi-Fi behavior to a recovery-oriented model:

- reconnect after unexpected disconnects
- retry station mode while setup AP remains available
- show users what the device is waiting for

That closes one of the most repeated ecosystem pain points without changing the provisioning model fundamentally.
