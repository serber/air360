# Local History And Diagnostics ADR

## Status

Proposed.

## Decision Summary

Expose a small local measurement history and queue-health diagnostics so Air360 remains debuggable and useful when external backends are unavailable.

The first version should add:

- a `last N samples` local history view or endpoint
- queue depth and oldest-pending age
- per-backend last success and last failure visibility
- minimal data-quality metadata where it can be collected cheaply

## Context

Air360 already stores:

- latest values per sensor for local status
- pending and inflight upload samples in memory

What it does not yet expose well is the operational state around those structures. That overlaps with recurring user pain:

- [#940 Store the last 40 measurements in JSON file](https://github.com/opendata-stuttgart/sensors-software/issues/940)
- [#937 Feature requests for data quality determination](https://github.com/opendata-stuttgart/sensors-software/issues/937)

When Wi-Fi is down overnight or a backend is broken, users want to know:

- are measurements still happening?
- how much backlog has accumulated?
- how old is the oldest unsent sample?
- is the device stuck or just waiting?

## Goals

- Give users a short local history without depending on a remote backend.
- Make queue state visible enough for field debugging.
- Surface lightweight quality metadata that improves trust in the stream.

## Non-Goals

- Building a full on-device analytics dashboard.
- Long-term local archival in this ADR.
- Persistent storage of unlimited history.

## Architectural Decision

### 1. Add a bounded local history buffer

Keep a small circular buffer of recent samples intended for local inspection.

This buffer is distinct from the upload queue:

- upload queue exists for delivery semantics
- local history exists for inspection and diagnostics

The first version can be RAM-only.

### 2. Expose history through one lightweight endpoint

Add a compact JSON endpoint that returns:

- recent samples
- queue depth
- oldest pending sample time or age
- per-backend last upload result and timestamp

This endpoint can later power both the web UI and external scraping.

### 3. Add a small UI surface, not a full dashboard

The web UI should expose:

- a recent sample list or table
- queue and backend status summary

The first version should stay diagnostic-first, not graph-first.

### 4. Add quality metadata opportunistically

Where the runtime already has the data cheaply, keep room for fields such as:

- device uptime at sample time
- configured poll interval
- PM range or min/max if a driver computes multiple subreads

The first version should avoid reworking every sensor driver just to produce diagnostics.

## Affected Files

- `firmware/main/include/air360/uploads/measurement_store.hpp`
- `firmware/main/src/uploads/measurement_store.cpp`
- `firmware/main/src/status_service.cpp`
- `firmware/main/src/web_server.cpp`
- `firmware/main/src/web_ui.cpp`
- related firmware docs

## Alternatives Considered

### Option A. Expose nothing beyond latest values

Simple, but leaves users blind during the exact failure modes they most want to debug.

### Option B. Full graphing UI on-device

Too much scope for the first version.

### Option C. Small history endpoint plus queue diagnostics (accepted)

High operational value with contained implementation cost.

## Reference Links

- [Sensor.Community ecosystem review for Air360](../../ecosystem/sensor-community-issues-prs-review-2026-04-14.md)
- [Measurement Pipeline](../measurement-pipeline.md)
- [Web UI](../web-ui.md)
- [Implemented overview health status ADR](implemented-overview-health-status-adr.md)
- [Proposed measurement queue persistence ADR](proposed-measurement-queue-persistence-adr.md)
- [#940 Store the last 40 measurements in JSON file](https://github.com/opendata-stuttgart/sensors-software/issues/940)
- [#937 Feature requests for data quality determination](https://github.com/opendata-stuttgart/sensors-software/issues/937)

## Practical Conclusion

Air360 should expose enough recent history and queue state that users can tell whether the device is sampling, buffering, and eventually recovering. That closes a real field-operations gap without needing a full local analytics product.
