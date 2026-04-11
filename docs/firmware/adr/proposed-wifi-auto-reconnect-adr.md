# Wi-Fi Auto-Reconnect ADR

## Status

Proposed.

## Decision Summary

Add automatic Wi-Fi reconnection after a station disconnect event, using a FreeRTOS timer with exponential backoff, instead of requiring a manual reboot.

## Context

The current firmware makes no reconnect attempt after `WIFI_EVENT_STA_DISCONNECTED`:

```cpp
// handleWifiEvent — current behavior
case WIFI_EVENT_STA_DISCONNECTED:
    state_.station_connected = false;
    state_.mode = NetworkMode::kOffline;
    // kStationFailedBit set → connectStation() returns → App falls back to AP or stays offline
```

Once offline, the device stays in `kOffline` until the next reboot. The upload task pauses, the maintenance loop retries SNTP (which also fails), and the device is effectively dead for measurement upload until power-cycled.

Common real-world causes: router reboot, brief ISP outage, DHCP lease renewal, momentary RF interference. All of these are transient and would resolve within seconds to minutes — but the current firmware treats them as permanent.

## Goals

- Automatically attempt reconnection after a station disconnect.
- Use backoff to avoid hammering the router on persistent failures.
- Reset to normal connected state on success (SNTP, uploads resume automatically).
- Not attempt reconnection if the device intentionally has no station config.

## Non-Goals

- Reconnecting after a deliberate `esp_wifi_stop()` call (only after unexpected disconnects).
- Reconnecting while in setup AP mode (no station config present).
- Changing the fallback-to-AP behavior on initial boot.

## Architectural Decision

### Reconnect timer

Add a FreeRTOS one-shot timer (`TimerHandle_t reconnect_timer_`) to `NetworkManager`'s `RuntimeContext`. The timer callback calls `connectStation()` with a short timeout (15 s, same as boot).

### Backoff

Track `uint32_t reconnect_attempt_count` in `RuntimeContext`. On each `WIFI_EVENT_STA_DISCONNECTED` event:

1. Increment `reconnect_attempt_count`.
2. Compute delay: `min(10s * 2^(attempt - 1), 300s)` — sequence: 10 s, 20 s, 40 s, 80 s, 160 s, 300 s (capped).
3. Start or reset the one-shot timer with the computed delay.

Reset `reconnect_attempt_count` to `0` on `IP_EVENT_STA_GOT_IP`.

### Timer callback

The callback runs on the FreeRTOS timer task (not the main task). It calls `NetworkManager::connectStation()` — which is already safe to call from any task. On success: IP event fires, state updates to `kStation`, uploads and SNTP resume automatically through existing code paths. On failure: `WIFI_EVENT_STA_DISCONNECTED` fires again, incrementing the counter and scheduling the next attempt.

### Guard: only reconnect if station config is present

Before starting the timer, check that `station_config_present` is true in `NetworkState`. If the device has no Wi-Fi credentials it should never attempt station reconnection.

## State transition update

```
kStation
  │ DISCONNECTED event
  ▼
kOffline + reconnect timer armed
  │ timer fires → connectStation()
  ├─ SUCCESS → kStation (reconnect_count reset)
  └─ FAILURE → kOffline + timer rescheduled (backoff)
```

## Affected Files

- `firmware/main/src/network_manager.cpp` — add `reconnect_timer_` and `reconnect_attempt_count` to `RuntimeContext`, update `handleWifiEvent` for `WIFI_EVENT_STA_DISCONNECTED`, add timer callback
- `firmware/main/include/air360/network_manager.hpp` — no public API change required

## Alternatives Considered

### Option A. No reconnect (current state)

Simple. Requires manual reboot on any disconnect. Unacceptable for unattended deployments.

### Option B. Immediate reconnect in event handler

Call `esp_wifi_connect()` directly from the disconnect handler. Fast but runs on the Wi-Fi event task, blocks it, and has no backoff — hammers the router on persistent failure.

### Option C. FreeRTOS timer with backoff (accepted)

Clean separation from event handler. Backoff prevents router flooding. Integrates naturally with the existing `connectStation()` flow.

## Practical Conclusion

Add a one-shot FreeRTOS reconnect timer triggered by `WIFI_EVENT_STA_DISCONNECTED`. Back off exponentially up to 300 s. Reset counter on successful IP assignment. The change is fully contained in `network_manager.cpp` with no API changes required.
