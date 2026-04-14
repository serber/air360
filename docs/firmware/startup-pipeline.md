# Firmware Startup Sequence

This document describes the full application startup process — from the ESP-IDF boot chain through each initialization step to the moment the device is ready and running.

---

## Overview

```
ROM bootloader
  └─ second-stage bootloader (firmware/build/bootloader)
       └─ ESP-IDF runtime init
            └─ app_main task (FreeRTOS, stack 8 KB)
                 └─ App::run()  ← 9 sequential boot steps
                      ├─ step 5 → spawns air360_sensor task
                      ├─ step 8 → spawns air360_upload task
                      └─ step 9 → starts esp_http_server (its own task)
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

`app_main.cpp` is a three-line C entry point. It constructs an `air360::App` on the main task stack and calls `run()`.

```cpp
extern "C" void app_main(void) {
    air360::App app;
    app.run();
}
```

`App::run()` is where all initialization happens and where the main task spends the rest of its life. To prevent large objects from overflowing the 8 KB main task stack, every long-lived runtime object inside `run()` is declared `static`:

```cpp
static BuildInfo build_info = getBuildInfo();
static ConfigRepository config_repository;
static DeviceConfig config = makeDefaultDeviceConfig();
static StatusService status_service(build_info);
static SensorConfigRepository sensor_config_repository;
static SensorConfigList sensor_config_list = ...;
static SensorManager sensor_manager;
static MeasurementStore measurement_store;
static BackendConfigRepository backend_config_repository;
static BackendConfigList backend_config_list = ...;
static UploadManager upload_manager;
static NetworkManager network_manager;
static WebServer web_server;
```

Static allocation places these objects in BSS/data segment rather than on the stack.

---

## Phase 2 — 9-step boot sequence (`App::run`)

Steps execute sequentially in the main task. There is no parallelism at this stage.

| Step | Action | Fatal? | Side effect |
|------|--------|--------|-------------|
| pre | Init boot LEDs (GPIO 10/11) | No | Both LEDs off |
| 1/9 | Arm task watchdog (10 s, no panic) | No | Main task subscribed to TWDT |
| 2/9 | Initialize NVS (`nvs_flash_init`) | **Yes** | Red LED on failure |
| 3/9 | Initialize network core (`netif` + event loop) | **Yes** | Red LED on failure |
| 4/9 | Load or create `device_cfg` | No | `boot_count` incremented; `StatusService` updated |
| 5/9 | Load or create `sensor_cfg`; start sensor task | No | **`air360_sensor` task spawned** |
| 6/9 | Load or create `backend_cfg` | No | — |
| 7/9 | Resolve network mode (station or setup AP) | No | `StatusService` updated with network state |
| 8/9 | Start upload manager; apply backend config | No | **`air360_upload` task spawned** |
| 9/9 | Start web server | **Yes** | Green LED on success; WDT removed from main task |

---

### Boot LEDs (pre-step)

GPIO 10 (red) and GPIO 11 (green) are configured as outputs and both set low. The LED state is used as a coarse boot indicator:

| State | Meaning |
|-------|---------|
| Both off | Booting |
| Green on, red off | Boot completed successfully |
| Red on, green off | Fatal error (NVS init failed or web server failed to start) |

---

### Step 1 — Arm task watchdog

`esp_task_wdt_add(nullptr)` subscribes the main task to the Task Watchdog Timer (TWDT).

- Timeout: 10 seconds
- Panic on timeout: **disabled** (logs warning, does not reboot)
- If TWDT was already initialized by ESP-IDF (from `sdkconfig`), the call simply attaches to it
- If TWDT is not pre-initialized, the firmware initializes it with the parameters above

The watchdog is fed (reset) implicitly at long-blocking calls during steps 7 and 8 by the respective subsystems. After step 9 is complete, the main task **removes itself** from the watchdog (`esp_task_wdt_delete(nullptr)`) — the maintenance loop does not need watchdog supervision.

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

The decision is based on whether `wifi_sta_ssid` is non-empty in `DeviceConfig`:

```
wifi_sta_ssid non-empty?
  ├─ YES → NetworkManager::connectStation()
  │          ├─ SUCCESS → station mode, SNTP sync, green LED eventually
  │          └─ FAILURE → NetworkManager::startLabAp()  (fallback)
  └─ NO  → NetworkManager::startLabAp()
```

**`connectStation(config, timeout_ms=15000)`:**
- Creates Wi-Fi STA netif
- Registers WIFI_EVENT and IP_EVENT handlers on the default event loop
- Sets the DHCP hostname from `device_name` (lowercased, alphanumeric)
- Starts Wi-Fi and waits for an IP address (up to 15 seconds), resetting the watchdog while waiting
- On success: calls `synchronizeTime()` — polls SNTP for up to 15 seconds, resetting the watchdog

**`startLabAp(config)`:**
- Creates AP+STA netif
- Assigns static IP `192.168.4.1 / 255.255.255.0`
- Starts DHCP server on `192.168.4.0/24`
- Optionally scans for available networks (stored for the `/wifi-scan` endpoint)

Network failures at this step are **non-fatal** — the device continues booting without a network connection.

`StatusService` is updated with the current network state.

The full connection sequence, event handler lifecycle, SNTP synchronisation logic, and state transition diagram are in [network-manager.md](network-manager.md).

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
- Max URI handlers: 12

All nine HTTP routes are registered (`/`, `/diagnostics`, `/config`, `/sensors`, `/backends`, `/wifi-scan`, `/assets/*`, etc.). The web server runs in its own internal FreeRTOS task managed by `esp_http_server`.

Failure is **fatal** — sets red LED, returns from `run()`.

On success:
- `StatusService` is updated (web server started flag)
- Green LED is set on
- Main task **removes itself from the watchdog**

---

## Phase 3 — Maintenance loop

After step 9, `App::run()` enters an infinite loop in the main task with a 10-second delay per iteration:

```cpp
for (;;) {
    if (station mode && connected && no valid time) {
        network_manager.ensureStationTime(10000);  // retry SNTP
    }
    status_service.setNetworkState(network_manager.state());
    vTaskDelay(10000 ms);
}
```

The loop has two responsibilities:
- **SNTP retry** — if the device is in station mode but time sync has not succeeded yet, it retries every 10 seconds
- **Network state refresh** — keeps `StatusService` in sync with the current network state so the web UI reflects live uplink status

The main task runs at the default FreeRTOS task priority and stays alive for the lifetime of the firmware.

---

## Long-lived tasks after boot

After the boot sequence completes, the following tasks run concurrently:

| Task | Stack | Priority | Loop period | Spawned at |
|------|-------|----------|-------------|------------|
| `app_main` (main task) | 8 192 B | default | 10 s | ESP-IDF runtime |
| `air360_sensor` | 6 144 B | 5 | 250 ms | Step 5 — `SensorManager::applyConfig()` |
| `air360_upload` | 7 168 B | 4 | 1 s | Step 8 — `UploadManager::start()` |
| `esp_httpd` (web server) | 10 240 B | default | event-driven | Step 9 — `WebServer::start()` |
| ESP-IDF Wi-Fi task | (IDF managed) | (IDF managed) | event-driven | Step 7 — `esp_netif_init` / `esp_wifi_start` |
| ESP-IDF event loop task | (IDF managed) | (IDF managed) | event-driven | Step 3 — `esp_event_loop_create_default` |

`air360_sensor` and `air360_upload` can be stopped and restarted at runtime — `air360_sensor` is restarted by `SensorManager::applyConfig()` when the user applies sensor changes through the web UI, and `air360_upload` is restarted when backend config changes.

---

## Boot outcome log

A successful boot produces the following sequence on the serial monitor:

```
I (air360.app) Boot step 1/9: arm task watchdog
I (air360.app) Boot step 2/9: initialize NVS
I (air360.app) Boot step 3/9: initialize network core
I (air360.app) Boot step 4/9: load or create device config
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
| Sensor config load fails | 5 | Warning logged, empty sensor list used, boot continues |
| Sensor task creation fails | 5 | All sensor states set to `kError`, boot continues without polling |
| Backend config load fails | 6 | Warning logged, defaults used, boot continues |
| Station join fails | 7 | Falls back to Lab AP mode |
| Lab AP start fails | 7 | Warning logged, device runs without network access |
| Upload task creation fails | 8 | All backend states set to `kError`, boot continues without uploads |
| Web server start fails | 9 | Red LED, `run()` returns, device halts |
