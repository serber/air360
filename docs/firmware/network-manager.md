# Network Manager

## Status

Implemented. Keep this document aligned with the current Wi-Fi and SNTP runtime behavior.

## Scope

This document covers station mode, setup AP fallback, reconnect logic, connectivity checks, SNTP synchronization, and mDNS local discovery handled by `NetworkManager`.

## Source of truth in code

- `firmware/main/src/network_manager.cpp`
- `firmware/main/src/connectivity_checker.cpp`
- `firmware/main/include/air360/network_manager.hpp`
- `firmware/main/include/air360/tuning.hpp`

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

The state is guarded by a mutex inside `NetworkManager`; callers receive copies through `state()`. The Wi-Fi scan cache is exposed separately via `wifiScanSnapshot()`, which returns the SSID list together with `last_scan_error` and `last_scan_uptime_ms` as one consistent snapshot.

---

## Boot-time network selection

`NetworkLayer::bootWifi` (boot step 9, called from `App::run()`) decides the network mode from the cellular and Wi-Fi config:

```text
cellular_config.enabled != 0?
  ├─ YES → cellular is primary uplink
  │    ├─ wifi_sta_ssid non-empty? → connectStation() for boot debug window only
  │    └─ no setup AP fallback from NetworkManager
  └─ NO  → Wi-Fi/setup-AP flow
       ├─ wifi_sta_ssid non-empty? → connectStation()
       │    ├─ SUCCESS → kStation, then SNTP sync
       │    └─ FAILURE → startLabAp() fallback
       └─ no station config → startLabAp()
```

Boot-time station failure is non-fatal. In Wi-Fi-primary mode the device falls back to setup AP and continues booting. In cellular-primary mode the cellular manager owns the permanent uplink; Wi-Fi station is only an optional temporary diagnostics window.

If `wifi_debug_window_s > 0` in cellular-primary mode, `App` arms a one-shot ESP timer after the boot-time station join. The timer callback only notifies the existing `air360_net` worker through `NetworkManager::requestStopStation()`. The worker then performs the blocking station shutdown (`stopStation()`) in task context.

---

## Runtime context

`NetworkManager` keeps long-lived Wi-Fi runtime resources in an instance-owned `RuntimeContext`:

```cpp
struct RuntimeContext {
    EventGroupHandle_t station_events;   // BIT0=connected, BIT1=failed
    esp_netif_t* ap_netif;
    esp_netif_t* sta_netif;
    TimerHandle_t reconnect_timer;
    TimerHandle_t setup_ap_retry_timer;
    TaskHandle_t worker_task;
    SemaphoreHandle_t scan_request_mutex;
    SemaphoreHandle_t scan_done;
    esp_event_handler_instance_t wifi_handler;
    esp_event_handler_instance_t ip_handler;
    bool handlers_registered;
    bool auto_connect_on_sta_start;
    bool reconnect_cycle_active;
    uint64_t ignore_disconnect_until_ms;
    bool wifi_initialized;
    bool sntp_initialized;
    bool mdns_initialized;
};
```

The context is a private field of `NetworkManager`, so RTOS handles and callback state belong to the same object as the public network state. ESP-IDF event callbacks and FreeRTOS timer callbacks remain static functions, but their callback argument or timer ID points back to the owning `NetworkManager` instance.

`worker_task` is the single long-lived `air360_net` FreeRTOS task for blocking Wi-Fi recovery, station-stop, and scan work. Timer callbacks never allocate, create tasks, take application mutexes, call Wi-Fi APIs, or block; they only notify this worker with request bits. The worker is subscribed to TWDT and feeds it after each notification wait.

`ensureWifiInit()` does four things once for the lifetime of the manager:

1. initializes the Wi-Fi driver with `WIFI_STORAGE_RAM`
2. creates the reconnect/setup-AP retry timers
3. creates the `air360_net` worker and scan request synchronization primitives
4. registers persistent `WIFI_EVENT` and `IP_EVENT_STA_GOT_IP` handlers

The handlers stay active after `connectStation()` returns, which is what enables runtime recovery. `NetworkManager` is non-copyable/non-movable so those callback registrations cannot accidentally outlive or point at a copied manager object.

---

## Station connection

`connectStation(config, timeout_ms=15000)` performs a synchronous station join attempt:

1. stores the latest station/AP config for future retry attempts
2. creates `sta_netif` if needed
3. applies hostname and optional static IPv4 settings
4. clears the station event bits
5. switches Wi-Fi to `WIFI_MODE_STA`
6. applies STA credentials and starts Wi-Fi
7. sets power save mode: `WIFI_PS_MIN_MODEM` if `config.wifi_power_save_enabled`, otherwise `WIFI_PS_NONE`
8. waits up to 15 seconds for `kStationConnectedBit` or `kStationFailedBit`, retrying `esp_wifi_connect()` inside that window after a fast failure

The default timeout comes from `CONFIG_AIR360_WIFI_CONNECT_TIMEOUT_MS` via `tuning::network::kConnectTimeoutMs`. Increase it only if field networks routinely need more than one WPA + DHCP round-trip window to become usable.

### Boot-time in-window retries

The ESP-IDF driver calls `esp_wifi_connect()` once and never retries by itself, so a `NO_AP_FOUND` after a 1–2 s scan, a beacon timeout, or an auth failure on a sagging supply used to end the whole boot attempt within seconds. For the boot-time attempt (`ConnectAttemptKind::kInitial`) the firmware now stays inside the connect window: after `kStationFailedBit` it waits `kInitialConnectRetryDelayMs` (1 s), clears the event bits, calls `esp_wifi_connect()` again, and keeps waiting for the remainder of the window. A retry is attempted only while at least `kInitialConnectRetryMinRemainingMs` (3 s) of the window is left, so a 15 s window typically yields three to four association attempts. Each retry is logged with the disconnect reason and the remaining window time.

Runtime reconnects and setup-AP retries do not use in-window retries; they already have their own backoff or interval loop.

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

## mDNS local discovery

After a successful station join, `startMdns()` initialises mDNS once for the lifetime of the process:

```cpp
mdns_init();
mdns_hostname_set(hostname.c_str());   // derived from device_name
mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0);
```

The hostname is the sanitised form of `device_name` produced by `stationHostname()`:

- alphanumeric characters are kept and lowercased
- spaces and special characters become `-`
- leading and trailing `-` are stripped
- empty result falls back to `"air360"`

The device becomes reachable at `{hostname}.local` on the local network immediately after DHCP succeeds. The mDNS responder advertises an `_http._tcp` service on port 80 so that local service browsers can discover the web UI automatically.

`runtime_.mdns_initialized` guards against double-init. mDNS survives Wi-Fi reconnects without any restart — the ESP-IDF mDNS implementation re-announces after interface events automatically.

mDNS is not started in setup AP mode. The setup AP network is isolated, and the AP address (`192.168.4.1`) is fixed and already known.

## Captive portal

When a client connects to the setup AP, the OS performs a captive portal probe — an HTTP request to a well-known URL (varies by OS) to detect whether internet is available. If the response is unexpected, the OS treats the network as a captive portal and surfaces a "sign in" prompt.

Air360 uses the `nordesems/esp-captive-portal` managed component to intercept this mechanism:

- **DNS**: the component registers a DNS server that starts automatically on `WIFI_EVENT_AP_START` and stops on `WIFI_EVENT_AP_STOP`. All hostname queries resolve to the AP IP (`192.168.4.1`), so any URL the browser tries will reach the device.
- **HTTP**: `captive_portal_register_catchall()` is called in `WebServer::start()` after all regular URI handlers. It registers a wildcard `/*` handler that redirects any unrecognised request to `/`, sending the client to the config page.

The catchall is registered last so it does not shadow the existing `/`, `/config`, `/sensors`, `/diagnostics`, and asset routes.

The component has no effect when the AP is not running (station-only mode).

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

The first step and cap are build-time tunables: `CONFIG_AIR360_WIFI_RECONNECT_BASE_DELAY_MS` and `CONFIG_AIR360_WIFI_RECONNECT_MAX_DELAY_MS`.

When the timer fires, the callback only notifies the persistent `air360_net` worker. The worker performs the blocking `attemptStationConnect(...)`, so the shared FreeRTOS timer task is not blocked by the 15-second station wait and no per-attempt tasks are spawned. Before acting on a reconnect or setup-AP retry request the worker checks `station_connected`; a request that outlived the outage (the station came back through another path) is logged and ignored instead of tearing the live connection down.

`IP_EVENT_STA_GOT_IP` resets the reconnect counter to zero and clears the backoff state.

**Self-healing schedule.** A runtime reconnect attempt starts with the intentional-stop guard active (see below) for `CONFIG_AIR360_WIFI_DISCONNECT_IGNORE_WINDOW_MS`. A real disconnect that lands inside that window, for example `NO_AP_FOUND` after a short scan, still sets `kStationFailedBit` but the handler deliberately schedules nothing. `attemptStationConnect()` therefore checks on its failure path whether the reconnect timer is active and, if not, arms the next backoff step itself through `armReconnectBackoff()`. The same check re-arms the setup-AP retry timer after a failed APSTA retry. Without it the loop ended silently with `reconnect_backoff_active = true` and no timer.

### Recovery when setup AP cannot start

If the boot-time station join fails **and** `startLabAp()` itself fails (an `esp_wifi_*` error on a weak supply, for example), `NetworkLayer::bootWifi` calls `scheduleStationRecovery()`. With stored station credentials and an initialised Wi-Fi runtime this arms the normal reconnect backoff in station-only mode, so the device keeps retrying the upstream network instead of sitting in `kOffline` until the next reboot. Without credentials or without a runtime it returns `ESP_ERR_INVALID_STATE` and the failure is logged.

### Setup AP retry loop

If setup AP is active and stored station credentials exist, the firmware now keeps retrying station mode in the background:

- the setup AP remains available during the wait period
- every 3 minutes the `air360_net` worker attempts station association in `WIFI_MODE_APSTA`
- on success the firmware switches back to `WIFI_MODE_STA`
- on failure the AP stays up and the retry timer is armed again

The retry cadence is controlled by `CONFIG_AIR360_WIFI_SETUP_AP_RETRY_DELAY_MS` (default `180000` ms).

This covers the common “sensor boots faster than the router” case without needing a reboot.

### Intentional stop guard

`stopStation()` and internal mode reconfiguration operations temporarily suppress disconnect handling for a short window (`ignore_disconnect_until_ms`) so deliberate `esp_wifi_stop()` / `esp_wifi_disconnect()` calls do not accidentally arm reconnect logic. The debug-window expiry path calls `requestStopStation()`, which posts a worker request bit and lets `air360_net` call `stopStation()` in task context. The guard window is controlled by `CONFIG_AIR360_WIFI_DISCONNECT_IGNORE_WINDOW_MS` and defaults to `2000` ms.

---

## Setup AP

`startLabAp(config)`:

1. stores the latest config for future background retries
2. creates both AP and STA netifs if needed
3. switches Wi-Fi to `WIFI_MODE_APSTA`
4. configures the setup AP on `192.168.4.1/24`
5. optionally preloads STA credentials into the Wi-Fi driver if they exist
6. starts Wi-Fi with `WIFI_PS_NONE` (setup AP always disables power save)
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

`scanAvailableNetworks()` remains a synchronous public API, but the blocking Wi-Fi scan itself runs inside the `air360_net` worker. The caller posts a scan request bit, waits for the worker's completion semaphore, and then reads the updated scan cache. The scan still requires `WIFI_MODE_STA` or `WIFI_MODE_APSTA`.

- hidden SSIDs are skipped
- duplicate SSIDs are collapsed
- failures clear `available_networks_` and populate `last_scan_error_`
- callers do not read the scan cache field-by-field; `/config` and `/wifi-scan` consume a single `WifiScanSnapshot`

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

Wi-Fi reconnect itself no longer depends on this loop; it is driven by the persistent event handlers, timers, and the `air360_net` worker.

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
| Boot join in-window retry delay | 1 000 ms |
| Boot join minimum window left for a retry | 3 000 ms |
| Station wait poll slice | 250 ms |
| Reconnect base delay | 10 s |
| Reconnect cap | 300 s |
| Setup-AP retry interval | 180 s |
| SNTP poll interval | 250 ms |
| SNTP timeout (initial) | 15 000 ms |
| SNTP timeout (maintenance retry) | 10 000 ms |
| AP static IP | 192.168.4.1/24 |
| Wi-Fi storage | RAM only |
| Power save mode (station) | `WIFI_PS_NONE` (default) or `WIFI_PS_MIN_MODEM` (when `wifi_power_save_enabled = 1`) |
| Power save mode (setup AP) | `WIFI_PS_NONE` always |
