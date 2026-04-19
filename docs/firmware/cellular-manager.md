# Cellular Manager

## Status

Implemented. Keep this document aligned with the current modem runtime and cellular config model.

## Scope

This document covers SIM7600E modem bring-up, PPP lifecycle, modem GPIO control, reconnect behavior, and the cellular runtime state exposed by the firmware.

## Source of truth in code

- `firmware/main/src/cellular_manager.cpp`
- `firmware/main/src/cellular_config_repository.cpp`
- `firmware/main/src/modem_gpio.cpp`
- `firmware/main/include/air360/cellular_manager.hpp`

## Read next

- [network-manager.md](network-manager.md)
- [configuration-reference.md](configuration-reference.md)
- [sensors/sim7600e.md](sensors/sim7600e.md)

This document describes the `CellularManager` class — SIM7600E modem lifecycle, PPP session management, hardware GPIO control, runtime reconnect logic, and connectivity verification.

---

## Runtime state

```cpp
struct CellularState {
    bool enabled;
    bool modem_detected;
    bool sim_ready;
    bool registered;
    bool ppp_connected;
    bool connectivity_ok;
    bool connectivity_check_skipped;
    string ip_address;
    int rssi_dbm;
    string last_error;
    uint32_t reconnect_attempts;
    uint64_t next_reconnect_uptime_ms;
};
```

The state is owned by `CellularManager` and exposed via `state()`. The struct is updated from both the cellular task and PPP event handlers under an internal mutex, and callers receive a copied snapshot instead of a live reference.

---

## Initialisation and start

Two-phase setup:

1. `init(network_manager)` — wires up the uplink bearer sink so `CellularManager` can notify `NetworkManager` when PPP comes up or drops.
2. `start(config)` — reads `CellularConfig`, initialises modem GPIOs (PWRKEY / SLEEP / RESET), and — if `config.enabled == 1` — spawns the reconnect task. Calling `start()` more than once is a no-op.

When `config.enabled == 0` the modem GPIOs are still configured (idle LOW) but no task is created and no UART traffic is generated.

---

## FreeRTOS task

| Property | Value |
|----------|-------|
| Name | `"cellular"` |
| Stack | 8 192 bytes |
| Priority | 5 |
| Lifecycle | runs indefinitely while cellular is enabled |

The task body is a loop that calls `attemptConnect()` and handles the outcome:

```text
loop:
  wake modem (de-assert SLEEP pin)
  attemptConnect()          ← blocks until session is fully over
  ├─ returned true  → clean disconnect; reset backoff counter; next iteration
  └─ returned false → setup failure; increment reconnect_attempts
       ├─ attempts < 5  → compute backoff; assert SLEEP; wait; de-assert SLEEP; next iteration
       └─ attempts ≥ 5  → hardware reset (PWRKEY pulse); reset counter; next iteration
```

---

## PPP session lifecycle (`attemptConnect`)

`attemptConnect()` is a synchronous 11-step sequence that blocks from the first AT command until the PPP session is fully torn down:

| Step | Action | Failure outcome |
|------|--------|-----------------|
| 1 | Allocate PPP `esp_netif` | return false |
| 2 | Configure UART DTE (port, baud, TX/RX pins, no flow control) | — |
| 3 | Create SIM7600E DCE via `esp_modem_new_dev` | return false |
| 4 | Allocate PPP event group; register `IP_EVENT_PPP_GOT_IP` / `IP_EVENT_PPP_LOST_IP` handlers | return false |
| 5 | SIM PIN unlock (if `sim_pin` non-empty; skipped if PIN not required) | return false |
| 6 | Poll for network registration: check signal quality every 2 s, up to 60 s | return false |
| 7 | Set PPP authentication (PAP) if `username` non-empty | — |
| 8 | Enter PPP data mode (`ESP_MODEM_MODE_DATA`) | return false |
| 9 | Wait up to 30 s for `IP_EVENT_PPP_GOT_IP` | return false |
| 10 | PPP is up: set PPP netif as default; call `onPppConnected()` (runs connectivity check) | — |
| 11 | Block indefinitely on `IP_EVENT_PPP_LOST_IP`; then teardown and return true | — |

`teardownModem()` is called on every exit path. It unregisters event handlers, deletes the event group, destroys the DCE (best-effort `ESP_MODEM_MODE_COMMAND` exit first), and destroys the PPP netif.

---

## Connectivity check

After PPP is up, `runConnectivityCheck(host, timeout_ms=5000, retries=3)` runs:

1. If `connectivity_check_host` is empty — result is `kSkipped`; `connectivity_check_skipped = true`
2. Parse `host` as an IPv4 address (hostname resolution is not supported)
3. Send ICMP pings via `esp_ping`: 3 attempts, 5 s timeout each
4. If at least one reply received — `connectivity_ok = true`; otherwise `connectivity_ok = false`

The check result is visible in the Overview page Connection panel and in the Diagnostics raw JSON.

---

## Hardware reset

After `kMaxReconnectAttempts` (5) consecutive setup failures the modem is hard-reset via PWRKEY:

```text
PWRKEY HIGH for 3 500 ms   → modem power-off
wait 2 000 ms
PWRKEY HIGH for 2 000 ms   → modem power-on
wait 5 000 ms              → boot settling time
```

`0xFF` in the GPIO field means "not wired" — all GPIO operations are skipped for that pin. After a hardware reset the reconnect counter is zeroed and the next `attemptConnect()` runs immediately (no additional backoff sleep).

---

## Reconnect backoff

Applied only after setup failures (not after clean disconnects):

```text
attempt 1 → 10 s
attempt 2 → 20 s
attempt 3 → 40 s
attempt 4 → 80 s
attempt 5+ → hardware reset (no sleep)
```

Cap: 300 s (unreachable with the current 5-attempt limit before reset).

During the backoff window the SLEEP pin is asserted (if wired). `next_reconnect_uptime_ms` is published to the UI during the wait.

---

## NetworkManager integration

`CellularManager` calls two `NetworkManager` methods:

| Method | When called |
|--------|-------------|
| `setCellularStatus(true, ip)` | PPP is up and IP is assigned |
| `setCellularStatus(false, nullptr)` | PPP link dropped (`IP_EVENT_PPP_LOST_IP`) |

`NetworkManager` uses these calls to update the uplink bearer in `StatusService` so the Overview page Uplink stat reflects the cellular link. When PPP drops, `NetworkManager` also restores the Wi-Fi station netif as the default netif if one is available.

---

## Default netif management

On PPP connect: `esp_netif_set_default_netif(ppp_netif_)` — all outgoing traffic (SNTP, uploads) goes through cellular.

On PPP disconnect: the teardown code looks up `"WIFI_STA_DEF"` via `esp_netif_get_handle_from_ifkey()` and restores it as default if present.

---

## State transition sketch

```text
[disabled]  config.enabled == 0 → no task, no UART traffic

[enabled, not connected]
  attemptConnect() running
  ├─ success → ppp_connected = true; connectivity check
  └─ failure → backoff or hardware reset

[connected]
  blocking on IP_EVENT_PPP_LOST_IP
  └─ drop → ppp_connected = false; teardown; reconnect cycle
```

---

## Summary of constants

| Parameter | Value |
|-----------|-------|
| Task stack | 8 192 bytes |
| Task priority | 5 |
| Registration poll interval | 2 000 ms |
| Registration timeout | 60 000 ms |
| PPP IP assignment timeout | 30 000 ms |
| Connectivity check timeout | 5 000 ms |
| Connectivity check retries | 3 |
| Backoff base | 10 000 ms |
| Backoff cap | 300 000 ms |
| Max attempts before HW reset | 5 |
| PWRKEY power-off pulse | 3 500 ms |
| PWRKEY power-on pulse | 2 000 ms |
| Modem shutdown wait | 2 000 ms |
| Modem boot wait | 5 000 ms |
| GPIO "not wired" sentinel | `0xFF` |
