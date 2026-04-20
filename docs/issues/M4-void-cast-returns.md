# M4 — Ignored return values via `static_cast<void>`

- **Severity:** Medium
- **Area:** Code quality / observability
- **Files:**
  - `firmware/main/src/config_repository.cpp` (`loaded_from_storage`, `wrote_defaults`)
  - Various: grep for `static_cast<void>(` in `main/src/`

## What is wrong

Several call sites wrap a meaningful return value in `static_cast<void>(...)` to suppress unused-result warnings. The information is discarded without a comment explaining why.

Examples flagged in review:
- `static_cast<void>(loaded_from_storage)` — whether the config was loaded from NVS or regenerated from defaults is significant for telemetry.
- `static_cast<void>(wrote_defaults)` — whether a default write succeeded matters: a silent failure means the next boot repeats the work.

## Why it matters

- Silent discarding hides recoverable or reportable conditions.
- Future readers cannot tell whether the discard was deliberate or an oversight.
- `[[nodiscard]]` annotations (or their absence) are not reliable signals without the review discipline.

## Consequences on real hardware

- No direct runtime failure, but observability suffers: operators cannot tell from logs whether a device booted with preserved config or with defaults.

## Fix plan

1. **Grep for `static_cast<void>(`** in `firmware/main/`. Audit each site.
2. **For each site, decide:**
   - Is the value genuinely unused? → leave the cast and add a one-line comment explaining why.
   - Should it inform an error log, a status field, or a counter? → wire it up.
3. **Specifically for config repositories:**
   - Surface a `ConfigLoadSource` enum on the status endpoint: `{kNvsPrimary, kNvsBackup, kDefaults}`.
   - Increment a counter on each path.
4. **Add `[[nodiscard]]`** to return-value-mattering functions (`load`, `save`, `commit`, `start`, `stop`) where it's not already present.
5. **Enable `-Wunused-result`** as a build warning (or `-Wall` with a `[[nodiscard]]` audit).
6. **Document** the convention in `AGENTS.md`: "Discarding a return value requires either `[[nodiscard]]` absent, or an explicit comment and `static_cast<void>()`."

## Verification

- Status JSON includes `config.load_source` for each repository.
- Grep shows no unexplained `static_cast<void>(` casts.

## Related

- H4 / H5 — migration and backup events surface through this mechanism.
