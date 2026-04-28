# Firmware ADRs

This directory contains firmware architecture decision records for Air360.

These documents are firmware-specific architecture decision records. Some are still planned or proposed, while others describe implemented decisions. `firmware/` remains the source of truth for what is already implemented.

---

## Proposed — New features

New capabilities not present in the current firmware.

- [`proposed-ota-firmware-update-adr.md`](proposed-ota-firmware-update-adr.md)
  OTA firmware update via the web UI using ESP-IDF native OTA API.
- [`proposed-station-web-authentication-adr.md`](proposed-station-web-authentication-adr.md)
  Optional station-mode web UI authorization using HTTP Basic authentication.
- [`proposed-air360-api-device-authentication-adr.md`](proposed-air360-api-device-authentication-adr.md)
  HMAC-signed Air360 API uploads with TOFU provisioning and local pairing-code recovery.
