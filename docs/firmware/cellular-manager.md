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
    uint32_t consecutive_failures;
    uint32_t pwrkey_cycles_total;
    uint64_t last_pwrkey_uptime_ms;
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

Cellular settings are not reconfigured in place. The web UI saves `CellularConfig` together with the device config and then schedules `esp_restart()`, so the cellular task is created only during boot with the saved config. There is no runtime `stop()` path for `CellularManager`; PPP/DCE/DTE teardown happens inside `attemptConnect()` whenever a setup step fails or an established PPP session drops.

---

## FreeRTOS task

| Property | Value |
|----------|-------|
| Name | `"cellular"` |
| Stack | 8 192 bytes |
| Priority | 5 |
| Lifecycle | runs indefinitely while cellular is enabled |
| TWDT | subscribed on task entry; reset during setup waits, PPP monitoring, connectivity checks, backoff, and PWRKEY waits |

The task body is a loop that calls `attemptConnect()` and handles the outcome:

```text
loop:
  wake modem (de-assert SLEEP pin)
  attemptConnect()          ← blocks until session is fully over
  ├─ returned true  → clean disconnect; reset backoff counter; next iteration
  └─ returned false → setup failure; increment consecutive failure counters
       ├─ < 2 min continuous failure   → soft retries with table backoff
       ├─ ≥ 2 min continuous failure   → hard retry tier (full command/data cycle)
       ├─ ≥ 10 min continuous failure  → PWRKEY tier if hourly cap allows it
       └─ third PWRKEY need in window   → full ESP restart
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
| 6 | Poll for network registration every 2 s with `AT+CEREG?` / registration-state API plus CSQ fallback | return false only on denied, unknown timeout, or no non-searching registration; "searching" keeps polling |
| 7 | Set PPP authentication (PAP) if `username` non-empty | — |
| 8 | Enter PPP data mode (`ESP_MODEM_MODE_DATA`) | return false |
| 9 | Wait up to 30 s for `IP_EVENT_PPP_GOT_IP` | return false |
| 10 | PPP is up: set PPP netif as default; call `onPppConnected()` (runs connectivity check) | — |
| 11 | Monitor PPP in bounded 25 s waits; on `IP_EVENT_PPP_LOST_IP` tear down and return true; if no event arrives, run a liveness probe | — |

`teardownModem()` is called on every exit path. It unregisters event handlers, deletes the event group, destroys the DCE (best-effort `ESP_MODEM_MODE_COMMAND` exit first), and destroys the PPP netif.

---

## Connectivity check

After PPP is up, `runConnectivityCheck(host, timeout_ms=5000, retries=3)` runs:

1. If `connectivity_check_host` is empty — result is `kSkipped`; `connectivity_check_skipped = true`
2. Parse `host` as an IPv4 address (hostname resolution is not supported)
3. Send ICMP pings via `esp_ping`: 3 attempts, 5 s timeout each
4. If at least one reply received — `connectivity_ok = true`; otherwise `connectivity_ok = false`

The check result is visible in the Overview page Connection panel and in the Diagnostics raw JSON.

While PPP remains up, the cellular task also runs a liveness probe when no `IP_EVENT_PPP_LOST_IP` event has arrived for 25 s. The probe uses the same configured `connectivity_check_host`, but only sends one ICMP echo with a 1 s timeout. One failed probe is treated as inconclusive; two consecutive failed probes mark the PPP link dead, force the modem back to command mode, mark the PPP netif disconnected, publish the same disconnected state as a normal lost-IP event, and enter the reconnect loop. If `connectivity_check_host` is empty, runtime probes are skipped and the task can only react to real PPP lost-IP events.

Recovery timeline for a silent PPP failure:

| Stage | Timing |
|-------|--------|
| Native `IP_EVENT_PPP_LOST_IP` | immediate disconnect handling |
| First missing-event probe | after 25 s |
| Second consecutive failed probe | after another 25 s |
| Forced reconnect deadline | about 56 s worst case, including two 3 s probe budgets |
| TWDT feed while connected | every bounded wait/probe slice, at most 2 s apart |

The firmware watchdog target timeout is 30 s when `initWatchdog()` owns TWDT initialization. If ESP-IDF pre-initializes TWDT from `sdkconfig`, the effective timeout comes from that config; the cellular task still feeds in shorter slices.

---

## Hardware reset

PWRKEY is a last-resort escalation, not an attempt-count threshold. It is only considered after **10 minutes of continuous setup failure** and is rate-limited to **one PWRKEY cycle per hour**:

```text
PWRKEY HIGH for 3 500 ms   → modem power-off
wait 2 000 ms
PWRKEY HIGH for 2 000 ms   → modem power-on
wait 5 000 ms              → boot settling time
```

`0xFF` in the GPIO field means "not wired" — all GPIO operations are skipped for that pin. Skipped PWRKEY requests do not increment `pwrkey_cycles_total`. If the same continuous failure window reaches a third PWRKEY need, the firmware logs the escalation and calls `esp_restart()` instead of cycling the modem indefinitely.

---

## Reconnect backoff

Applied only after setup failures (not after clean disconnects). The backoff is table-driven and capped at 15 minutes:

```text
attempt 1 → 10 s
attempt 2 → 30 s
attempt 3 → 1 min
attempt 4 → 2 min
attempt 5 → 5 min
attempt 6 → 10 min
attempt 7+ → 15 min
```

Escalation is based on elapsed time in the current failure window:

- **soft retry** — initial retry tier; backoff table applies
- **hard retry** — logged once after 2 minutes of continuous failure; the next cycle performs the full command/data setup again without PWRKEY
- **PWRKEY** — logged once after 10 minutes of continuous failure if the 1-hour cap allows it
- **reboot** — if the modem would need more than two PWRKEY cycles in the same continuous failure window

During the backoff window the SLEEP pin is asserted (if wired). `next_reconnect_uptime_ms` is published to the UI during the wait.

If the modem reports registration state `2` ("not registered, searching"), the task keeps the modem alive and polls registration without treating the condition as a failure. This avoids PWRKEY cycling during carrier search or marginal-signal windows.

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
  bounded wait for IP_EVENT_PPP_LOST_IP
  ├─ drop event → ppp_connected = false; teardown; reconnect cycle
  └─ two failed liveness probes → force disconnect; teardown; reconnect cycle
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
| PPP monitor wait | 25 000 ms |
| PPP liveness probe timeout | 1 000 ms |
| PPP liveness probe retries | 1 |
| PPP liveness probe failures before forced disconnect | 2 |
| Connectivity check timeout | 5 000 ms |
| Connectivity check retries | 3 |
| Backoff base | 10 000 ms |
| Backoff table | 10 s, 30 s, 1 min, 2 min, 5 min, 10 min, 15 min |
| Hard retry escalation | 2 min continuous failure |
| PWRKEY escalation | 10 min continuous failure |
| PWRKEY frequency cap | 1 cycle per hour |
| System reboot escalation | after the second PWRKEY cycle in one continuous failure window |
| PWRKEY power-off pulse | 3 500 ms |
| PWRKEY power-on pulse | 2 000 ms |
| Modem shutdown wait | 2 000 ms |
| Modem boot wait | 5 000 ms |
| GPIO "not wired" sentinel | `0xFF` |
