# SIM7600E Mobile Uplink ADR

## Status

Proposed.

This document is a planning and architecture decision note for future firmware work. It is not a description of already implemented behavior.

## Decision Summary

Air360 should add support for `SIM7600E` as an optional cellular uplink path for deployments where Wi-Fi is unavailable or unreliable.

The preferred implementation shape is:

- use `SIM7600E` as an IP uplink modem, not as a module that performs HTTP requests itself
- integrate it through `PPP over UART` using ESP-IDF modem support rather than custom AT-driven HTTP flows
- keep the existing upload pipeline backend-agnostic so `Sensor.Community` and `Air360 API` continue to work without backend-specific cellular code
- add explicit persisted cellular configuration in NVS
- expose cellular provisioning through the local firmware UI
- treat Wi-Fi as the preferred uplink when both Wi-Fi and cellular are configured, with cellular as fallback

## Context

The current firmware assumes that the device uploads over Wi-Fi station mode and falls back to setup AP mode when station join is unavailable.

That is fine for indoor or home-network deployments, but it blocks a practical class of installations:

- remote environmental monitoring points
- outdoor installations
- mobile or temporary installations
- installations where no local Wi-Fi exists

`SIM7600E` is a pragmatic target for the first cellular path because:

- it is widely available
- it supports LTE data
- it can expose a conventional IP uplink over UART/PPP
- it is a common choice in embedded telemetry projects

The main product goal is not “support a modem” in the abstract. The real goal is:

- allow the current firmware to collect sensor data and upload it from places where Wi-Fi is not available

## Goals

- support sensor-data upload over a mobile network
- reuse as much of the existing firmware runtime as possible
- preserve the existing backend model so both `Sensor.Community` and `Air360 API` uploads continue to work
- keep provisioning understandable for a user who may initially reach the device only through setup AP mode
- expose enough runtime visibility to diagnose modem, network, and signal problems from the local web UI and `/status`
- keep queueing semantics unchanged so measurements continue to accumulate and drain through the existing upload pipeline

## Non-Goals

- SMS features
- GNSS features of the module
- voice or call features
- arbitrary AT-console access in the normal UI
- replacing Wi-Fi onboarding with a mobile-only captive provisioning flow
- implementing every SIM7600 variant at once
- solving all TLS and certificate issues as part of the first cellular milestone

## Architectural Decision

### 1. Use PPP over UART, not module-side HTTP

The firmware should treat `SIM7600E` as a network bearer that provides IP connectivity.

Chosen approach:

- `SIM7600E` on a dedicated UART
- ESP-IDF modem integration
- PPP session established by firmware
- all HTTP requests continue to run through the existing ESP-IDF-side transport stack

This means the modem is responsible for:

- SIM access
- radio attach
- PDP context activation
- PPP data path

The firmware remains responsible for:

- backend request construction
- HTTP execution
- SNTP
- queue management
- retry logic
- user-visible status

### 2. Keep backend adapters unchanged in principle

The current backends should not gain “cellular-specific” code paths.

Instead:

- `UploadManager`
- `UploadTransport`
- backend adapters

should continue to see a generic IP uplink.

If cellular is active, uploads should look the same to the rest of the pipeline as they do over Wi-Fi.

### 3. Add a dedicated cellular runtime component

Do not bury modem logic directly inside the existing Wi-Fi setup code.

Preferred runtime shape:

- keep the current Wi-Fi and setup-AP responsibilities explicit
- add a dedicated `CellularManager` or equivalent modem-focused service
- introduce a small generic connectivity/uplink status layer consumed by `UploadManager` and `StatusService`

This avoids turning one class into a large mixed Wi-Fi/AP/modem blob.

### 4. Preserve setup AP as the provisioning and recovery path

The current local setup flow remains valuable even for cellular deployments.

For the first version:

- the device should still use setup AP when it has no usable uplink configuration
- the user should be able to enter cellular settings through the local web UI
- setup AP remains the recovery path when Wi-Fi and cellular both fail

Running setup AP permanently alongside a healthy modem uplink is not required for the first milestone.

## Alternatives Considered

### Option A. Use raw AT commands and let the module perform HTTP requests

Rejected.

Why:

- duplicates HTTP logic already implemented in firmware
- pushes backend-specific behavior into modem command sequences
- makes `Sensor.Community` and `Air360 API` handling more fragile
- complicates TLS, redirects, timeouts, and diagnostics
- makes queueing and retry behavior harder to keep consistent

### Option B. Use PPP over UART with ESP-IDF modem support

Accepted direction.

Why:

- keeps the modem as a bearer, not as an application-layer transport
- lets the existing upload stack continue to operate mostly unchanged
- lets SNTP work normally over the active uplink
- is a cleaner long-term basis for additional cellular modules later

### Option C. Use USB modem mode

Deferred.

Why:

- `SIM7600E` may support richer USB modes, but that increases hardware and firmware complexity
- UART PPP is a better first milestone
- the current firmware and board assumptions are already closer to UART integration

## Expected Firmware Changes

## Connectivity Model

The firmware should move toward an explicit notion of “active uplink”.

Preferred uplink policy for the first implementation:

- if Wi-Fi station is configured and connected, use Wi-Fi
- if Wi-Fi is unavailable and cellular is enabled and healthy, use cellular
- if neither is available, use setup AP for provisioning/recovery

This keeps metered cellular traffic from becoming the default in installations where Wi-Fi exists.

## New Persisted Configuration

Add a dedicated persisted `CellularConfig` model in NVS rather than overloading `DeviceConfig`.

Suggested fields:

- `enabled`
- `apn`
- `username`
- `password`
- `sim_pin`
- `uart_port`
- `uart_rx_gpio`
- `uart_tx_gpio`
- `uart_baud_rate`
- `pwrkey_gpio` optional
- `reset_gpio` optional
- `power_enable_gpio` optional
- `network_preference` such as `auto` / `lte_preferred`
- `allow_roaming`

Notes:

- `sim_pin` is optional because some SIMs do not require it
- modem hardware-control pins should stay optional because not every board routes them
- do not persist device-identification data such as IMEI as config; those should remain runtime-read values

## UI Changes

For the first version, cellular configuration should be exposed on the existing `Device` page.

Why:

- setup AP mode currently exposes only `Device`
- a user provisioning a cellular-only installation must still be able to enter APN and SIM settings during first setup
- it keeps first-time onboarding possible without adding extra AP-mode navigation

Suggested sections on `Device`:

- `Device name`
- `Wi-Fi station`
- `Mobile uplink`

Suggested mobile uplink fields:

- enable/disable
- APN
- username/password if required by the carrier
- SIM PIN if required
- network mode preference

Advanced UART and control-pin fields can remain hidden or developer-oriented unless the hardware actually exposes them as a user-facing choice.

## Runtime Status and Diagnostics

Expose cellular runtime state both in the web UI and `/status`.

Suggested fields:

- cellular enabled
- modem detected
- SIM ready
- network registered
- PDP/PPP connected
- operator name
- radio access technology such as LTE
- RSSI / signal quality
- current active uplink: `wifi`, `cellular`, or `none`
- last cellular error

Do not expose highly sensitive identifiers more than necessary. In particular:

- avoid prominently exposing full SIM/ICCID details in the general UI
- expose IMEI only if truly needed for debugging, and consider partial masking in the normal UI

## Upload Pipeline Impact

The upload pipeline should remain structurally the same:

- `SensorManager` forwards successful readings into `MeasurementStore`
- `UploadManager` waits for a valid uplink and valid time
- samples remain queued while the modem is offline
- queue drains when connectivity returns

The core rule is:

- cellular changes the bearer, not the queueing contract

This is important because remote cellular links will be less stable than home Wi-Fi and the existing queue/retry semantics are already a useful fit.

## Time Synchronization

The primary time source should remain SNTP over the active IP uplink.

For the first version:

- do not treat modem-reported time as the primary trusted source
- use SNTP over PPP once the uplink is active
- keep the existing “uploads require valid Unix time” rule

Possible later enhancement:

- use modem-provided time only as a bootstrap hint if SNTP is still unavailable

## Power and Hardware Constraints

This integration is not just a firmware task. `SIM7600E` has real hardware implications.

Important assumptions:

- the modem needs a stable external power path with high current headroom
- antenna design and placement matter
- UART level, reset, and power-control wiring must be explicit at board level

Firmware should support optional modem-control GPIOs, but firmware alone cannot compensate for underpowered hardware.

The ADR therefore assumes:

- power delivery is solved in hardware
- the firmware controls the modem, but does not create its power budget

## Failure Handling

The firmware should handle modem and network instability explicitly.

Required behaviors:

- backoff between reconnect attempts
- modem reset after repeated attach/PPP failures
- clear state transitions in UI and `/status`
- continued measurement queueing while offline
- no reboot loops just because cellular attach fails

The modem path should degrade into an “offline but still collecting” state rather than repeatedly destabilizing the whole runtime.

## Security and Privacy Notes

- store APN credentials and SIM PIN in NVS similarly to other device credentials
- do not expose secret fields back into the UI unnecessarily
- keep backend credentials independent from modem credentials
- avoid adding ad-hoc modem debug routes that can issue arbitrary AT commands without access control

This ADR does not change the current broader limitation that `Air360 API` currently defaults to plain HTTP in the firmware.

## Implementation Phases

### Phase 1. Bring-up

- add `CellularConfig`
- integrate `SIM7600E` over UART
- establish PPP
- expose basic status

Success criteria:

- device gets an IP uplink over the modem
- SNTP works over cellular

### Phase 2. Upload Integration

- switch `UploadManager` to generic active-uplink status
- confirm `Sensor.Community` and `Air360 API` uploads work over cellular without backend-specific changes

Success criteria:

- a device with no Wi-Fi can still upload data through the modem

### Phase 3. UI and Diagnostics

- add `Mobile uplink` section to `Device`
- add signal/operator/runtime status to `Overview` and `/status`

Success criteria:

- user can provision the modem without serial access
- modem problems are diagnosable from the UI

### Phase 4. Robustness

- backoff tuning
- modem reset policy
- carrier- and roaming-related edge cases
- field testing in weak-signal conditions

## Acceptance Criteria

The work should be considered successful when all of the following are true:

- the device can boot with no Wi-Fi credentials and still upload over `SIM7600E`
- sensor collection continues while cellular is temporarily unavailable
- queued samples drain after connectivity returns
- the firmware UI clearly shows whether cellular is provisioned and connected
- `Sensor.Community` and `Air360 API` both work over the cellular uplink without separate code paths inside the backend adapters
- setup AP remains usable as a provisioning/recovery mechanism

## Open Questions

- which UART and modem-control pins should become board defaults
- whether the first hardware target should support only one modem wiring layout
- whether AP mode should ever run concurrently with a healthy cellular uplink
- whether Wi-Fi fallback policy should be fixed or user-configurable
- whether modem-derived time should ever be used as a fallback before SNTP
- what minimum operator diagnostics should be shown in the normal UI

## Consequences

Positive:

- opens non-Wi-Fi deployment scenarios
- preserves backend compatibility
- reuses the current upload pipeline and queue model
- provides a path for future cellular modules beyond `SIM7600E`

Negative:

- increases runtime complexity
- adds power and hardware dependencies outside firmware
- requires new provisioning UX and new diagnostics
- introduces a new class of intermittent-network failure modes that must be handled carefully
