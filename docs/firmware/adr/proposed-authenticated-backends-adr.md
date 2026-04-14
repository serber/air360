# Authenticated Backends ADR

## Status

Proposed.

## Decision Summary

Turn the current backend abstraction into a real authenticated integration surface and use that capability first for:

- openSenseMap device API key uploads
- `InfluxDB 2.x` token-based uploads

## Context

Air360 already has a backend registry and a `bearer_token` field in backend configuration, but the current firmware does not use that field.

This lines up with clear ecosystem demand:

- [PR #1062 support openSenseMap device API keys](https://github.com/opendata-stuttgart/sensors-software/pull/1062)
- [#867 Support for Influx2](https://github.com/opendata-stuttgart/sensors-software/issues/867)
- [#1034 No data upload to opensensemap since 3/16/2024](https://github.com/opendata-stuttgart/sensors-software/issues/1034)

The current Air360 backend set is:

- `Sensor.Community`
- `Air360 API`

That is a strong foundation, but it leaves the firmware unable to absorb modern token- and key-based backend requirements cleanly.

## Goals

- Make authenticated HTTP backends first-class citizens in the backend model.
- Reuse one consistent config and runtime pattern across backends.
- Add two high-value integrations without turning the firmware into a generic integration playground.

## Non-Goals

- Supporting every conceivable cloud backend.
- Building a universal scripting layer for custom HTTP uploads.
- Reworking the entire web UI into an integration marketplace.

## Architectural Decision

### 1. Make backend auth fields real

Backend records should support a normalized auth model such as:

- `auth_mode`
- `bearer_token`
- optional backend-specific key field names when the protocol requires a non-standard header

The current reserved `bearer_token` field is a good starting point, but it should become an actual validated and runtime-used field.

### 2. Add openSenseMap as a dedicated uploader

Do not squeeze openSenseMap into the `Sensor.Community` uploader.

Add a dedicated uploader with:

- its own backend type
- device API key support
- backend-specific request formatting
- backend-specific response classification

### 3. Add InfluxDB 2 as a dedicated uploader

Add an uploader that can:

- POST line protocol
- use token authorization
- configure target URL and bucket-style path fields as needed

This is a natural next backend because it covers a repeated self-hosted use case without forcing Home Assistant- or MQTT-scale design work first.

### 4. Keep TLS and auth errors explicit

The UI and `/status` should distinguish:

- missing token
- rejected token
- TLS trust failure
- endpoint misconfiguration

These are not the same operational problem and should not collapse into one generic “upload failed”.

## Affected Files

- `firmware/main/include/air360/uploads/backend_types.hpp`
- `firmware/main/include/air360/uploads/backend_config.hpp`
- `firmware/main/src/uploads/backend_registry.cpp`
- `firmware/main/src/uploads/backend_config_repository.cpp`
- new uploader sources under `firmware/main/src/uploads/adapters/`
- `firmware/main/src/web_ui.cpp`
- `firmware/main/src/status_service.cpp`
- related firmware docs

## Alternatives Considered

### Option A. Keep only unauthenticated backends

Simplest implementation, but leaves Air360 behind changing ecosystem expectations.

### Option B. Add one generic custom HTTP backend

Flexible in theory, but harder to validate, document, and support.

### Option C. Add authenticated backend support plus a few opinionated uploaders (accepted)

Best balance between capability and maintainability.

## Reference Links

- [Sensor.Community ecosystem review for Air360](../../ecosystem/sensor-community-issues-prs-review-2026-04-14.md)
- [Upload Adapters](../upload-adapters.md)
- [Configuration Reference](../configuration-reference.md)
- [Backend fault isolation ADR](proposed-backend-fault-isolation-adr.md)
- [PR #1062 support openSenseMap device API keys](https://github.com/opendata-stuttgart/sensors-software/pull/1062)
- [#867 Support for Influx2](https://github.com/opendata-stuttgart/sensors-software/issues/867)
- [#1034 No data upload to opensensemap since 3/16/2024](https://github.com/opendata-stuttgart/sensors-software/issues/1034)

## Practical Conclusion

Air360 already has the right architectural seam for richer integrations. The next step is to make authentication a real backend capability and use it to ship a small set of high-value uploaders rather than keeping the system unauthenticated-only.
