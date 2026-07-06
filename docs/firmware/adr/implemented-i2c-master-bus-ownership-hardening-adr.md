# I2C Master Bus Ownership Hardening ADR

## Status

Implemented (steps 1–3). Step 4 (migrating AHT30 off the borrowed handle) remains open as an optional, independently schedulable follow-up.

Notes from implementation: the upstream bug was re-verified against the vendored `managed_components/esp-idf-lib__i2cdev/i2cdev.c` — `i2c_dev_delete_mutex()` nulls `dev->dev_handle` in its device-removal block before the ref-count decrement condition `dev->dev_handle != NULL` is evaluated, so the `i2c_del_master_bus()` path is confirmed unreachable in 2.1.1. The pin uses the exact form `"2.1.1"` (no caret) in `firmware/main/idf_component.yml`. The upgrade gate lives in the "Co-change expectations" section of `firmware/CLAUDE.md`.

## Decision Summary

Make the lifetime contract of borrowed I2C master bus handles explicit and safe against `i2cdev` upgrades. Pin `esp-idf-lib/i2cdev` as a direct dependency with an exact version, document the bus-lifetime invariant at `I2cBusManager::getMasterBusHandle()`, and treat any future `i2cdev` version bump as a change that requires re-auditing `i2c_dev_delete_mutex()` before merging. As the long-term direction, prefer migrating the remaining new-master-API drivers (AHT30) onto `i2cdev` so the number of borrowed-handle consumers shrinks instead of growing.

## Context

`i2cdev` (esp-idf-lib, currently locked at **2.1.1**) owns the I2C master bus for all sensor drivers. Two drivers do not go through `i2cdev` for transactions:

- AHT30 (`firmware/main/src/sensors/drivers/aht30_sensor.cpp:38`)
- BMP390 (`firmware/main/src/sensors/drivers/bmp390_sensor.cpp:40`)

Both borrow the raw `i2c_master_bus_handle_t` via `I2cBusManager::getMasterBusHandle()` (`firmware/main/src/sensors/transport_binding.cpp:79`) and register their own devices on that bus with the native `i2c_master_bus_add_device()` API. `i2cdev` knows nothing about these devices: its per-port `ref_count` only tracks devices added through `i2cdev` itself.

`i2cdev` 2.1.1 contains cleanup logic in `i2c_dev_delete_mutex()` that is supposed to delete the master bus (`i2c_del_master_bus()`) when the last `i2cdev`-registered device is removed. Today that logic is **dead code due to an upstream bug**: the function nulls `dev->dev_handle` before evaluating the decrement condition `dev->dev_handle != NULL`, so `ref_count` is never decremented and the bus is never deleted (see `managed_components/esp-idf-lib__i2cdev/i2cdev.c`, device-removal block at the top of `i2c_dev_delete_mutex()` versus the ref-count block below it).

This bug is currently the only thing making the borrowed handles safe. The reachable failure scenario if upstream fixes it:

1. A configuration includes at least one `i2cdev`-based sensor (e.g. BME280, SHT3x) and at least one borrower (AHT30 or BMP390).
2. The `i2cdev`-based sensor hits the 3-consecutive-poll-failure threshold and is re-initialized individually by `SensorManager`; its `teardown()` calls `*_free_desc()` → `i2c_dev_delete_mutex()`.
3. With fixed upstream logic, `ref_count` drops to zero and `i2cdev` calls `i2c_del_master_bus()` while AHT30/BMP390 still hold device handles on that bus.
4. The next borrower transaction is a use-after-free.

Exposure is amplified by dependency management: `i2cdev` is not a direct dependency of `main` — every esp-idf-lib sensor component pulls it with an unconstrained `version: '*'`. Only `firmware/dependencies.lock` holds it at 2.1.1; any `idf.py update-dependencies` may silently bump it.

## Goals

- Remove the silent dependency of runtime memory safety on an upstream bug.
- Make an `i2cdev` version bump a deliberate, reviewed event instead of a lockfile side effect.
- Keep `i2cdev` the single bus owner (established by the bus lazy-install fix in `getMasterBusHandle()`).
- Keep the fix cheap; no driver rewrites are required for the first step.

## Non-Goals

- Forking or patching the vendored `i2cdev` component.
- Rewriting BMP390 away from `k0i05/esp_bmp390` (its calibration math makes replacement costly).
- Introducing a general handle-invalidation/notification mechanism between drivers.

## Architectural Decision

### 1. Pin `esp-idf-lib/i2cdev` explicitly

Add `esp-idf-lib/i2cdev: 2.1.1` as a direct dependency in `firmware/main/idf_component.yml` with a comment stating why the version is load-bearing. This overrides the transitive `'*'` constraints and makes an upgrade an explicit manifest edit.

### 2. Document the invariant at the borrow site

Extend the comment block in `I2cBusManager::getMasterBusHandle()` and the "Bus ownership" section of `docs/firmware/transport-binding.md` with the invariant:

> Borrowed master bus handles remain valid only while `i2cdev` never deletes the bus. As of i2cdev 2.1.1 the delete path in `i2c_dev_delete_mutex()` is unreachable (upstream nulls `dev_handle` before checking it). Any i2cdev upgrade must re-verify this before merging.

### 3. Gate future upgrades

Add a checklist item to `firmware/CLAUDE.md` co-change expectations: bumping `esp-idf-lib/i2cdev` requires re-reading `i2c_dev_delete_mutex()` and confirming either (a) the bus still survives removal of the last i2cdev device, or (b) the borrower drivers have been migrated off borrowed handles first.

### 4. Long-term: shrink the borrower set

AHT30's protocol is trivial (one trigger command, 6-byte read, CRC). Rewriting it as an in-house `i2cdev`-based driver removes one borrower and the `espressif__aht30`/`sensor_hub`/`i2c_bus` transitive component chain. BMP390 then remains the only borrower. This step is optional and independently schedulable.

## Affected Files

- `firmware/main/idf_component.yml` — add pinned `esp-idf-lib/i2cdev: 2.1.1`
- `firmware/main/src/sensors/transport_binding.cpp` — extend the invariant comment
- `docs/firmware/transport-binding.md` — extend the "Bus ownership" section
- `firmware/CLAUDE.md` — add the upgrade-gate checklist item
- (optional, step 4) `firmware/main/src/sensors/drivers/aht30_sensor.cpp` and its docs

## Alternatives Considered

### Option A. Do nothing

The lockfile already pins 2.1.1. Rejected: `update-dependencies` bumps transitive `'*'` constraints silently, and nothing records that the version is load-bearing.

### Option B. Keep a sentinel i2cdev device registered to hold `ref_count > 0`

Rejected: `i2cdev` only counts devices after their first transaction (`i2c_setup_device` runs lazily), so a sentinel would need to perform real bus traffic against a phantom address, and the mechanism it protects against is currently unreachable anyway.

### Option C. Have `I2cBusManager` create and own the bus outright, bypassing i2cdev

Rejected: `i2cdev`'s `i2c_setup_port()` unconditionally calls `i2c_new_master_bus()` and fails on an occupied port — all i2cdev-based drivers would break. This would effectively require forking i2cdev.

### Option D. Pin + document + gate upgrades (accepted)

Cheapest change that converts an invisible invariant into a visible, enforced one, with an optional path (step 4) that reduces the exposed surface over time.

## Practical Conclusion

The system is safe today, but by accident rather than by design. Pinning the version, writing the invariant down at the borrow site, and gating upgrades makes the accident survivable; migrating AHT30 onto i2cdev later makes it mostly irrelevant.
