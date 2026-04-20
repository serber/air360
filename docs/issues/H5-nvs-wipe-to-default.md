# H5 — Wipe-and-default on NVS corruption

- **Severity:** High
- **Area:** Persistence / resilience
- **Files:**
  - `firmware/main/src/config_repository.cpp`
  - `firmware/main/src/cellular_config_repository.cpp`
  - `firmware/main/src/uploads/backend_config_repository.cpp`
  - `firmware/main/src/sensors/sensor_config_repository.cpp`

## What is wrong

When any repository's load fails validation (bad magic / schema / size / null-termination), the code wipes the blob and writes defaults. No backup. No retry. No degraded-mode signal.

## Why it matters

- A single transient read error or a bit-flip in flash destroys the device's Wi-Fi credentials, cellular APN, and backend tokens.
- Owner must re-provision from scratch — on a remote or embedded-in-wall device that can mean a service visit.
- There is no telemetry to distinguish "factory boot" from "I just lost your config."

## Consequences on real hardware

- Rare but catastrophic. Most devices will never hit this; the few that do end up offline and look like a hardware failure.
- Debugging is impossible after the fact because the only surviving log line says "blob invalid, defaults written."

## Fix plan

1. **Dual-blob storage per repository.** Keep a `*_primary` and `*_backup` key per config.
2. **Write flow:**
   - Write to `*_primary`.
   - On success, copy to `*_backup`.
   - If primary write fails partway (power loss), the old `*_primary` may be invalid, but `*_backup` still holds the last-known-good.
3. **Read flow:**
   - Load `*_primary`.
   - If valid, done.
   - If invalid, load `*_backup`. If valid, promote it to `*_primary`. Log "degraded config recovered from backup."
   - If both invalid, *then* write defaults. Log "config lost, defaults applied."
4. **Counter for degradation events.** Persist a small counter per repository: `{primary_failures, backup_failures, default_resets}`. Expose on the status JSON so fleet dashboards can alarm on devices that regularly degrade.
5. **Throttle the reset.** If primary is invalid but backup is also invalid, require N consecutive failures on reboot before wiping to defaults (N=3). Between attempts, leave the NVS untouched and run with in-RAM defaults — this lets a transient read error recover on the next boot.
6. **Size cost.** Each config is ~200–600 B; doubling them all is <4 KB of NVS. Acceptable.
7. **Atomic write primitive.** Consider a helper:
   ```cpp
   esp_err_t writeBlobWithBackup(nvs_handle_t h,
                                 const char* primary_key,
                                 const char* backup_key,
                                 const void* data, size_t size);
   ```
   Ensures the primary/backup invariant is centralized.

## Verification

- Host test with fake NVS: corrupt primary, load returns backup; corrupt both, returns defaults and bumps counter.
- Device test: yank power during a `commit()`; on reboot, backup survives.
- Status endpoint reports `{config_recovered_from_backup: 1}` after the induced failure.

## Related

- H4 — schema migration must run on either primary or backup once loaded.
- C2 — similar WAL-style discipline for the measurement queue.
