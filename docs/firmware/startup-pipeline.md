# Firmware Startup Sequence

## Status

Implemented. Keep this document aligned with the current `firmware/` startup code.

## Scope

This document covers the boot chain and application initialization order from `app_main()` through the point where the long-lived runtime services are active.

## Source of truth in code

- `firmware/main/src/app_main.cpp`
- `firmware/main/src/app.cpp`
- `firmware/main/src/network_manager.cpp`
- `firmware/main/src/uploads/upload_manager.cpp`

## Read next

- [nvs.md](nvs.md)
- [network-manager.md](network-manager.md)
- [measurement-pipeline.md](measurement-pipeline.md)

This document describes the full application startup process — from the ESP-IDF boot chain through each initialization step to the moment the device is ready and running.

---

## Overview

```
ROM bootloader
  └─ second-stage bootloader (firmware/build/bootloader)
       └─ ESP-IDF runtime init
            └─ app_main task (FreeRTOS, stack 8 KB)
                 └─ App::run()  ← 9 sequential boot steps
                      ├─ step 4b → CellularManager::start() (may spawn air360_cellular task)
                      ├─ step 5  → spawns air360_sensor task
                      ├─ step 8  → spawns air360_upload task
                      └─ step 9  → starts esp_http_server (its own task)
                           └─ maintenance loop (runs in app_main task)
```

---

## Phase 0 — ESP-IDF boot chain (before app_main)

This phase is handled entirely by ESP-IDF and the hardware bootloader. The application has no control over it.

1. ROM bootloader runs from internal ROM, verifies and loads the second-stage bootloader from flash offset `0x0`.
2. Second-stage bootloader loads the partition table from `0x8000`, selects the active OTA slot (factory at `0x20000` in the current config), and loads the application image.
3. ESP-IDF C runtime initializes: sets up the heap, SPI flash driver, default log level, chip identification, and starts the FreeRTOS scheduler.
4. FreeRTOS creates the **main task** with a stack size of `CONFIG_ESP_MAIN_TASK_STACK_SIZE` (8192 bytes, set in `sdkconfig.defaults`) and calls `app_main()`.

---

## Phase 1 — Entry point (`app_main`)

`app_main.cpp` is a small C entry point. It keeps the top-level `air360::App` object in static storage and calls `run()`.

```cpp
extern "C" void app_main(void) {
    static air360::App app;
    app.run();
}
```

`App::run()` is where all initialization happens and where the main task spends the rest of its life. To prevent large objects from overflowing the 8 KB main task stack, `App` owns long-lived runtime objects as explicit members and the single `App` instance itself lives in static storage:

```cpp
class App {
    BuildInfo build_info_;
    ConfigRepository config_repository_;
    DeviceConfig config_;
    StatusService status_service_;
    SensorConfigRepository sensor_config_repository_;
    SensorConfigList sensor_config_list_;
    SensorManager sensor_manager_;
    MeasurementStore measurement_store_;
    CellularConfigRepository cellular_config_repository_;
    CellularConfig cellular_config_;
    CellularManager cellular_manager_;
    BackendConfigRepository backend_config_repository_;
    BackendConfigList backend_config_list_;
    UploadManager upload_manager_;
    NetworkManager network_manager_;
    WebServer web_server_;
    esp_timer_handle_t debug_window_timer_ = nullptr;
    BleAdvertiser ble_advertiser_;
};
```

This makes lifecycle ownership visible in `app.hpp` while still placing the runtime graph in BSS/data rather than on the main task stack. `App`, the manager classes, the web server, BLE advertiser, and transport managers are non-copyable so RTOS handles, callback registrations, and shared mutex-protected state cannot be accidentally duplicated.

---

## Phase 2 — 9-step boot sequence (`App::run`)

Steps execute sequentially in the main task. There is no parallelism at this stage.

| Step | Action | Fatal? | Side effect |
|------|--------|--------|-------------|
| pre | Init RGB LED (GPIO48 WS2812) | No | LED turns blue |
| 1/9 | Arm task watchdog (30 s, panic on timeout) | No | Main task subscribed to TWDT |
| 2/9 | Initialize NVS (`nvs_flash_init`) | **Yes** | Red LED on failure |
| 3/9 | Initialize network core (`netif` + event loop) | **Yes** | Red LED on failure |
| 4/9 | Load or create `device_cfg` | No | `boot_count` incremented; `StatusService` updated |
| 4b/9 | Load or create `cellular_cfg`; init and start `CellularManager` | No | **`air360_cellular` task spawned** if `enabled != 0` |
| 5/9 | Load or create `sensor_cfg`; start sensor task | No | **`air360_sensor` task spawned** |
| 6/9 | Load or create `backend_cfg` | No | — |
| 7/9 | Resolve network mode (cellular or Wi-Fi / setup AP) | No | `StatusService` updated with network and cellular state |
| 8/9 | Start upload manager; apply backend config | No | **`air360_upload` task spawned** |
| 9/9 | Start web server | **Yes** | Green or pink LED on success; main task enters maintenance loop |

---

### RGB Status LED (pre-step)

The built-in WS2812 RGB LED on GPIO48 (ESP32-S3-DevKitC-1) is initialised via the `espressif/led_strip` component using the RMT peripheral. It shows boot and network state at a glance:

| Color | Meaning |
|-------|---------|
| Blue | Booting |
| Green | Boot completed — joined Wi-Fi station |
| Pink | Boot completed — running in setup AP mode |
| Red | Fatal error (NVS init failed or web server failed to start) |

---

### Step 1 — Arm task watchdog

`esp_task_wdt_add(nullptr)` subscribes the main task to the Task Watchdog Timer (TWDT).

- Timeout: **30 seconds**
- Panic on timeout: **enabled** (triggers `esp_system_abort`, device reboots via panic handler)
- If TWDT was already initialized by ESP-IDF (from `sdkconfig`), the call simply attaches to it
- If TWDT is not pre-initialized, the firmware initializes it with the parameters above

The main task feeds the watchdog with `esp_task_wdt_reset()` on every iteration of the maintenance loop (every ~10 s). Subsystem tasks (`air360_sensor`, `air360_upload`, `air360_cellular`, `air360_ble`) subscribe to the TWDT on their own entry and feed it on each loop iteration — see `docs/firmware/watchdog.md`.

Spawned helper tasks created by `NetworkManager` during Wi-Fi reconnect (issue C6) are **not yet subscribed** — they are short-lived and addressed as part of the C6 refactor.

---

### Step 2 — Initialize NVS

`nvs_flash_init()` initializes the NVS subsystem on the `nvs` partition (offset `0x9000`, 24 KB).

- If the partition has no free pages or a version mismatch is detected: the partition is erased and reinitialized
- Failure is **fatal** — sets red LED, returns from `run()`

---

### Step 3 — Initialize network core

Two ESP-IDF calls:

1. `esp_netif_init()` — initializes the TCP/IP adapter
2. `esp_event_loop_create_default()` — creates the default system event loop

Both calls are idempotent (already-initialized is not an error). Failure is **fatal** — sets red LED, returns from `run()`.

---

### Step 4 — Load or create device config

`ConfigRepository::loadOrCreate()` reads the `device_cfg` blob from NVS (namespace `"air360"`). If not found, or if magic/version/size validation fails, factory defaults are written and used. See [configuration-reference.md](configuration-reference.md) for field defaults and validation rules, and [nvs.md](nvs.md) for the blob format and integrity guard pattern.

After loading:
- `StatusService` is updated with the loaded config, boot source, and boot count
- `boot_count` is incremented in NVS and stored in `StatusService`

Config load failure is **non-fatal** — in-memory defaults are used and the boot continues.

---

### Step 4b — Load or create cellular config + start CellularManager

`CellularConfigRepository::loadOrCreate()` reads the `cellular_cfg` blob (namespace `"air360"`). If not found or invalid, defaults are written and used. The cellular config is versioned independently of `DeviceConfig` — a reset here does not touch `device_cfg`.

Then:

1. `CellularManager::init(network_manager)` — wires the network manager reference into the cellular manager so it can update uplink state
2. `CellularManager::start(cellular_config)` — if `cellular_config.enabled != 0`, **spawns the `air360_cellular` FreeRTOS task** which manages the SIM7600E PPP session, reconnect backoff, and hardware reset cycles

> **`air360_cellular` task is spawned here** (when cellular is enabled) — it begins the modem connection sequence independently from this point.

`StatusService` is updated with the cellular manager reference.

Config load failure is **non-fatal** — in-memory defaults are used and the boot continues. If cellular is disabled (`enabled == 0`), no task is spawned and the modem is not touched.

---

### Step 5 — Load or create sensor config + start sensor task

`SensorConfigRepository::loadOrCreate()` reads the `sensor_cfg` blob.

Then:

1. `SensorManager::setMeasurementStore()` — wires `MeasurementStore` into the sensor manager
2. `SensorManager::applyConfig()` — validates the loaded sensor list, instantiates drivers, and **starts the `air360_sensor` FreeRTOS task** if at least one enabled sensor has a valid driver

> **`air360_sensor` task is spawned here** — it begins polling sensors independently from this point on.

`StatusService` is updated with the sensor manager reference and the measurement store reference.

---

### Step 6 — Load or create backend config

`BackendConfigRepository::loadOrCreate()` reads the `backend_cfg` blob. If not found or invalid, defaults are written and used.

Config load failure is **non-fatal** — in-memory defaults are used.

---

### Step 7 — Resolve network mode

The decision tree depends on whether cellular is enabled:

```
cellular_config.enabled != 0?
  ├─ YES (cellular is primary uplink)
  │    ├─ wifi_sta_ssid non-empty?
  │    │    ├─ YES → NetworkManager::connectStation()  (debug window only)
  │    │    │          └─ wifi_debug_window_s > 0 → arm esp_timer to stop station after N seconds
  │    │    └─ NO  → skip Wi-Fi entirely
  │    └─ No AP fallback — CellularManager drives reconnect from this point
  └─ NO (Wi-Fi / setup-AP flow)
       ├─ wifi_sta_ssid non-empty?
       │    ├─ YES → NetworkManager::connectStation()
       │    │          ├─ SUCCESS → station mode, SNTP sync
       │    │          └─ FAILURE → NetworkManager::startLabAp()  (fallback)
       └─ NO  → NetworkManager::startLabAp()
```

**`connectStation(config)`:**
- Creates Wi-Fi STA netif
- Ensures persistent WIFI/IP event handlers and recovery timers are registered
- Sets the DHCP hostname from `device_name` (lowercased, alphanumeric)
- Starts Wi-Fi and waits for an IP address (up to 15 seconds), resetting the watchdog while waiting
- On success: calls `synchronizeTime()` — polls SNTP for up to 15 seconds, resetting the watchdog

**`startLabAp(config)`:**
- Creates AP+STA netif
- Assigns static IP `192.168.4.1 / 255.255.255.0`
- Starts DHCP server on `192.168.4.0/24`
- Optionally scans for available networks (stored for the `/wifi-scan` endpoint)
- If station credentials exist, arms a background station retry loop while setup AP stays available

**Wi-Fi debug window (cellular mode only):**
- If `cellular_config.wifi_debug_window_s > 0`, an `esp_timer` fires after that many seconds and calls `NetworkManager::stopStation()`
- This gives an operator a short Wi-Fi access window at boot for diagnostics while cellular is the permanent uplink

Network failures at this step are **non-fatal** — the device continues booting without a network connection.

`StatusService` is updated with the current network and cellular state.

The full connection sequence, reconnect backoff behavior, setup-AP retry path, SNTP synchronisation logic, and state transition diagram are in [network-manager.md](network-manager.md). Cellular reconnect lifecycle is in [cellular-manager.md](cellular-manager.md).

---

### Step 8 — Start upload manager + apply backend config

Two calls:

1. `UploadManager::start(build_info, config, sensor_manager, measurement_store, network_manager)` — stores references to all dependencies and **spawns the `air360_upload` FreeRTOS task** if at least one enabled backend has a registered uploader
2. `UploadManager::applyConfig(backend_config_list)` — configures the backends list

> **`air360_upload` task is spawned here** — it begins running its upload cycle independently.

The full pipeline — queue mechanics, upload window, batch assembly, acknowledge/restore cycle — is in [measurement-pipeline.md](measurement-pipeline.md).

`StatusService` is updated with the upload manager reference.

---

### Step 9 — Start web server

`WebServer::start()` configures and starts `esp_http_server`:

- Stack size: 10 240 bytes
- Max URI handlers: 14

HTTP handlers are registered for the overview, diagnostics/logs, config, sensors, backends, Wi-Fi scan, SNTP check, embedded assets, and captive-portal catch-all routes. The web server runs in its own internal FreeRTOS task managed by `esp_http_server`.

Failure is **fatal** — sets red LED, returns from `run()`.

On success:
- `StatusService` is updated (web server started flag)
- LED turns green (station mode) or pink (setup AP mode)
- Main task enters the maintenance loop (remains subscribed to TWDT)

---

## Phase 3 — Maintenance loop

After step 9, `App::run()` enters an infinite loop in the main task with a 10-second delay per iteration:

```cpp
for (;;) {
    if (station mode && connected && no valid time) {
        network_manager.ensureStationTime(10000);  // retry SNTP
    }
    status_service.setNetworkState(network_manager.state());
    status_service.setCellularState(cellular_manager.state());
    esp_task_wdt_reset();   // feed TWDT before sleeping
    vTaskDelay(10000 ms);
}
```

The loop has four responsibilities:
- **SNTP retry** — if the device is in station mode but time sync has not succeeded yet, it retries every 10 seconds
- **Network state refresh** — keeps `StatusService` in sync with the current Wi-Fi network state so the web UI reflects live uplink status
- **Cellular state refresh** — keeps `StatusService` in sync with the current cellular state (PPP IP, RSSI, ping result)
- **Watchdog feed** — resets the TWDT on every iteration; the 30-second timeout gives a comfortable margin above the 10-second sleep

The main task runs at the default FreeRTOS task priority and stays alive for the lifetime of the firmware.

---

## Long-lived tasks after boot

After the boot sequence completes, the following tasks run concurrently:

| Task | Stack | Priority | Loop period | TWDT | Spawned at |
|------|-------|----------|-------------|------|------------|
| `app_main` (main task) | 8 192 B | default | 10 s | ✓ subscribed | ESP-IDF runtime |
| `air360_cellular` | 8 192 B | 5 | event-driven | ✓ subscribed | Step 4b — `CellularManager::start()` (when enabled) |
| `air360_net` | 6 144 B | 2 | event-driven | ✓ subscribed | Step 7 — `NetworkManager::ensureWifiInit()` |
| `air360_sensor` | 6 144 B | 5 | 250 ms | ✓ subscribed | Step 5 — `SensorManager::applyConfig()` |
| `air360_upload` | 7 168 B | 4 | 1 s | ✓ subscribed | Step 8 — `UploadManager::start()` |
| `air360_ble` | 4 096 B | 3 | 5 s | ✓ subscribed | Step 5 — `BleAdvertiser::start()` (when enabled) |
| `esp_httpd` (web server) | 10 240 B | default | event-driven | ✗ IDF-managed | Step 9 — `WebServer::start()` |
| ESP-IDF Wi-Fi / event loop | (IDF managed) | (IDF managed) | event-driven | ✗ IDF-managed | Steps 3 / 7 |

`air360_sensor` and `air360_upload` can be stopped and restarted at runtime. `SensorManager::applyConfig()` restarts the sensor task when the user applies sensor changes through the web UI; `UploadManager::applyConfig()` restarts the upload task when backend config changes. Both paths use task notification plus an acknowledgement event bit and abort the runtime apply on timeout instead of replacing live runtime objects under a still-running task. `BleAdvertiser::stop()` also uses task notification plus a stop-acknowledge semaphore; the `air360_ble` task self-deletes after leaving NimBLE calls so no caller deletes it from a foreign task context.

---

## Boot outcome log

A successful boot produces the following sequence on the serial monitor:

```
I (air360.app) Boot step 1/9: arm task watchdog
I (air360.app) Boot step 2/9: initialize NVS
I (air360.app) Boot step 3/9: initialize network core
I (air360.app) Boot step 4/9: load or create device config
I (air360.app) Boot step 4b/9: load or create cellular config
I (air360.app) Cellular uplink: enabled   (or: disabled)
I (air360.app) Boot step 5/9: load or create sensor config
I (air360.app) Boot step 6/9: load or create backend config
I (air360.app) Boot step 7/9: resolve network mode
I (air360.app) Boot step 8/9: start upload manager
I (air360.app) Boot step 9/9: start status web server
I (air360.app) Runtime ready on port 80
```

---

## Failure modes

| Failure | Step | Effect |
|---------|------|--------|
| NVS init fails | 2 | Red LED, `run()` returns, device halts |
| Network core init fails | 3 | Red LED, `run()` returns, device halts |
| Config load fails | 4 | Warning logged, in-memory defaults used, boot continues |
| Cellular config load fails | 4b | Warning logged, in-memory defaults used, boot continues |
| Cellular task creation fails | 4b | Warning logged, boot continues without cellular uplink |
| Sensor config load fails | 5 | Warning logged, empty sensor list used, boot continues |
| Sensor task creation fails | 5 | All sensor states set to `kError`, boot continues without polling |
| Backend config load fails | 6 | Warning logged, defaults used, boot continues |
| Station join fails (Wi-Fi mode) | 7 | Falls back to Lab AP mode |
| Station join fails (cellular mode) | 7 | Warning logged, Wi-Fi skipped, cellular is still primary uplink |
| Lab AP start fails | 7 | Warning logged, device runs without network access |
| Upload task creation fails | 8 | All backend states set to `kError`, boot continues without uploads |
| Web server start fails | 9 | Red LED, `run()` returns, device halts |
