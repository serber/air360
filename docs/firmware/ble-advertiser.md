# BLE Advertiser

## Status

Implemented. Keep this document aligned with `firmware/main/src/ble_advertiser.cpp` and the BLE fields in `DeviceConfig`.

## Scope

This document covers the BLE advertising subsystem: how it is configured, what data it broadcasts, the packet format, and how Home Assistant and other scanners consume it.

## Source of truth in code

- `firmware/main/include/air360/ble_advertiser.hpp`
- `firmware/main/src/ble_advertiser.cpp`
- `firmware/main/include/air360/config_repository.hpp` (BLE fields in `DeviceConfig`)

## Read next

- [configuration-reference.md](configuration-reference.md)
- [nvs.md](nvs.md)
- [measurement-pipeline.md](measurement-pipeline.md)

---

## Overview

When enabled, the device continuously broadcasts current sensor readings as non-connectable BLE advertisement packets in BTHome v2 format. No pairing or connection is required. Any nearby BLE scanner — including Home Assistant's built-in Bluetooth integration — passively receives the data.

The feature requires:
- `DeviceConfig.ble_advertise_enabled = 1` (runtime, toggled via Device Configuration page)

---

## BLE stack

The firmware uses the ESP-IDF NimBLE stack (advertising-only profile). Two FreeRTOS tasks are involved:

| Task | Owner | Role |
|------|-------|------|
| `nimble_host` | NimBLE port | Runs the NimBLE event loop |
| `air360_ble` | `BleAdvertiser` | Rebuilds advertisement payload every 5 seconds by default |

**Log tag:** `air360.ble`

`air360_ble` is subscribed to the Task Watchdog Timer. It feeds TWDT while waiting for NimBLE host sync and after each advertisement update wakeup. Shutdown is cooperative: `BleAdvertiser::stop()` clears the atomic enable flag, wakes the task with a notification, waits on a stop-acknowledge semaphore, and only then stops the NimBLE host. When the configured NimBLE role set links ESP-IDF's full port deinit path, `stop()` also deinitializes the port; the current broadcaster-only role keeps the initialized port reusable after host stop because ESP-IDF 6.0 does not link the security-manager deinit symbol for that role set. The BLE task always exits through `vTaskDelete(nullptr)`.

---

## BTHome v2 packet format

Service UUID: `0xFCD2`  
AD type: `0x16` (Service Data — 16-bit UUID)

The fixed advertisement fields are populated through ESP-IDF 6.0's `ble_hs_adv_fields` API:

- Flags: general discoverable + BR/EDR unsupported
- Complete local name: included only when it fits the legacy 31-byte advertisement budget
- Service data (`0x16`): BTHome payload, still packed manually because NimBLE does not understand the BTHome object layout itself

Payload layout:

```
[0xD2][0xFC]           ← UUID 0xFCD2 in little-endian
[0x40]                 ← Device info byte: no encryption, BTHome v2
[object_id][value]...  ← Measurement objects in priority order
```

Total packet size limit: 31 bytes (legacy BLE advertisement).

### Measurement encoding

Values are encoded in priority order. Encoding stops when the remaining budget is exhausted.

| Measurement | Object ID | Value type | Scale factor | Bytes |
|-------------|-----------|-----------|--------------|-------|
| Temperature | `0x02` | int16 | ×100 (0.01 °C) | 2 |
| Humidity | `0x03` | uint16 | ×100 (0.01 %) | 2 |
| CO₂ | `0x12` | uint16 | ×1 (ppm) | 2 |
| PM 2.5 | `0x0D` | uint16 | ×100 (0.01 µg/m³) | 2 |
| PM 10 | `0x0E` | uint16 | ×100 (0.01 µg/m³) | 2 |
| Pressure | `0x04` | uint24 | ×100 (0.01 hPa) | 3 |
| Illuminance | `0x05` | uint24 | ×100 (0.01 lux) | 3 |

With a 6-character device name ("air360"), the packet budget for measurements is approximately 15 bytes — enough for 5 measurements of 3 bytes each (temperature, humidity, CO₂, PM 2.5, PM 10).

---

## Data source

`BleAdvertiser` reads from `MeasurementStore::allLatestMeasurements()` on every update cycle. This method returns a snapshot of the most recent reading from each sensor, regardless of SNTP status. The advertisement therefore reflects the last valid sensor reading even when there is no network connection.

Payload rebuild cadence is separate from the on-air advertising interval. `air360_ble` wakes up every `CONFIG_AIR360_BLE_PAYLOAD_REFRESH_INTERVAL_MS` (default `5000` ms) to re-scan the latest measurements and repack the BTHome payload. Lower values make BLE telemetry fresher; higher values reduce task wakeups and payload churn.

---

## Advertising interval

| Index | Interval | Recommended for |
|-------|----------|-----------------|
| 0 | 100 ms | High-frequency scanning; highest power draw |
| 1 | 300 ms | Fast updates |
| **2** | **1000 ms** | **Default — Home Assistant, general use** |
| 3 | 3000 ms | Power-constrained deployments |

The interval is stored as `DeviceConfig.ble_adv_interval_index` and mapped to BLE time units (0.625 ms each) at advertising start.

---

## Configuration

BLE settings appear in the **Device Configuration** web page under the "BLE Advertising" section, visually separated from network settings:

- **Enable BLE advertising** checkbox → `ble_advertise_enabled`
- **Advertising interval** selector → `ble_adv_interval_index`

Changes require a device reboot (same as all Device Configuration changes).

---

## Home Assistant integration

BTHome v2 is detected automatically by Home Assistant's Bluetooth integration. Once the device is advertising:

1. Navigate to **Settings → Devices & Services → Bluetooth**
2. The Air360 device appears with its advertised sensor values
3. Add the device — no further configuration required

The device name shown in Home Assistant matches `DeviceConfig.device_name`.

---

## WiFi + BLE coexistence

ESP32-S3 supports concurrent WiFi and BLE operation via hardware radio arbitration. No explicit coexistence configuration is required beyond enabling both in `sdkconfig`. Expect a small reduction in WiFi throughput (~10–15%) when BLE advertising is active, which is negligible for sensor telemetry workloads.

---

## BLE stack configuration

The NimBLE stack is enabled unconditionally in `sdkconfig.defaults` (`CONFIG_BT_ENABLED`, `CONFIG_BT_NIMBLE_ENABLED`, broadcaster role only). The stack initialises at boot only when `ble_advertise_enabled = 1`. When disabled, `BleAdvertiser::start()` returns immediately and no BLE tasks are spawned.
