# Finding F09: Sensitive credentials are stored in plaintext NVS

## Status

Confirmed audit finding. Not implemented.

## Scope

Task file for hardening storage of firmware credentials in production builds.

## Source of truth in code

- `firmware/main/include/air360/config_repository.hpp`
- `firmware/main/include/air360/cellular_config_repository.hpp`
- `firmware/main/include/air360/uploads/backend_config.hpp`
- `firmware/sdkconfig.defaults`

## Read next

- `docs/firmware/nvs.md`
- `docs/firmware/configuration-reference.md`
- `docs/firmware/user-guide.md`

**Priority:** Medium
**Category:** Security
**Files / symbols:** `DeviceConfig`, `CellularConfig`, `BackendRecord`, config repositories, `firmware/sdkconfig.defaults`

## Problem

Wi-Fi passwords, setup AP password, cellular PAP password, SIM PIN, and backend Basic Auth passwords are stored as plaintext fields inside NVS blobs. The build defaults do not enable flash encryption, NVS encryption, or secure boot.

## Why it matters

Anyone with physical flash access, a firmware dump, or debug access can recover network and backend credentials. This is a common production IoT attack path.

## Evidence

- `firmware/main/include/air360/config_repository.hpp` includes `wifi_sta_password` and `lab_ap_password` fields.
- `firmware/main/include/air360/cellular_config_repository.hpp` includes `password` and `sim_pin`.
- `firmware/main/include/air360/uploads/backend_config.hpp` includes `BackendAuthConfig::basic_password`.
- `firmware/main/src/config_repository.cpp:44`, `firmware/main/src/cellular_config_repository.cpp:16`, and `firmware/main/src/uploads/backend_config_repository.cpp:19` store whole structs via `nvs_set_blob()`.
- `firmware/sdkconfig.defaults` enables PSRAM and BLE settings but does not enable flash encryption, NVS encryption, or secure boot defaults.

## Recommended Fix

Define a production security profile that enables flash encryption and secure boot, and use NVS encryption for credential namespaces where possible. Also reduce credential exposure through diagnostics and UI per F02.

## Where To Change

- `firmware/sdkconfig.defaults`
- Potential new `firmware/sdkconfig.production.defaults`
- Config repository modules if NVS encrypted partitions are added
- `firmware/partitions.csv` if an NVS keys partition is required
- `docs/firmware/configuration-reference.md`
- `docs/firmware/nvs.md`
- `docs/firmware/user-guide.md`

## How To Change

1. Add a documented production build profile:
   - secure boot v2
   - flash encryption
   - NVS encryption
   - disabled debug interfaces as appropriate
2. If keeping developer defaults open, make that explicit and prevent production images from being built accidentally with insecure defaults.
3. Avoid rendering stored secrets in the web UI.
4. Add a manufacturing/provisioning note for key generation and device recovery.

## Example Fix

Use separate defaults:

```text
firmware/sdkconfig.defaults
firmware/sdkconfig.production.defaults
```

The production defaults should enable ESP-IDF secure boot and flash encryption settings appropriate for ESP32-S3, plus NVS encryption support.

## Validation

- Build a production image from the production defaults.
- Flash and confirm NVS credentials remain readable by firmware.
- Dump flash externally and confirm plaintext credentials are not visible.
- Confirm secure boot and encryption state through boot logs and eFuse inspection.
- Run `source ~/.espressif/v6.0/esp-idf/export.sh && idf.py build` from `firmware/`.
- Run `python3 scripts/check_firmware_docs.py`.

## Risk Of Change

High for production enablement because secure boot and flash encryption affect manufacturing and recovery. Low for documenting a separate profile.

## Dependencies

F02 should be fixed so secrets are not leaked over HTTP after storage is hardened.

## Suggested Agent Type

Security agent / ESP-IDF agent / documentation agent
