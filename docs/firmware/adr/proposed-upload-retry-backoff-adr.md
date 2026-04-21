# Upload Retry Exponential Backoff ADR

## Status

Proposed.

## Decision Summary

Replace the fixed `upload_interval_ms` retry delay on upload failures with capped exponential backoff per backend.

## Context

The upload task currently retries failed uploads after the same fixed interval regardless of failure count:

- Normal cycle: 145 s
- Failed upload: 145 s (same as success)

If a backend is unreachable for hours (server down, DNS failure, bad API key), the device makes a full HTTP request attempt every 145 seconds indefinitely. Each attempt involves a TCP connection, TLS handshake, and a timeout wait — consuming CPU cycles and Wi-Fi radio time for zero benefit.

There is also no distinction between:
- Transient failures (`503 Service Unavailable`, network timeout) — should retry soon
- Permanent failures (invalid API key `403`, bad endpoint URL) — should back off aggressively

This ADR also sits inside a broader reliability problem identified in the ecosystem review:

- one failing backend can still dominate device behavior
- the current delivery model remains effectively all-or-nothing for an upload window
- users need clearer runtime distinction between transport errors, auth errors, and remote HTTP failures

The larger fault-isolation work is captured separately in [proposed-backend-fault-isolation-adr.md](proposed-backend-fault-isolation-adr.md). This ADR keeps the narrower decision about retry timing.

## Goals

- Reduce wasted upload attempts when a backend is persistently unavailable.
- Converge toward a longer retry interval for repeated failures.
- Reset to normal cadence immediately on the first success.
- Keep per-backend retry state independent (one broken backend should not slow uploads to a healthy one).

## Non-Goals

- Distinguishing transient from permanent failures in the first version (all failures back off equally).
- User-configurable backoff parameters.
- Changing successful-upload cadence.

## Architectural Decision

### Backoff formula

```
next_delay = min(base_interval * 2^consecutive_failures, max_backoff)
```

| Parameter | Value |
|-----------|-------|
| `base_interval` | `upload_interval_ms` (default 145 s) |
| `max_backoff` | 1 800 s (30 min) |
| Max doublings before cap | ~3–4 |

Example progression: 145 s → 290 s → 580 s → 1 160 s → 1 800 s (capped).

### Per-backend tracking

Add `uint32_t consecutive_failures` to `ManagedBackend` (already exists in `UploadManager`). Increment on every failure, reset to `0` on `UploadResult::kSuccess`.

### Next cycle scheduling

The upload task already computes `next_cycle_time_ms` after each cycle. Change the failure branch to use the backoff formula instead of the fixed interval. The minimum next delay across all active backends determines when the task wakes next (currently it always uses one global timer — this can stay, with the value being the minimum of all per-backend next times, or simply use the backoff of the worst-performing backend).

For simplicity in the first version: use a single task-level next_cycle computed as `min(backoff_for_each_backend)` so the task wakes when the fastest-recovering backend is due.

### No change to NoNetwork / NoTime paths

When the upload is skipped due to no network or no valid time, the consecutive_failures counter is not incremented — these are not backend failures. The 1 s re-check interval for these conditions stays unchanged.

## Affected Files

- `firmware/main/src/uploads/upload_manager.cpp` — add `consecutive_failures` to `ManagedBackend`, update cycle scheduling in `taskMain()`
- `firmware/main/include/air360/uploads/upload_manager.hpp` — update `ManagedBackend` struct if it is defined in the header

## Alternatives Considered

### Option A. Keep fixed interval

Simple. Wastes attempts when backends are persistently down.

### Option B. Exponential backoff per backend (accepted)

Low implementation complexity (add one counter, change one formula). Significant reduction in wasted attempts during outages.

### Option C. Jitter + exponential backoff

Adds randomness to prevent thundering herd if many devices share the same backend. Useful at scale but unnecessary for single-device deployments. Defer to a follow-up.

## Reference Links

- [Sensor.Community ecosystem review for Air360](../../ecosystem/sensor-community-issues-prs-review-2026-04-14.md)
- [Measurement Pipeline](../measurement-pipeline.md)
- [Upload Adapters](../upload-adapters.md)
- [Proposed backend fault isolation ADR](proposed-backend-fault-isolation-adr.md)
- [#912 Crash and reboot when API reply is too big](https://github.com/opendata-stuttgart/sensors-software/issues/912)

## Practical Conclusion

Add `consecutive_failures` per `ManagedBackend`. Apply the backoff formula in the cycle scheduling path. Reset on success. The change is confined to `upload_manager.cpp` and has no effect on healthy backends.
