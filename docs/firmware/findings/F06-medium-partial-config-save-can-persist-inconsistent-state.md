# Finding F06: Config save can persist a partial state

## Status

Confirmed audit finding. Not implemented.

## Scope

Task file for preventing partial multi-repository configuration commits from the web UI.

## Source of truth in code

- `firmware/main/src/web/web_mutating_routes.cpp`
- `firmware/main/src/config_repository.cpp`
- `firmware/main/src/cellular_config_repository.cpp`

## Read next

- `docs/firmware/web-ui.md`
- `docs/firmware/nvs.md`
- `docs/firmware/configuration-reference.md`

**Priority:** Medium
**Category:** Reliability / Storage
**Files / symbols:** `WebServer::handleConfig`, `ConfigRepository::save`, `CellularConfigRepository::save`

## Problem

`POST /config` saves `DeviceConfig` first and `CellularConfig` second. If the device config save succeeds but the cellular config save fails, the handler returns an error while the device config has already been committed to NVS.

## Why it matters

The UI presents the operation as failed, but a reboot will use a partially updated configuration. A user can end up with changed Wi-Fi settings but old cellular settings, or vice versa in future multi-repository saves. Partial configuration commits are hard to diagnose remotely.

## Evidence

- `firmware/main/src/web/web_mutating_routes.cpp:687` builds `DeviceConfig updated`.
- `firmware/main/src/web/web_mutating_routes.cpp:697` calls `server->config_repository_->save(updated)`.
- `firmware/main/src/web/web_mutating_routes.cpp:724` builds and saves `CellularConfig updated_cellular`.
- If `cellular_save_err != ESP_OK`, the handler renders an error and returns, but the earlier device config write remains committed.

## Recommended Fix

Make multi-repository config changes transactional at the application level. At minimum, validate and stage all records before committing any, and record a pending config transaction marker so boot can complete or roll back.

## Where To Change

- `firmware/main/src/web/web_mutating_routes.cpp`
- `firmware/main/src/config_repository.cpp`
- `firmware/main/src/cellular_config_repository.cpp`
- `docs/firmware/web-ui.md`
- `docs/firmware/nvs.md`

## How To Change

1. Validate both `updated` and `updated_cellular` before writing.
2. Add a `ConfigTransactionRepository` or small transaction marker in NVS:
   - marker contains intended device and cellular sequence numbers
   - save both records
   - clear marker only after both commits succeed
3. On boot, if marker exists:
   - load newest valid complete pair, or
   - roll back to last complete pair if using dual-slot config from F05
4. If full transaction support is too large for the first patch, at least change the UI message to state exactly which part was saved and which failed.

## Example Fix

```cpp
if (!config_repository_->isValid(updated) ||
    !cellular_config_repository_->isValid(updated_cellular)) {
    return render_validation_error();
}

ConfigTransaction tx(nvs);
ESP_RETURN_ON_ERROR(tx.begin(), kTag, "begin config transaction");
ESP_RETURN_ON_ERROR(config_repository_->save(updated), kTag, "save device config");
ESP_RETURN_ON_ERROR(cellular_config_repository_->save(updated_cellular), kTag, "save cellular config");
ESP_RETURN_ON_ERROR(tx.commit(), kTag, "commit config transaction");
```

## Validation

- Fake-NVS test: inject failure on the second save and assert boot does not silently use a half-committed transaction.
- UI test: failure message accurately reports persisted state.
- Hardware test: repeatedly save config while fault-injecting NVS failure if possible.
- Run `source ~/.espressif/v6.0/esp-idf/export.sh && idf.py build` from `firmware/`.
- Run `python3 scripts/check_firmware_docs.py`.

## Risk Of Change

Medium. True rollback depends on F05-style dual-slot storage.

## Dependencies

Works best after F05.

## Suggested Agent Type

ESP-IDF agent / C++ refactoring agent
