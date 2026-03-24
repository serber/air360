# Phase 2 Hardware Onboarding Notes

## Target

- Board: `ESP32-S3-DevKitC-1`
- Runtime: ESP-IDF 6.x
- Build system: native ESP-IDF CMake

## Current implementation choices

- `firmware/` remains the codebase root for the replacement firmware.
- Config persistence still uses NVS, but the stored `DeviceConfig` now includes:
  - device name
  - HTTP port
  - Wi-Fi station SSID/password
  - setup AP SSID/password
  - local auth placeholder flag
- Boot now resolves network mode from stored config:
  - if station credentials exist, the firmware attempts station join first
  - if credentials are missing or join fails, it falls back to setup AP mode
- Setup AP mode starts at `192.168.4.1` and serves `/`, `/status`, and `/config`.
- In setup AP mode, `/` redirects to `/config`.
- `POST /config` saves config to NVS and reboots the device.
- If Wi-Fi SSID is saved as empty, the next boot returns to setup AP mode.
- Captive-portal DNS and wildcard DNS are still not implemented in this phase.

## Expected bring-up flow

1. Change to `firmware/`.
2. Open the project in VS Code with the ESP-IDF extension configured for the local ESP-IDF installation.
3. Select target `esp32s3` in the extension when creating `sdkconfig` for the first time or after removing it.
4. Build from VS Code.
5. Find the serial port if needed, for example with `ls /dev/cu.*` on macOS.
6. Flash and monitor from VS Code.
7. On first boot, the firmware writes a default config record to NVS.
8. With no saved station SSID, the device starts setup AP SSID `air360`.
9. Browse to `http://192.168.4.1/config` for onboarding or `http://192.168.4.1/status` for JSON status.
10. Submit Wi-Fi credentials and basic device settings through `/config`.
11. After reboot, confirm the device joins the configured Wi-Fi network.
12. Open `/`, `/config`, or `/status` on the DHCP address assigned on that network.

## Validation checklist

- Confirm deterministic boot logs for watchdog, NVS, network core, config load, mode resolution, and HTTP server start.
- Confirm first boot with empty station config enters setup AP mode on `192.168.4.1`.
- Confirm `/config` renders in setup AP mode and accepts a valid POST.
- Confirm successful save triggers reboot and `config_loaded_from_storage=true` on the next boot.
- Confirm valid station credentials lead to normal-mode join after reboot.
- Confirm `/status` reports:
  - `network_mode`
  - `station_config_present`
  - `station_connect_attempted`
  - `station_connected`
  - `lab_ap_active`
- Confirm `boot_count` increments across reboots.
- Confirm invalid or empty Wi-Fi config leaves the device recoverable through setup AP mode.

## Environment note

Do not assume a fixed ESP-IDF install path. In this repository, the expected day-to-day workflow is through the VS Code ESP-IDF extension bound to the local installation.
