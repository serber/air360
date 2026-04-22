# L1 — Inconsistent log tag conventions

- **Severity:** Low
- **Area:** Code hygiene
- **Files:**
  - `firmware/main/src/ble_advertiser.cpp` (`constexpr char kTag[] = "air360.ble";`)
  - Other subsystems in `firmware/main/src/` use various styles — grep for `ESP_LOG`/`kTag`/`TAG` to inventory.

## What is wrong

Log tags are defined in multiple styles across the codebase:

- `constexpr char kTag[]` (BLE advertiser)
- `static const char* TAG` (common in ESP-IDF examples)
- Macro-based tags in some files
- Naming schemes mix dot-separated (`air360.ble`) with other conventions

## Why it matters

- Grep-ability suffers: no single pattern finds all tags.
- Log-line prefixes are inconsistent, making fleet log ingestion (ELK, Loki) harder to filter per-subsystem.
- Minor friction for new contributors.

## Consequences on real hardware

- None.

## Fix plan

1. **Choose one convention.** Recommended:
   ```cpp
   namespace {
   constexpr char kTag[] = "air360.<subsystem>";
   }
   ```
2. **Document in `AGENTS.md`** under a "Code style" section.
3. **Sweep the codebase** with a single refactor commit. Not urgent; can piggyback on other cleanup.
4. **Optional:** write a small lint rule (`grep` in `check_firmware_docs.py` or a new `check_style.py`) that fails on deviations.

## Verification

- `grep -rn "static const char\\* TAG" firmware/main/` returns nothing.
- All tags follow `air360.<subsystem>` pattern.

## Related

- None.
