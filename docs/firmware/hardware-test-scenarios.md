# Hardware Test Scenarios

## Status

Implemented. These are manual on-device checks for behaviour that host tests cannot reach (radios, modem, flash, real sensors, power). Keep the expected results aligned with the current `firmware/` tree and the subsystem docs linked from each scenario.

## Scope

Covers on-device verification of link loss, sensor disconnect, OTA interruption, sustained operation, and the hardware checks still pending for recent fixes. Host-testable logic (parsers, repositories, manager lifecycles) is covered by `firmware/test/host/` and runs in CI; see [`firmware/README.md`](../../firmware/README.md#host-tests). This document does not describe how to build or flash; see [`../guide/README.md`](../guide/README.md).

## Source of truth in code

- `firmware/main/src/network_manager.cpp`, `firmware/main/src/cellular_manager.cpp`
- `firmware/main/src/sensors/sensor_manager.cpp`, `firmware/main/src/status_service.cpp`
- `firmware/main/src/uploads/upload_manager.cpp`, `firmware/main/src/uploads/measurement_store.cpp`
- `firmware/main/src/ota_service.cpp`, `firmware/main/src/app.cpp`

## Read next

- [network-manager.md](network-manager.md) — Wi-Fi reconnect backoff and setup-AP retry
- [cellular-manager.md](cellular-manager.md) — PPP reconnect table and escalation
- [measurement-pipeline.md](measurement-pipeline.md) — queueing, freshness, acknowledgement cursors
- [ota.md](ota.md) — OTA session, deferred reboot, rollback

---

## How to run and record

- Run on a board flashed with the firmware build under test; note the version shown on `/config` (Firmware Update card) or in the boot log.
- Observe through the web UI: `/` (overview health), `/diagnostics` (raw JSON and live log console backed by `GET /logs/data`), `/ota/status`, and the serial monitor where available.
- Record each run as: date, firmware version, board, scenario ID, result (pass / fail / blocked), and the log excerpt for any failure. File failures as GitHub issues referencing the scenario ID.
- Times below are firmware constants; allow a few seconds of slack for the 10 s maintenance loop and the 1 s upload-task idle wait.

## Link loss

### LINK-1 — Wi-Fi access point disappears and returns

1. Device in station mode with at least one backend enabled and samples flowing.
2. Power off the access point for ~5 minutes, then restore it.

Expected:

- `/diagnostics` shows `station_connected = false`, the disconnect reason, and an increasing `reconnect_attempt_count`; reconnect attempts follow `10 s → 20 s → 40 s → 80 s → 160 s → 300 s` (cap).
- Samples keep being recorded while offline (`queued_sample_count` grows up to `CONFIG_AIR360_MEASUREMENT_QUEUE_DEPTH`; beyond that the oldest are dropped and counted in `measurement_queue_dropped_count`).
- After the AP returns the station reconnects without a reboot, the counter resets, and backends drain the backlog; no window is delivered twice to a backend that had already acknowledged it.

### LINK-2 — Cellular-only cold boot (no Wi-Fi credentials)

1. Cellular enabled, station SSID empty, valid SIM, system time invalid (cold power-up).
2. Boot and wait for PPP.

Expected: PPP connects, SNTP synchronizes over PPP with the configured server (`time.md`), Unix time becomes valid, and uploads start. Retry every 10 s if the first SNTP attempt fails.

### LINK-3 — Cellular registration loss

1. Device on PPP; shield the antenna or remove it for ~3 minutes, then restore.

Expected: reconnect follows the cellular backoff table (`10 s, 30 s, 1 min, …`), hard retry is logged after 2 minutes of continuous failure, no PWRKEY cycle within this window; PPP returns and uploads resume.

## Sensor disconnect

### SENS-1 — UART PM sensor unplugged (SDS011 or PMSX003)

1. Sensor polling normally after warmup.
2. Unplug its UART cable for several poll intervals, then reconnect.

Expected:

- At most one queued sample from the last valid frame; no further samples while unplugged (the cached reading is not re-queued with new timestamps).
- The sensor check on `/` turns `stale` once the latest sample is older than 3 poll intervals (minimum 15 s); status text reads "No … UART data received.".
- After reconnecting, fresh frames are queued again and the check returns to `ok`.

### SENS-2 — GPS loses fix

1. GPS with a valid fix; cover the antenna until the fix is lost.

Expected: coordinates are no longer repeated as new samples; satellites/HDOP may still update; status reads "No GPS fix yet." or "No new GPS data received."; with the module unplugged the check turns `stale`.

### SENS-3 — I2C sensor unplugged and replugged

Expected: three consecutive poll failures trigger driver re-init with backoff; after 16 consecutive init/poll failures the sensor is `kFailed` until Apply or reboot. Replugging before that point recovers to polling without a reboot.

### SENS-4 — Sensor Apply and replacement on the same UART

1. Configure MH-Z19B on UART2, Apply, wait for readings.
2. Edit the sensor (e.g. poll interval) and Apply again; then replace it with SDS011 on UART2 and Apply.

Expected: every Apply succeeds without a reboot and without "UART driver already installed" errors; the replacement sensor reports readings.

### SENS-5 — Maintenance action during Apply

1. Schedule SCD30 forced recalibration (or SPS30 fan cleaning) and Apply.
2. While it runs, change another sensor's poll interval and Apply.

Expected: the finished action is not re-run after the second Apply (except in the documented narrow window, see [sensors/maintenance-actions.md](sensors/maintenance-actions.md)); the selector returns to "None"; after reboot the stored config matches the page.

## OTA interruption

### OTA-1 — Power loss during upload

1. Start an OTA upload; cut power at roughly 50 % progress.

Expected: the device boots the previous, unchanged image; `/ota/status` shows no committed update; a new upload succeeds.

### OTA-2 — Power loss after commit, before confirmation

1. Complete an upload (device reboots into the new slot); cut power before the web server is up on the first boot.

Expected: the image stays in `PENDING_VERIFY`; the bootloader either boots it again (and it confirms itself once boot completes) or rolls back if it crashed. The device never ends without a bootable image.

### OTA-3 — Second upload before the deferred reboot

1. Upload an image; immediately (within ~1.5 s of success) POST a second image to `/ota`.

Expected: the second upload gets `409 Conflict`; the device reboots into the first image.

### OTA-4 — New image fails a fatal boot step

1. Build an image whose web server cannot start (for example a temporarily invalid HTTP port) and install it via OTA.

Expected: the first boot logs the fatal step, `runFailedBootLoop()` rolls the unverified image back, and the device reboots into the previous image instead of idling with a red LED.

### OTA-5 — Operator rollback

`POST /ota/rollback?confirm=yes` on a device with a previous valid slot reboots into it; during a transfer or while a committed update awaits its reboot it returns `409`.

## Sustained operation

### SOAK-1 — 72-hour run

Device with the full sensor set and all used backends enabled, in normal conditions.

Record at start, every 24 h, and at the end from the `/diagnostics` raw JSON: `uptime_ms` (no unexpected resets), `heap_free_bytes`, `heap_min_free_bytes`, `heap_largest_block_bytes`, the task stack high-water marks, `queued_sample_count`, `measurement_queue_dropped_count`, and per-backend last success and retry counts.

Pass: no reboot, no monotonic heap decline beyond normal fragmentation, no task stack watermark approaching its limit, every backend has recent successes, no watchdog messages in the log.

### SOAK-2 — Repeated SNTP checks under load

During normal operation press "Check SNTP" repeatedly (also during boot time sync). Expected: each press returns a result or "A time sync is already in progress", never a crash; afterwards time sync continues with the configured server.

## Pending checks for recent fixes

| Fix | Scenario | Status |
|-----|----------|--------|
| #75 cached UART/GPS readings not re-queued | SENS-1, SENS-2 | Pending |
| #76 MH-Z19B UART owned by `UartPortManager` | SENS-4 | Pending |
| #77 sensor Apply vs maintenance completion | SENS-5 | Pending |
| #82 SNTP lifecycle lock | SOAK-2 | Pending |
| #84 OTA rejected after commit until reboot | OTA-3 | Pending |
| #85 rollback of an unverified image on fatal boot failure | OTA-4 | Pending |
| #74 cellular-only time sync (fixed earlier) | LINK-2 | Pending |

Update the status column (with date and firmware version) as checks are run.
