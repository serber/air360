# Firmware ADRs

This directory contains firmware architecture decision records for Air360.

These documents are firmware-specific architecture decision records. Some are still planned or proposed, while others describe implemented decisions. `firmware/` remains the source of truth for what is already implemented.

---

## Implemented

Decisions already present in the current firmware.

- [`implemented-air360-api-upload-secret-adr.md`](implemented-air360-api-upload-secret-adr.md)
  Firmware-generated Air360 API upload secret with bearer authentication and user-saved reset recovery.
- [`implemented-ota-firmware-update-adr.md`](implemented-ota-firmware-update-adr.md)
  OTA firmware update via the web UI using the ESP-IDF native `app_update` API with automatic rollback.
- [`implemented-sensor-driver-i2c-hygiene-adr.md`](implemented-sensor-driver-i2c-hygiene-adr.md)
  Centralised I2C descriptor electrical policy (`applyDescriptorDefaults()`), shared Pa→hPa constant, removal of write-only `record_` members, and consistent teardown-failure logging.
- [`implemented-i2c-master-bus-ownership-hardening-adr.md`](implemented-i2c-master-bus-ownership-hardening-adr.md)
  Exact pin of `esp-idf-lib/i2cdev` 2.1.1, documented borrowed master-bus-handle lifetime invariant (AHT30, BMP390), and an upgrade gate requiring re-audit of `i2c_dev_delete_mutex()` before any i2cdev bump.

## Proposed — New features

New capabilities not present in the current firmware.

- [`proposed-station-web-authentication-adr.md`](proposed-station-web-authentication-adr.md)
  Optional station-mode web UI authorization using HTTP Basic authentication.

## Deferred — Production hardening

Hardening ideas that are not the current first implementation direction.

- [`proposed-air360-api-device-authentication-adr.md`](proposed-air360-api-device-authentication-adr.md)
  HMAC-signed Air360 API uploads with TOFU provisioning and local pairing-code recovery.
