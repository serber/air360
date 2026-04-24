# Finding F07: OTA metadata exists without OTA app slots

## Status

Confirmed audit finding. Not implemented.

## Scope

Task file for aligning the firmware partition layout with OTA support expectations.

## Source of truth in code

- `firmware/partitions.csv`
- `firmware/sdkconfig.defaults`

## Read next

- `docs/firmware/ARCHITECTURE.md`
- `docs/firmware/startup-pipeline.md`
- `docs/firmware/adr/proposed-ota-firmware-update-adr.md`

**Priority:** Medium
**Category:** Build / Reliability
**Files / symbols:** `firmware/partitions.csv`, `docs/firmware/adr/proposed-ota-firmware-update-adr.md`

## Problem

The partition table includes an `otadata` partition but only one application partition named `factory`. There are no `ota_0` and `ota_1` application slots.

## Why it matters

The layout advertises OTA metadata but cannot safely perform ESP-IDF native OTA because there is no inactive app slot to write. Adding OTA code without fixing the partition table would fail at runtime or risk update/rollback confusion. For a long-lived IoT device, lack of a working OTA path is a maintainability and field-service risk.

## Evidence

`firmware/partitions.csv`:

```text
nvs,      data, nvs,     0x9000,   0x6000,
otadata,  data, ota,     0xf000,   0x2000,
phy_init, data, phy,     0x11000,  0x1000,
factory,  app,  factory, 0x20000,  1792K,
storage,  data, spiffs,  0x1e0000, 0x60000,
```

`docs/firmware/adr/proposed-ota-firmware-update-adr.md` already states that OTA requires replacing the single factory slot with dual OTA slots.

## Recommended Fix

Decide whether OTA is in scope for the current product milestone. If yes, convert the partition table to dual OTA slots and add rollback validation. If no, remove or document the unused OTA metadata partition to avoid false assumptions.

## Where To Change

- `firmware/partitions.csv`
- `firmware/sdkconfig.defaults`
- `firmware/main/CMakeLists.txt` if OTA code is added
- `firmware/main/src/app.cpp` for `esp_ota_mark_app_valid_cancel_rollback()`
- `docs/firmware/ARCHITECTURE.md`
- `docs/firmware/startup-pipeline.md`
- `docs/firmware/PROJECT_STRUCTURE.md`

## How To Change

1. For OTA support, use a layout with `ota_0` and `ota_1`.
2. Enable app rollback support in sdkconfig if using ESP-IDF rollback.
3. Mark app valid only after core boot health checks pass.
4. Add firmware image metadata checks before writing.
5. Add OTA failure and rollback diagnostics to status JSON.

## Example Fix

```text
# Name,   Type, SubType, Offset,   Size,     Flags
nvs,      data, nvs,     0x9000,   0x6000,
otadata,  data, ota,     0xf000,   0x2000,
phy_init, data, phy,     0x11000,  0x1000,
ota_0,    app,  ota_0,   0x20000,  4M,
ota_1,    app,  ota_1,            4M,
storage,  data, spiffs,           1M,
```

Adjust sizes after checking current binary size and desired storage allocation.

## Validation

- Run `idf.py size` after repartitioning.
- Flash from factory to `ota_0` and `ota_1`.
- Force a crash before app validation and confirm rollback.
- Confirm NVS and measurement storage survive update.
- Run `source ~/.espressif/v6.0/esp-idf/export.sh && idf.py build` from `firmware/`.
- Run `python3 scripts/check_firmware_docs.py`.

## Risk Of Change

High if implemented, because repartitioning can erase storage and requires migration planning. Low if only documenting/removing unused metadata.

## Dependencies

Coordinate with F04 before assigning storage partition sizes.

## Suggested Agent Type

ESP-IDF agent / CI agent / documentation agent
