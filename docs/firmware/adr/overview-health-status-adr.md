# Overview Health Status ADR

## Status

Proposed.

This document records a planned firmware architecture and UX decision. It does not describe already implemented behavior.

## Decision Summary

Air360 should add a compact `Health` summary to the `Overview` page that answers the operator's first question:

`Is this device healthy right now?`

The health summary should aggregate the current state of:

- time synchronization
- sensor reporting
- uplink availability
- backend upload health

The feature should provide:

- one top-level health state
- a small set of explicit sub-checks
- a short human-readable explanation when the device is not fully healthy

The feature should not rely on an opaque numeric score.

## Context

Today the `Overview` page exposes the raw building blocks of device state:

- device identity
- sensor cards
- backend cards
- uptime
- boot count
- per-sensor queued samples

That is useful for debugging, but it still requires the user to mentally combine multiple signals.

For normal device operation, especially in beta deployments and field installations, operators need a faster answer:

- is time valid?
- are enabled sensors producing data?
- is network uplink usable?
- are enabled backends actually succeeding?

Without an aggregated health view, the user must infer these conclusions manually from several sections.

## Goals

- provide a fast at-a-glance runtime health signal
- reduce the need to manually inspect all overview sections
- make degraded states explicit and actionable
- reuse existing runtime data where possible
- keep the first implementation simple and deterministic

## Non-Goals

- building a weighted health-scoring system
- hiding detailed sensor and backend state behind a single badge
- inventing synthetic metrics that are hard to explain
- introducing background remediation logic as part of the health UI itself

## Architectural Decision

### 1. Add a dedicated `Health` block near the top of `Overview`

The `Overview` page should contain a compact health section above or near the existing runtime stat cards.

That section should show:

- a top-level status label
- a short description of why the current status was chosen
- a small fixed set of sub-check pills or rows

The top-level status should be one of:

- `Healthy`
- `Degraded`
- `Offline`
- `Setup Required`

### 2. Use explicit checks instead of a hidden score

The health summary should be derived from explicit checks:

- `Time synced`
- `Sensors reporting`
- `Uplink available`
- `Backends healthy`

Each check should have a clear pass/fail or pass/warn result that can be explained directly in UI and `/status`.

The overall health state should be determined from these explicit checks, not from a hidden weighted score.

### 3. Health must be based on enabled runtime entities only

The health summary should only evaluate configured entities that matter operationally.

That means:

- disabled sensors must not count as unhealthy
- disabled backends must not count as unhealthy
- setup-mode conditions must not be treated the same as station-mode failure

This avoids false alarms when the user intentionally disables parts of the device.

### 4. Health state derivation should be simple and deterministic

The first version should use rules like:

#### `Setup Required`

Use when:

- the device is in setup AP mode
- or station credentials are absent / onboarding is incomplete

Meaning:

- device is not yet in normal station-mode operation

#### `Offline`

Use when:

- station-mode operation is expected
- but uplink is unavailable
- or valid time is still missing for normal backend operation

Meaning:

- the device is not fully connected for normal upload flow

#### `Degraded`

Use when:

- uplink and time are available
- but one or more enabled sensors are not reporting
- or one or more enabled backends are failing

Meaning:

- the device is partially operational, but something needs attention

#### `Healthy`

Use when:

- time is valid
- uplink is available for station-mode operation
- all enabled sensors are reporting recently enough
- all enabled backends are healthy enough by the current backend policy

Meaning:

- the device is operating normally

### 5. "Recently enough" must use explicit freshness thresholds

Sensor health should not merely mean "a value exists".

For enabled sensors, the firmware should evaluate freshness using a deterministic threshold derived from the configured poll interval.

A reasonable initial rule is:

- a sensor is considered healthy if its last measurement age is less than a small multiple of its configured `poll interval`

This keeps the logic understandable and aligned with each sensor's cadence.

### 6. Backend health should be operational, not perfect-history-based

Backend health should not require a perfect upload history.

The first version should evaluate backend health based on currently enabled backends and a simple operational rule set, such as:

- recent success exists
- or no current hard error is present
- and the backend is not clearly blocked by missing prerequisites

The purpose is to answer whether uploads are flowing, not to perform long-term SLA analysis.

### 7. The same health data should be exposed in `/status`

The health summary should not be UI-only.

The firmware should expose the same derived health information in `/status`, including:

- top-level health state
- per-check state
- short summary message

This allows:

- remote inspection
- future alerting or monitoring
- easier debugging of field units

## Proposed Data Model

The initial health payload should be intentionally small.

Suggested fields:

- `health_status`
- `health_summary`
- `health_checks`

Suggested `health_checks` entries:

- `time_synced`
- `sensors_reporting`
- `uplink_available`
- `backends_healthy`

Each check may expose:

- `state`
- `summary`

The exact JSON shape can remain implementation-defined, but it should stay small and easy to inspect.

## UI Guidance

The `Health` block should be compact and calm.

Recommended behavior:

- one primary status pill or label
- one short line of explanation
- a small row/list of checks

If the state is not `Healthy`, the block should explain the first useful reason, for example:

- `Waiting for valid time`
- `1 sensor not reporting`
- `1 backend failing`
- `Wi-Fi uplink unavailable`

The block should help the user decide where to look next, not replace the detailed sections below.

## Alternatives Considered

### Option A. Keep only raw sensor and backend sections

Rejected as the preferred UX direction.

Why:

- forces users to synthesize health mentally
- slows diagnosis
- increases friction in field deployments

### Option B. Add one overall health state with supporting checks

Accepted direction.

Why:

- fast to understand
- still explainable
- can be built on existing runtime data
- does not hide the detailed view

### Option C. Use a numeric health score

Rejected for the first version.

Why:

- looks precise without being truly interpretable
- harder to explain and debug
- encourages arbitrary weighting rules

## Consequences

Positive:

- much faster operator feedback
- easier support and field diagnostics
- clearer distinction between healthy, degraded, and onboarding states
- creates a useful summary layer above the detailed overview cards

Trade-offs:

- requires explicit freshness and backend-health rules
- some edge cases will still require looking at detailed sections
- health semantics must remain stable and understandable as the firmware evolves

## Implementation Notes

The feature should preferably be implemented in `StatusService`, because that layer already aggregates runtime data for both UI and `/status`.

Likely inputs:

- network mode and uplink state
- current valid time state
- enabled sensor runtime records
- last measurement timestamps
- enabled backend status

The first version should avoid adding new persistent config and should derive health entirely from existing runtime state.
