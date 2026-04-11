# Network Manager

This document describes the `NetworkManager` class — how it initialises the Wi-Fi driver, transitions between operating modes, manages station connection and fallback AP, and synchronises system time via SNTP.

---

## Operating modes

```cpp
enum class NetworkMode : uint8_t {
    kOffline  = 0,   // Wi-Fi driver stopped or connection failed
    kSetupAp  = 1,   // Setup AP active, waiting for configuration
    kStation  = 2,   // Connected to upstream Wi-Fi, IP assigned
};
```

The current mode plus all derived state is held in `NetworkState`:

```cpp
struct NetworkState {
    NetworkMode mode;
    bool station_config_present;      // wifi_sta_ssid is non-empty in NVS
    bool station_connect_attempted;
    bool station_connected;           // IP has been assigned
    bool time_sync_attempted;
    bool time_synchronized;
    bool lab_ap_active;
    string station_ssid;
    string lab_ap_ssid;
    string ip_address;
    string last_error;
    string time_sync_error;
    int64_t last_time_sync_unix_ms;
};
```

---

## Boot-time network selection (step 7/9)

`App::run()` calls `NetworkManager` once during boot step 7 of the 9-step startup sequence (see [startup-pipeline.md](startup-pipeline.md)):

```
Station SSID in NVS?
  ├─ Yes → connectStation()
  │         ├─ Success → kStation, then synchronizeTime()
  │         └─ Failure → startLabAp()  (fallback)
  └─ No  → startLabAp()
```

Both outcomes are non-fatal. If `startLabAp()` also fails (logged at WARN level), the device starts with no network connectivity but the web server still starts on port 80.

---

## Internal runtime context

A file-scope singleton `RuntimeContext` holds resources that must survive for the lifetime of the application:

```cpp
struct RuntimeContext {
    EventGroupHandle_t station_events;  // BIT0=connected, BIT1=failed
    esp_netif_t*       ap_netif;        // created once for AP interface
    esp_netif_t*       sta_netif;       // created once for STA interface
    bool               wifi_initialized;
    bool               sntp_initialized;
};
```

`ensureWifiInit()` is called from both `connectStation()` and `startLabAp()`. It initialises the Wi-Fi driver exactly once (`wifi_initialized` guard). Wi-Fi credentials are kept in RAM only — `WIFI_STORAGE_RAM` is set explicitly so the ESP-IDF driver does not write to its own NVS slot.

---

## Station connection (`connectStation`)

Called with `timeout_ms = 15 000` ms by default.

1. Calls `ensureWifiInit()` — creates event group, initialises Wi-Fi driver if needed.
2. Creates `sta_netif` if it does not exist yet.
3. Sets DHCP hostname: `device_name` from `DeviceConfig`, lowercased, non-alphanumeric characters replaced with `-`, trailing hyphens stripped. Falls back to `"air360"` if the result is empty.
4. Registers per-call event handler instances for `WIFI_EVENT` (any ID) and `IP_EVENT_STA_GOT_IP`.
5. Calls `esp_wifi_stop()` (tolerates not-started errors), then configures the driver:
   - Mode: `WIFI_MODE_STA`
   - Auth threshold: `WIFI_AUTH_OPEN` (connects to any security level)
   - PMF: capable but not required
   - Power save: `WIFI_PS_NONE` — disabled for lower upload latency
6. Calls `esp_wifi_start()`, which triggers `WIFI_EVENT_STA_START` → the event handler calls `esp_wifi_connect()`.
7. Polls the event group in 250 ms slices, resetting the task watchdog on each slice, until `kStationConnectedBit` or `kStationFailedBit` is set or the timeout expires.
8. Unregisters both event handlers regardless of outcome.

| Outcome | Action | Return |
|---------|--------|--------|
| `kStationConnectedBit` set | Calls `synchronizeTime()` | `ESP_OK` |
| `kStationFailedBit` set | Stops Wi-Fi | `ESP_FAIL` |
| Timeout | Stops Wi-Fi | `ESP_ERR_TIMEOUT` |

On failure the caller (`App::run()`) falls back to `startLabAp()`.

### Wi-Fi event handlers

`handleWifiEvent` and `handleIpEvent` are registered as per-call instances and unregistered before `connectStation()` returns. They run on the Wi-Fi system event task (not the main task).

| Event | Handler action |
|-------|---------------|
| `WIFI_EVENT_STA_START` | `esp_wifi_connect()` |
| `WIFI_EVENT_STA_DISCONNECTED` | `station_connected = false`, `mode = kOffline`, IP cleared, reason code written to `last_error`, `kStationFailedBit` set |
| `IP_EVENT_STA_GOT_IP` | IP address saved, `mode = kStation`, `station_connected = true`, `lab_ap_active = false`, `kStationConnectedBit` set |

**No automatic reconnect.** A disconnection during normal operation sets `mode = kOffline` and `station_connected = false`. The upload manager and maintenance loop detect this and pause; no reconnect attempt is made.

---

## Setup AP (`startLabAp`)

1. Calls `ensureWifiInit()`.
2. Creates both `ap_netif` and `sta_netif` if absent. The STA interface is required even in AP-only mode because `scanAvailableNetworks()` needs it.
3. Sets mode to `WIFI_MODE_APSTA` — the device acts as both an AP and a station simultaneously. This allows scanning for upstream networks while the AP is active.
4. Configures a static IP for the AP interface: **`192.168.4.1/24`**, starts the DHCP server.
5. Applies AP config:
   - SSID and password from `DeviceConfig.lab_ap_ssid` / `lab_ap_password`
   - Channel: `CONFIG_AIR360_LAB_AP_CHANNEL` (Kconfig)
   - Max connections: `CONFIG_AIR360_LAB_AP_MAX_CONNECTIONS` (Kconfig)
   - Auth: `WIFI_AUTH_WPA2_PSK` if password is non-empty; `WIFI_AUTH_OPEN` otherwise
   - PMF not required
6. Calls `esp_wifi_start()`.
7. Sets `mode = kSetupAp`, `lab_ap_active = true`, `ip_address = "192.168.4.1"`.
8. Calls `scanAvailableNetworks()` immediately — the scan result is used by the web UI to populate the network selector. A scan failure is logged at WARN level and does not affect the AP startup result.

---

## Wi-Fi scan (`scanAvailableNetworks`)

Can be called at any time while Wi-Fi is in `WIFI_MODE_STA` or `WIFI_MODE_APSTA`. The scan is **blocking** (`esp_wifi_scan_start(..., true)`).

Results are stored in `available_networks_` as `WifiNetworkRecord` entries:

```cpp
struct WifiNetworkRecord {
    string ssid;
    int    rssi;
    wifi_auth_mode_t auth_mode;
};
```

Duplicate SSIDs (multiple BSSIDs with the same name) are deduplicated — only the first entry in the scan result is kept. Hidden networks (empty SSID) are skipped. On error `available_networks_` is cleared and the error is stored in `last_scan_error_`.

---

## Time synchronisation

> A full reference for both time domains (uptime vs Unix), the validity threshold, and all places in the system that gate on valid time is in [time.md](time.md). This section covers only the SNTP mechanics inside `NetworkManager`.

### `synchronizeTime()` (called internally)

Called immediately after a successful station connection and from `ensureStationTime()`.

1. Requires `station_connected == true`; fails immediately otherwise.
2. If `hasValidUnixTime()` is already true, skips SNTP and returns `ESP_OK`. This handles the case of a reboot with time already set by a previous run.
3. On the first call: `esp_netif_sntp_init()` with server `pool.ntp.org` (`sntp_initialized = false → true`).
4. On subsequent calls: `esp_netif_sntp_start()` (re-arms the already-initialised SNTP client).
5. Polls `hasValidUnixTime()` every **250 ms** with task watchdog reset, up to `timeout_ms` (default 15 000 ms).
6. Sets `time_synchronized = true` and records `last_time_sync_unix_ms` on success.

### Time validity threshold

`hasValidUnixTime()` and `currentUnixMilliseconds()` compare the system clock against `kMinValidUnixTimeSeconds = 1700000000` (2023-11-14 UTC). Any value below this threshold is treated as not-yet-set and `currentUnixMilliseconds()` returns `0`.

### `ensureStationTime()` (called from maintenance loop)

Public method — used by `App::run()` to retry synchronisation if it failed during the initial connect. Only executes if `mode == kStation && station_connected == true` and time is not yet valid. Timeout: **10 000 ms**.

---

## Maintenance loop (runtime)

After all boot steps complete, `App::run()` enters an infinite loop:

```cpp
for (;;) {
    if (mode == kStation && station_connected && !hasValidTime()) {
        ensureStationTime(10000);   // retry SNTP if not yet synchronized
    }
    status_service.setNetworkState(network_manager.state());
    vTaskDelay(kRuntimeMaintenanceDelay);
}
```

This periodically retries SNTP in case the first attempt timed out (e.g., NTP server temporarily unreachable right after boot).

---

## State transition diagram

```
            boot: no STA config          boot: STA config, connect OK
                   │                              │
                   ▼                              ▼
              kSetupAp ──────────────────────► kStation
                                                  │
                   ▲         DISCONNECTED event   │
                   │  ◄───────────────────────────┘
               kOffline
                   │
        (no reconnect — stays offline
         until next reboot)
```

`kOffline` is also the initial state before any Wi-Fi call. The firmware does not attempt automatic reconnection after a disconnect; the upload manager and sensor pipeline pause until the device is rebooted.

---

## Summary of constants and defaults

| Parameter | Value | Source |
|-----------|-------|--------|
| Station connect timeout | 15 000 ms | `connectStation()` default |
| Station wait poll slice | 250 ms | `kStationWaitSliceMs` |
| SNTP server | `pool.ntp.org` | `kDefaultSntpServer` |
| SNTP poll interval | 250 ms | `kSntpPollIntervalMs` |
| SNTP timeout (initial) | 15 000 ms | `synchronizeTime()` default |
| SNTP timeout (retry) | 10 000 ms | `ensureStationTime()` call |
| Min valid Unix time | 1 700 000 000 s | `kMinValidUnixTimeSeconds` |
| AP static IP | 192.168.4.1/24 | `startLabAp()` |
| AP channel | Kconfig | `CONFIG_AIR360_LAB_AP_CHANNEL` |
| AP max connections | Kconfig | `CONFIG_AIR360_LAB_AP_MAX_CONNECTIONS` |
| Wi-Fi storage | RAM only | `WIFI_STORAGE_RAM` |
| Power save mode | disabled | `WIFI_PS_NONE` |
| Log tag | `air360.net` | `kTag` |
