# Phase 1 Hardware Boot Notes

## Target

- Board: `ESP32-S3-DevKitC-1`
- Runtime: ESP-IDF 6.x
- Build system: native ESP-IDF CMake

## Current implementation choices

- `firmware/` is the new codebase root for the replacement firmware.
- Config persistence uses NVS in Phase 1 for a small, versioned device record plus a boot counter.
- `partitions.csv` reserves a future `storage` partition for files or outbox data, but Phase 1 only depends on `nvs`.
- A lab-only SoftAP bring-up hook starts at `192.168.4.1` so `/` and `/status` can be exercised before the real onboarding flow is implemented in Phase 2.

## Expected bring-up flow

1. Change to `firmware/`.
2. Open the project in VS Code with the ESP-IDF extension configured for the local ESP-IDF installation.
3. Select target `esp32s3` in the extension when creating `sdkconfig` for the first time or after removing it.
4. Build from VS Code.
5. Find the serial port if needed, for example with `ls /dev/cu.*` on macOS.
6. Flash and monitor from VS Code.
7. On first boot, the firmware writes a default config record to NVS.
8. If lab AP is enabled, the device starts SSID `air360-phase1`.
9. Browse to `http://192.168.4.1/` for the landing page or `http://192.168.4.1/status` for JSON health data.

## Validation checklist

- Confirm deterministic boot logs for watchdog, NVS, network core, config load, AP start, and HTTP server start.
- Confirm `/status` reports `web_server_started=true`.
- Reboot once and confirm `boot_count` increments and `config_loaded_from_storage=true`.

## Environment note

Do not assume a fixed ESP-IDF install path. In this repository, the expected day-to-day workflow is through the VS Code ESP-IDF extension bound to the local installation.
