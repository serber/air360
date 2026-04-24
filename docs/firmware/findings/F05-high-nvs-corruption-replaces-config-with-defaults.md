# Finding F05: NVS corruption replaces configuration with defaults

## Status

Confirmed audit finding. Not implemented.

## Scope

Task file for making persisted firmware configuration resilient to NVS blob corruption.

## Source of truth in code

- `firmware/main/src/config_repository.cpp`
- `firmware/main/src/sensors/sensor_config_repository.cpp`
- `firmware/main/src/uploads/backend_config_repository.cpp`
- `firmware/main/src/cellular_config_repository.cpp`

## Read next

- `docs/firmware/nvs.md`
- `docs/firmware/configuration-reference.md`

**Priority:** High
**Category:** Reliability / Storage
**Files / symbols:** `ConfigRepository::loadOrCreate`, `SensorConfigRepository::loadOrCreate`, `BackendConfigRepository::loadOrCreate`, `CellularConfigRepository::loadOrCreate`

## Problem

Each config repository stores one blob and replaces it with defaults when the blob is invalid, incompatible, or has the wrong size. There is no backup copy, migration path, or quarantine of the invalid blob.

## Why it matters

A single corrupted NVS blob can erase Wi-Fi credentials, cellular APN/PIN, sensor configuration, or backend upload configuration. On a field device, this can make the node unreachable or silently stop uploads after a brownout or interrupted flash write.

## Evidence

- `firmware/main/src/config_repository.cpp:199` loads `device_cfg`; invalid data is replaced with defaults.
- `firmware/main/src/sensors/sensor_config_repository.cpp:52` loads `sensor_cfg`; invalid data is replaced with defaults.
- `firmware/main/src/uploads/backend_config_repository.cpp:116` loads `backend_cfg`; invalid data is replaced with defaults.
- `firmware/main/src/cellular_config_repository.cpp:69` loads `cellular_cfg`; invalid data is replaced with defaults.
- All repositories use `nvs_set_blob()` followed by `nvs_commit()` for a single primary key.

## Recommended Fix

Use a two-slot committed config scheme with CRC and sequence number. Load the newest valid slot; only fall back to defaults when both slots are invalid.

## Where To Change

- `firmware/main/src/config_repository.cpp`
- `firmware/main/src/sensors/sensor_config_repository.cpp`
- `firmware/main/src/uploads/backend_config_repository.cpp`
- `firmware/main/src/cellular_config_repository.cpp`
- Corresponding headers if metadata structs are added
- `docs/firmware/nvs.md`
- `docs/firmware/configuration-reference.md`

## How To Change

1. Wrap each stored blob in a small header:
   - magic
   - schema version
   - payload size
   - sequence number
   - CRC32
2. Store `*_a` and `*_b` keys for each config family.
3. On save:
   - read current active sequence
   - write the inactive slot with `sequence + 1`
   - commit
4. On load:
   - validate both slots
   - choose newest valid slot
   - if one slot is bad, rewrite it from the good slot
   - if both are bad, use defaults and record a diagnostic event
5. Add explicit schema migration instead of blanket defaulting when only the schema version changed.

## Example Fix

```cpp
struct StoredBlobHeader {
    uint32_t magic;
    uint16_t schema_version;
    uint16_t payload_size;
    uint32_t sequence;
    uint32_t crc32;
};
```

## Validation

- Host-test the repository logic with fake NVS:
  - primary valid, backup valid, newer primary wins
  - primary corrupt, backup valid, backup loads and primary is repaired
  - both corrupt, defaults load and a diagnostic is recorded
  - old schema migrates without dropping compatible fields
- Hardware test: interrupt power during repeated config saves and verify at least one valid slot survives.
- Run `source ~/.espressif/v6.0/esp-idf/export.sh && idf.py build` from `firmware/`.
- Run `python3 scripts/check_firmware_docs.py`.

## Risk Of Change

Medium. The implementation is localized, but migration must be careful.

## Dependencies

None.

## Suggested Agent Type

ESP-IDF agent / testing agent
