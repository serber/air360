# Backend Fault Isolation ADR

## Status

Proposed.

## Decision Summary

Isolate backend failures so one broken upload destination cannot stall delivery to healthy backends.

The first version should add:

- per-backend success or failure accounting
- acknowledgement of samples for backends that succeeded
- retry only for backends that failed
- safe handling of oversized or malformed HTTP responses

## Context

The current `MeasurementStore` and upload pipeline provide one shared inflight window for all enabled backends. As documented in [`../measurement-pipeline.md`](../measurement-pipeline.md):

- if all backends succeed, inflight samples are acknowledged
- if any backend fails, inflight samples are restored to `pending_`

That behavior is simple, but it turns one backend outage into a global stall. It overlaps directly with recurring ecosystem pain:

- [#912 Crash and reboot when API reply is too big](https://github.com/opendata-stuttgart/sensors-software/issues/912)
- [#1034 No data upload to opensensemap since 3/16/2024](https://github.com/opendata-stuttgart/sensors-software/issues/1034)

Air360 already has multiple backends and a backend registry. That makes fault isolation more valuable than in single-destination firmware.

## Goals

- Prevent one failing backend from blocking healthy ones.
- Keep at-least-once semantics per backend.
- Bound response handling so malformed servers cannot destabilize the device.
- Improve runtime classification of backend failures.

## Non-Goals

- Full exactly-once delivery guarantees.
- Redesigning the entire measurement store around an external database.
- Solving authentication-specific backend changes in this ADR.

## Architectural Decision

### 1. Move from queue-level acknowledgement to backend-aware acknowledgement

The shared inflight window should remain, but completion should become backend-aware.

Accepted direction:

- each backend processes the same batch independently
- success or failure is recorded per backend
- samples are dropped only when all enabled backends have either acknowledged them or the sample has no remaining interested backend

This can be implemented either with per-sample backend bitsets or a lightweight delivery mask attached to the inflight batch.

### 2. Bound transport response handling

`UploadTransport` should enforce explicit limits for:

- maximum response body captured
- maximum headers processed
- classification of truncated or oversized payloads

The firmware should never depend on reading an arbitrarily large HTML error page into memory.

### 3. Distinguish backend failure classes in runtime status

The backend runtime should report at least:

- DNS failure
- connect timeout
- TLS or certificate failure
- HTTP error
- malformed response
- authentication failure

This makes support and field triage materially easier.

### 4. Keep retry timing separate

Retry timing remains governed by [proposed-upload-retry-backoff-adr.md](proposed-upload-retry-backoff-adr.md).

This ADR covers the question “what should happen when one backend fails while another succeeds?”

## Affected Files

- `firmware/main/include/air360/uploads/measurement_store.hpp`
- `firmware/main/src/uploads/measurement_store.cpp`
- `firmware/main/include/air360/uploads/upload_manager.hpp`
- `firmware/main/src/uploads/upload_manager.cpp`
- `firmware/main/src/uploads/upload_transport.cpp`
- `firmware/main/src/status_service.cpp`
- related firmware docs

## Alternatives Considered

### Option A. Keep all-or-nothing backend behavior

Smallest code surface, but a poor fit once multiple independent backends exist.

### Option B. Full per-backend queues

Very flexible, but a larger redesign of queue storage, retention, and diagnostics.

### Option C. Shared inflight window plus per-backend delivery state (accepted)

Preserves most of the current architecture while solving the user-visible failure mode.

## Reference Links

- [Sensor.Community ecosystem review for Air360](../../ecosystem/sensor-community-issues-prs-review-2026-04-14.md)
- [Measurement Pipeline](../measurement-pipeline.md)
- [Upload Adapters](../upload-adapters.md)
- [Implemented measurement runtime separation ADR](implemented-measurement-runtime-separation-adr.md)
- [Proposed upload retry exponential backoff ADR](proposed-upload-retry-backoff-adr.md)
- [#912 Crash and reboot when API reply is too big](https://github.com/opendata-stuttgart/sensors-software/issues/912)
- [#1034 No data upload to opensensemap since 3/16/2024](https://github.com/opendata-stuttgart/sensors-software/issues/1034)

## Practical Conclusion

Air360 should stop treating a multi-backend upload cycle as one indivisible success or failure. Backend-aware acknowledgement and bounded response handling will close a real class of field failures without discarding the current upload architecture.
