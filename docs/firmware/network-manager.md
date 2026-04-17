# Network Manager

## Status

Implemented. Keep this document aligned with the current Wi-Fi and SNTP runtime behavior.

## Scope

This document covers station mode, setup AP fallback, reconnect logic, connectivity checks, and SNTP synchronization handled by `NetworkManager`.

## Source of truth in code

- `firmware/main/src/network_manager.cpp`
- `firmware/main/src/connectivity_checker.cpp`
- `firmware/main/include/air360/network_manager.hpp`

## Read next

- [time.md](time.md)
- [startup-pipeline.md](startup-pipeline.md)
- [cellular-manager.md](cellular-manager.md)

This document describes the `NetworkManager` class — Wi-Fi station setup, setup AP fallback, runtime recovery after disconnects, and SNTP time synchronisation.

---

## Operating modes

```cpp
enum class NetworkMode : uint8_t {
    kOffline = 0,  // no active station uplink
    kSetupAp,      // provisioning AP is active
    kStation,      // upstream Wi-Fi connected and IP assigned
};
```

`NetworkState` carries the current Wi-Fi/runtime status that is exposed to the web UI and the Diagnostics raw JSON dump:

```cpp
struct NetworkState {
    NetworkMode mode;
    bool station_config_present;
    bool station_connect_attempted;
    bool station_connected;
    bool time_sync_attempted;
    bool time_synchronized;
    bool lab_ap_active;
    string station_ssid;
    string lab_ap_ssid;
    string ip_address;
    string last_error;
    int32_t last_disconnect_reason;
    string last_disconnect_reason_label;
    bool reconnect_backoff_active;
    uint32_t reconnect_attempt_count;
    uint64_t next_reconnect_uptime_ms;
    bool setup_ap_retry_active;
    uint64_t next_setup_ap_retry_uptime_ms;
    string time_sync_error;
    int64_t last_time_sync_unix_ms;
};
```

The state is guarded by a mutex inside `NetworkManager`; callers receive snapshots through `state()`.

---

## Boot-time network selection

`App::run()` decides step 7 like this:

```text
wifi_sta_ssid non-empty?
  ├─ YES → connectStation()
  │          ├─ SUCCESS → kStation, then SNTP sync
  │          └─ FAILURE → startLabAp() fallback
  └─ NO  → startLabAp()
```

Boot-time station failure is non-fatal. The device falls back to setup AP and continues booting.

---

## Runtime context

`NetworkManager` keeps long-lived Wi-Fi runtime resources in a file-scope `RuntimeContext`:

```cpp
struct RuntimeContext {
    EventGroupHandle_t station_events;   // BIT0=connected, BIT1=failed
    esp_netif_t* ap_netif;
    esp_netif_t* sta_netif;
    TimerHandle_t reconnect_timer;
    TimerHandle_t setup_ap_retry_timer;
    TaskHandle_t connect_attempt_task;
    esp_event_handler_instance_t wifi_handler;
    esp_event_handler_instance_t ip_handler;
    bool auto_connect_on_sta_start;
    bool reconnect_cycle_active;
    uint64_t ignore_disconnect_until_ms;
    bool wifi_initialized;
    bool sntp_initialized;
};
```

`ensureWifiInit()` now does three things once for the lifetime of the app:

1. initializes the Wi-Fi driver with `WIFI_STORAGE_RAM`
2. creates the reconnect/setup-AP retry timers
3. registers persistent `WIFI_EVENT` and `IP_EVENT_STA_GOT_IP` handlers

The handlers stay active after `connectStation()` returns, which is what enables runtime recovery.

---

## Station connection

`connectStation(config, timeout_ms=15000)` performs a synchronous station join attempt:

1. stores the latest station/AP config for future retry attempts
2. creates `sta_netif` if needed
3. applies hostname and optional static IPv4 settings
4. clears the station event bits
5. switches Wi-Fi to `WIFI_MODE_STA`
6. applies STA credentials and starts Wi-Fi
7. waits up to 15 seconds for `kStationConnectedBit` or `kStationFailedBit`

On success:

- `IP_EVENT_STA_GOT_IP` stores the station IP
- `mode` becomes `kStation`
- reconnect/setup-AP retry state is cleared
- `synchronizeTime()` runs immediately

On failure:

- `last_error` records either the disconnect reason or a station timeout
- Wi-Fi is stopped
- the caller may fall back to `startLabAp()`

Timeouts are reported as:

```text
station connect timeout (DHCP or IP assignment not completed)
```

That is the current way the firmware distinguishes “joined Wi-Fi but never became usable” from explicit disconnect reasons such as auth failure or AP not found.

---

## Runtime recovery

### Unexpected station disconnects

Persistent `WIFI_EVENT_STA_DISCONNECTED` handling now drives automatic recovery:

1. mark `station_connected = false`
2. clear the current IP
3. store `last_disconnect_reason` and `last_disconnect_reason_label`
4. if this was an unexpected station loss, increment `reconnect_attempt_count`
5. arm a one-shot reconnect timer with capped exponential backoff

Backoff sequence:

```text
10 s → 20 s → 40 s → 80 s → 160 s → 300 s (cap)
```

When the timer fires, `NetworkManager` creates a dedicated FreeRTOS task and performs another blocking `attemptStationConnect(...)`. The shared FreeRTOS timer task itself is not blocked by the 15-second station wait.

`IP_EVENT_STA_GOT_IP` resets the reconnect counter to zero and clears the backoff state.

### Setup AP retry loop

If setup AP is active and stored station credentials exist, the firmware now keeps retrying station mode in the background:

- the setup AP remains available during the wait period
- every 3 minutes a retry task attempts station association in `WIFI_MODE_APSTA`
- on success the firmware switches back to `WIFI_MODE_STA`
- on failure the AP stays up and the retry timer is armed again

This covers the common “sensor boots faster than the router” case without needing a reboot.

### Intentional stop guard

`stopStation()` and internal mode reconfiguration operations temporarily suppress disconnect handling for a short window (`ignore_disconnect_until_ms`) so deliberate `esp_wifi_stop()` / `esp_wifi_disconnect()` calls do not accidentally arm reconnect logic.

---

## Setup AP

`startLabAp(config)`:

1. stores the latest config for future background retries
2. creates both AP and STA netifs if needed
3. switches Wi-Fi to `WIFI_MODE_APSTA`
4. configures the setup AP on `192.168.4.1/24`
5. optionally preloads STA credentials into the Wi-Fi driver if they exist
6. starts Wi-Fi and disables power save
7. sets `mode = kSetupAp`, `lab_ap_active = true`
8. triggers an initial Wi-Fi scan for `/wifi-scan`
9. if station credentials exist, arms the periodic setup-AP retry timer

The AP remains the recovery surface even after a failed boot-time station join.

---

## Wi-Fi diagnostics surfaced to UI and Diagnostics raw JSON

The runtime now exposes:

- `last_error`
- `last_disconnect_reason`
- `last_disconnect_reason_label`
- `reconnect_backoff_active`
- `reconnect_attempt_count`
- `next_reconnect_uptime_ms`
- `setup_ap_retry_active`
- `next_setup_ap_retry_uptime_ms`

The Overview page uses these fields to show whether the device is:

- normally connected
- waiting for reconnect backoff to expire
- sitting in setup AP while periodic station retry is armed

The Diagnostics page raw JSON dump exposes the same fields in machine-readable form.

---

## Wi-Fi scan

`scanAvailableNetworks()` remains blocking and still requires `WIFI_MODE_STA` or `WIFI_MODE_APSTA`.

- hidden SSIDs are skipped
- duplicate SSIDs are collapsed
- failures clear `available_networks_` and populate `last_scan_error_`

---

## Time synchronisation

`synchronizeTime()` still runs only when the station is connected:

1. fail fast if `station_connected == false`
2. skip work if Unix time is already valid
3. initialize or restart SNTP
4. poll for valid time in 250 ms slices up to the supplied timeout
5. update `time_synchronized`, `time_sync_error`, and `last_time_sync_unix_ms`

`ensureStationTime(10000)` is still called from the maintenance loop when Wi-Fi is up but valid Unix time is not yet available.

SNTP is still not attempted in setup AP mode.

---

## Maintenance loop

The main task still retries SNTP periodically:

```cpp
for (;;) {
    const NetworkState network_state = network_manager.state();
    if (network_state.mode == NetworkMode::kStation &&
        network_state.station_connected &&
        !network_manager.hasValidTime()) {
        network_manager.ensureStationTime(10000);
    }

    status_service.setNetworkState(network_manager.state());
    vTaskDelay(kRuntimeMaintenanceDelay);
}
```

Wi-Fi reconnect itself no longer depends on this loop; it is driven by the persistent event handlers, timers, and retry task.

---

## State transition sketch

```text
kStation
  │ unexpected disconnect
  ▼
kOffline + reconnect backoff
  │ timer fires
  ├─ success → kStation
  └─ failure → kOffline + next backoff

kSetupAp + stored station config
  │ periodic retry timer
  ├─ success → kStation
  └─ failure → stay in kSetupAp + retry later
```

If there is no stored station configuration, the firmware does not attempt automatic station recovery.

---

## Summary of constants

| Parameter | Value |
|-----------|-------|
| Station connect timeout | 15 000 ms |
| Station wait poll slice | 250 ms |
| Reconnect base delay | 10 s |
| Reconnect cap | 300 s |
| Setup-AP retry interval | 180 s |
| SNTP poll interval | 250 ms |
| SNTP timeout (initial) | 15 000 ms |
| SNTP timeout (maintenance retry) | 10 000 ms |
| AP static IP | 192.168.4.1/24 |
| Wi-Fi storage | RAM only |
| Power save mode | `WIFI_PS_NONE` |
