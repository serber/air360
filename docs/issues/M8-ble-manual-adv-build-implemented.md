# M8 — Manual BLE advertisement build to dodge NimBLE API churn

- **Severity:** Medium
- **Area:** Maintainability / vendor API discipline
- **Files:**
  - `firmware/main/src/ble_advertiser.cpp` (`updateAdvertisement`, `buildPayload`)

## What is wrong

Advertisement payload is built by hand as a raw byte array, with a comment explaining the choice:

> Build raw advertisement packet manually to avoid `ble_hs_adv_fields` API differences across NimBLE versions (`flags_is_present` was removed in newer ESP-IDF).

The project pins ESP-IDF 6.0 (`AGENTS.md` mandates `source ~/.espressif/v6.0/esp-idf/export.sh`). Working around vendor API churn in the presence of a pinned toolchain is needless tech debt.

## Why it matters

- Manual byte packing is a source of off-by-one, endian, and length-field bugs.
- The `buildPayload` function is 70+ lines of cast-heavy code when `ble_hs_adv_fields` would cover most of it declaratively.
- The comment rationale is not load-bearing: the NimBLE version is fixed by the project, so API churn is not a real threat.

## Consequences on real hardware

- Correct today; fragile to changes.
- Any new advertisement field (e.g. TX power, BTHome v2 version bump) adds more hand-written bytes.

## Fix plan

1. **Switch to `ble_hs_adv_fields`** for the fixed portion (flags, local name).
2. **Keep manual packing only for the BTHome service data payload** — this is user-supplied structured data that the NimBLE API does not natively understand.
3. **Factor the byte-packing helpers** (`writeLe16`, `writeLe24`) — these are useful on their own and eliminate the repeated `static_cast<std::uint8_t>` chains. See L2.
4. **Remove the "API differences" comment** with the refactor, or replace with a one-line note pointing at the pinned ESP-IDF version.
5. **Pin the ESP-IDF version explicitly** in a project-root file: a `.tool-versions` or a comment at the top of `AGENTS.md` that CI checks. (It's already documented; make it enforceable.)

## Verification

- Advertisement output identical before and after (byte-for-byte compare).
- A scanner (nRF Connect, Bluetility) sees the same device name, flags, and BTHome data.
- Source LOC for `ble_advertiser.cpp` decreases.

## Related

- L2 — byte-packing helper extraction.
