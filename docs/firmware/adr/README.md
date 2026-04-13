# Firmware ADRs

This directory contains firmware architecture decision records for Air360.

These documents are firmware-specific architecture decision records. Some are still planned or proposed, while others describe implemented decisions. `firmware/` remains the source of truth for what is already implemented.

---

## Implemented

Decisions that are reflected in the current codebase.

- [`implemented-measurement-runtime-separation-adr.md`](implemented-measurement-runtime-separation-adr.md)
  Split between sensor lifecycle management and measurement runtime ownership.
- [`implemented-live-sensor-reconfiguration-adr.md`](implemented-live-sensor-reconfiguration-adr.md)
  No-reboot sensor apply behavior built on top of the measurement/runtime split.
- [`implemented-overview-health-status-adr.md`](implemented-overview-health-status-adr.md)
  Aggregated device health summary for the `Overview` page and `/status`.
- [`implemented-measurement-store-sensor-index-adr.md`](implemented-measurement-store-sensor-index-adr.md)
  O(1) per-sensor counter map in `MeasurementStore` replacing O(n) linear scans.
- [`implemented-mutex-constructor-init-adr.md`](implemented-mutex-constructor-init-adr.md)
  Fix the racy lazy `ensureMutex()` pattern — initialize mutexes in constructors.
- [`implemented-configurable-sntp-server-adr.md`](implemented-configurable-sntp-server-adr.md)
  User-configurable SNTP server with a runtime `Check SNTP` action on the Device page.
- [`implemented-static-ip-configuration-adr.md`](implemented-static-ip-configuration-adr.md)
  Optional static IPv4 configuration for Wi-Fi station mode.
- [`implemented-cellular-uplink-adr.md`](implemented-cellular-uplink-adr.md)
  Cellular uplink abstractions — `CellularManager`, `CellularConfig`, network bearer integration.

---

## Proposed — Architecture improvements

Refactoring and correctness improvements to the existing codebase.

- [`proposed-web-server-handler-split-adr.md`](proposed-web-server-handler-split-adr.md)
  Split `web_server.cpp` (79 KB) and `status_service.cpp` (36 KB) into focused files.
- [`proposed-upload-retry-backoff-adr.md`](proposed-upload-retry-backoff-adr.md)
  Replace fixed retry interval with capped exponential backoff per backend.
- [`proposed-sensor-log-state-transitions-adr.md`](proposed-sensor-log-state-transitions-adr.md)
  Log sensor errors on state transitions only, not on every 250 ms poll iteration.
- [`proposed-non-blocking-sensor-init-adr.md`](proposed-non-blocking-sensor-init-adr.md)
  Enforce non-blocking `init()` contract — warm-up delays via `warmup_ms_out` instead of `vTaskDelay()`.

---

## Proposed — New features

New capabilities not present in the current firmware.

- [`proposed-ota-firmware-update-adr.md`](proposed-ota-firmware-update-adr.md)
  OTA firmware update via the web UI using ESP-IDF native OTA API.
- [`proposed-measurement-queue-persistence-adr.md`](proposed-measurement-queue-persistence-adr.md)
  Persist the upload queue to SPIFFS so queued samples survive reboot.
- [`proposed-wifi-auto-reconnect-adr.md`](proposed-wifi-auto-reconnect-adr.md)
  Automatic Wi-Fi reconnection with exponential backoff after station disconnect.
- [`proposed-host-unit-tests-adr.md`](proposed-host-unit-tests-adr.md)
  Host-compiled Unity test suite for `MeasurementStore`, sensor state machine, and config validation.
- [`proposed-gpio-factory-reset-adr.md`](proposed-gpio-factory-reset-adr.md)
  GPIO-triggered factory reset at boot for hardware recovery without reflash.
