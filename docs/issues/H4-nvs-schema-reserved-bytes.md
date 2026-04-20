# H4 — In-place NVS schema evolution via reserved bytes

- **Severity:** High
- **Area:** Persistence / compatibility
- **Files:**
  - `firmware/main/include/air360/config_repository.hpp` (`DeviceConfig`, `reserved1[2]`)
  - `firmware/main/src/config_repository.cpp`

## What is wrong

`DeviceConfig` is a raw POD written to NVS as a single blob. It carries `uint8_t reserved1[2]`. Previously `ble_adv_interval_index` lived as `reserved1[0]` — a byte was carved out of the reserved region *without* bumping `schema_version`.

Validation on load checks `magic`, `schema_version`, `record_size`, and null-termination on string fields. None of those catches a field being silently reinterpreted.

## Why it matters

- Any device flashed with an intermediate firmware wrote the reserved byte as zero (or worse, whatever RAM garbage). After the upgrade that byte is now read as `ble_adv_interval_index`.
- Because `uint8_t` has no illegal values in-range, validation passes.
- The device silently picks an unintended BLE advertising interval. In the worst case, an out-of-table index that is bounds-clamped but not the user's last setting.

## Consequences on real hardware

- Field devices reboot after an OTA to unexpected BLE intervals.
- No error surfaces; the only signal is "the interval isn't what I set."
- The same pattern will repeat every time someone carves a new field out of reserved bytes.

## Fix plan

1. **Bump `schema_version` for any field-meaning change.** Make this a hard rule in `AGENTS.md` / `firmware-change-checklist`.
2. **Introduce a migration framework.** In `config_repository.cpp`:
   ```cpp
   struct SchemaMigration {
       uint16_t from_version;
       uint16_t to_version;
       void (*migrate)(DeviceConfig&);
   };

   static constexpr SchemaMigration kMigrations[] = {
       { 3, 4, &migrateV3toV4 },   // reserved1[0] → ble_adv_interval_index
       { 4, 5, &migrateV4toV5 },
   };
   ```
3. **On load:** if `loaded.schema_version < kCurrentSchemaVersion`, walk migrations in order. Each migration:
   - Clamps or zeroes the newly-meaningful field to a safe default.
   - Updates `schema_version`.
   - Writes back the migrated blob to NVS.
4. **For the specific reserved1→ble_adv_interval_index case:** migrating from the pre-bump version should set `ble_adv_interval_index = kBleAdvIntervalDefaultIndex`.
5. **Add a CI check.** A Python script in `scripts/` parses `DeviceConfig` layout (via a small `.layout.json` sibling file) and fails the build if the struct layout changed but `schema_version` did not bump.
6. **Do this for every `*Config` blob:** `DeviceConfig`, `SensorConfigList`, `BackendConfigList`, `CellularConfig`. All four need the migration framework; the layout-check script should cover all of them.
7. **Document** in `docs/firmware/nvs.md` the current schema version, history of bumps, and the migration contract.

## Verification

- Host test: synthesize a blob with `schema_version = N-1`, load, assert it is migrated to `N` with the expected default.
- Flash a device with vN-1, OTA to vN, power cycle — `ble_adv_interval_index` equals the configured default, not garbage.
- CI script fails on a deliberate layout change without a version bump.

## Related

- H5 — backup blob design complements this.
- M4 — return-value discipline (`loaded_from_storage`) is related to surfacing migration events.
