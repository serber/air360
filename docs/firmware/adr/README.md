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

## Proposed — New features

New capabilities not present in the current firmware.

- [`proposed-station-web-authentication-adr.md`](proposed-station-web-authentication-adr.md)
  Optional station-mode web UI authorization using HTTP Basic authentication.

## Proposed — Technical debt

Cleanups and hardening of existing mechanisms, sourced from code review.

- [`proposed-i2c-master-bus-ownership-hardening-adr.md`](proposed-i2c-master-bus-ownership-hardening-adr.md)
  Pin `esp-idf-lib/i2cdev` and make the borrowed master-bus-handle lifetime invariant explicit; borrowed handles (AHT30, BMP390) currently stay valid only because an upstream i2cdev bug keeps the bus-delete path unreachable.
- [`proposed-sensor-driver-i2c-hygiene-adr.md`](proposed-sensor-driver-i2c-hygiene-adr.md)
  Centralise I2C descriptor electrical policy in an `I2cBusManager` helper, share the Pa→hPa constant, remove write-only `record_` members, and log teardown failures consistently.

## Deferred — Production hardening

Hardening ideas that are not the current first implementation direction.

- [`proposed-air360-api-device-authentication-adr.md`](proposed-air360-api-device-authentication-adr.md)
  HMAC-signed Air360 API uploads with TOFU provisioning and local pairing-code recovery.
