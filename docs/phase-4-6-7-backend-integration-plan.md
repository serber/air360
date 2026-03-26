# Phase 4 + 6 + 7 Backend Integration Plan

## Scope

This document consolidates the backend-facing work that spans:

- Phase 4: first legacy-compatible upload path
- Phase 6: multi-backend delivery
- Phase 7: Air360 native backend integration and future non-Wi-Fi uplink preparation

The goal is to turn those phases into one coherent implementation plan instead of treating them as disconnected milestones.

This is a planning document. The current `firmware/` source tree does not implement an upload pipeline yet. At the moment, the firmware only:

- reads and polls sensors locally
- stores device and sensor configuration in NVS
- exposes local state through `/`, `/status`, `/config`, and `/sensors`

## Why These Phases Should Be Planned Together

If Sensor.Community support, multi-backend support, and Air360 native upload are implemented as separate ad hoc features, the firmware will almost certainly end up with:

- backend-specific logic leaking into the core
- UI code hardcoded to a fixed set of APIs
- retry and status logic duplicated between adapters
- no clean path for future connectivity providers such as modem or relay delivery

The correct architecture is:

- one normalized internal measurement/output model
- one backend registry
- one upload manager
- per-backend adapters behind a common contract
- separate backend configuration and UI
- delivery status tracked per backend, not globally

## Design Goals

### Functional goals

- Support legacy Sensor.Community upload exactly where compatibility matters.
- Support the new Air360 API as the long-term native path.
- Allow both APIs to be enabled at the same time.
- Make it easy to add a third backend later without changing unrelated modules.
- Expose backend configuration and status in the local UI.

### Architectural goals

- Keep the internal measurement model backend-agnostic.
- Keep payload serialization inside backend adapters.
- Keep retries, scheduling, and status in shared upload infrastructure.
- Keep backend config separate from device config and sensor config.
- Prepare the upload stack for future non-Wi-Fi uplinks without implementing them yet.

### Non-goals for the first implementation

- Full modem support
- LoRa mesh transport
- Generic webhook builder for arbitrary third-party APIs
- Binary protocol optimization
- Full historical outbox for long offline durations from day one

## Current Baseline In Firmware

Confirmed current implementation boundary:

- `SensorManager` already produces live sensor runtime state and latest measurements.
- `StatusService` already renders measurements generically.
- `WebServer` already has separate pages for device config and sensor config.
- There is no upload task, no backend config repository, no backend registry, and no HTTP client upload layer yet.

This is a good baseline because the upload subsystem can now consume normalized sensor output without being coupled to individual drivers.

## Recommended Architecture

## 1. Internal Canonical Model

The upload subsystem should not serialize directly from UI forms or sensor-driver-specific structures.

It should work from a normalized batch model.

Recommended minimum models:

- `MeasurementPoint`
  - sensor instance id
  - sensor type
  - measurement kind
  - numeric value
  - unit metadata or implied unit
  - sample timestamp
- `MeasurementBatch`
  - batch id
  - creation time
  - device identity snapshot
  - network snapshot
  - list of normalized measurement points
- `BackendDeliveryState`
  - backend key
  - enabled flag
  - last attempt time
  - last success time
  - last result
  - retry count
  - next retry time

Important rule:

- the canonical model must not contain legacy names like `sensordatavalues`, `X-PIN`, `P1`, `P2`, or Air360 endpoint-specific field names

Those belong only inside adapters.

## 2. Backend Abstraction

Define one shared uploader interface:

- `IBackendUploader`
  - `backendKey()`
  - `validateConfig(...)`
  - `buildRequests(...)`
  - `classifyResponse(...)`

Why `buildRequests(...)` and not only `upload(...)`:

- Sensor.Community is not one request per batch; it is one request per sensor family
- Air360 native API is naturally one batch request
- future backends may use one-to-many or one-to-one mapping

That means the shared pipeline should work with a backend-specific list of `UploadRequestSpec` objects generated from one normalized batch.

## 3. Backend Registry

Use a registry-driven backend model, similar to the current sensor registry.

Each backend descriptor should contain:

- `backend_key`
- display name
- whether the backend is implemented
- whether it is legacy or native
- whether it supports dual-upload mode
- required config fields
- validation function
- uploader factory

The UI must build the backend list from this registry, not from hardcoded `if backend == ...` branches.

This is the key requirement for easy addition of a new API later.

## 4. Delivery Core

Create one central upload orchestrator:

- `UploadManager`

Responsibilities:

- load enabled backend configs
- receive or build measurement batches on schedule
- ask the registry for enabled backends
- ask each adapter to build request specs
- execute uploads through one transport layer
- store per-backend delivery status
- schedule retries independently
- expose a snapshot for `/status` and the UI

Recommended task model:

- one dedicated background FreeRTOS task for uploads
- sequential request execution in the first implementation

Do not create one task per backend.

That would add complexity before the retry and queue model is proven.

## 5. Transport Layer

Backend adapters should not call `esp_http_client` directly in ad hoc ways.

Add a shared transport layer:

- `UploadTransport`
- request method, URL, headers, body, timeout
- normalized response object with:
  - transport result
  - HTTP status
  - response size
  - response body snippet if needed for diagnostics

This makes retries and response classification consistent across all backends.

## 6. Persistence Model

Backend configuration should not be mixed into `DeviceConfig`.

Add:

- `BackendConfigRepository`
- NVS blob key such as `backend_cfg`
- explicit schema version

Recommended top-level persisted model:

- `DeviceConfig`
  - network and device basics
- `SensorConfigList`
  - sensor inventory
- `BackendConfigList`
  - enabled backends and their settings

This keeps onboarding, sensors, and uploads separate.

## 7. UI Model

Do not overload `/config`.

Add a separate `/backends` page:

- `GET /backends`
  - render enabled backends, current configuration, and last status
- `POST /backends`
  - add or update a backend config
- `POST /backends/<id>/delete`
  - remove a backend

This mirrors the sensor UI model and avoids mixing Wi-Fi setup with upload configuration.

The UI should support:

- enable or disable a backend
- choose which APIs are active
- edit backend-specific settings
- inspect last success/failure per backend
- inspect whether dual-upload mode is active

## Backend Types To Support First

## 1. Legacy Sensor.Community Adapter

This adapter must preserve compatibility-critical behavior:

- endpoint host/path compatible with the documented legacy contract
- one HTTP `POST` per sensor family
- `X-Sensor`
- `X-MAC-ID`
- `X-PIN`
- legacy `software_version`
- `sensordatavalues`
- string values in payload

Important implementation note:

- the adapter must own sensor-family grouping and legacy field-name mapping
- the normalized model must stay generic

## 2. Air360 Native Backend Adapter

This adapter should target the native contract already outlined in the architecture docs:

- `POST /api/v1/ingest/measurements`
- bearer token authentication
- explicit `device`
- explicit `batch`
- numeric `measurements`

This adapter should be the clean reference implementation for the long-term API path.

## 3. Future Additional Backends

The architecture should make these cheap to add later:

- Madavi
- OpenSenseMap
- Feinstaub-App
- custom Air360-compatible regional gateways
- future internal relay/gateway backends

The goal is that a new backend requires:

1. one new adapter
2. one registry entry
3. optional backend-specific UI field metadata

It should not require changes in:

- `UploadManager`
- `StatusService`
- `WebServer` core backend page flow
- batch building
- retry scheduling

## Recommended File Layout

Suggested new module area:

- `firmware/main/include/air360/uploads/backend_types.hpp`
- `firmware/main/include/air360/uploads/backend_config.hpp`
- `firmware/main/include/air360/uploads/backend_registry.hpp`
- `firmware/main/include/air360/uploads/backend_uploader.hpp`
- `firmware/main/include/air360/uploads/upload_transport.hpp`
- `firmware/main/include/air360/uploads/upload_manager.hpp`
- `firmware/main/include/air360/uploads/measurement_batch.hpp`
- `firmware/main/include/air360/uploads/backend_config_repository.hpp`
- `firmware/main/include/air360/uploads/adapters/sensor_community_uploader.hpp`
- `firmware/main/include/air360/uploads/adapters/air360_api_uploader.hpp`
- `firmware/main/src/uploads/backend_registry.cpp`
- `firmware/main/src/uploads/upload_transport.cpp`
- `firmware/main/src/uploads/upload_manager.cpp`
- `firmware/main/src/uploads/backend_config_repository.cpp`
- `firmware/main/src/uploads/adapters/sensor_community_uploader.cpp`
- `firmware/main/src/uploads/adapters/air360_api_uploader.cpp`

Possible web integration files:

- `firmware/main/src/web_backends.cpp`
- or extend the existing `WebServer` with a backend route section while keeping rendering helpers separate

## Configuration Surface

## BackendConfigList

Recommended persisted structure:

- list header
  - magic
  - schema version
  - record size
  - backend count
  - next backend id
- array of `BackendRecord`

Recommended generic fields per backend:

- id
- enabled
- backend key
- display name
- upload interval override or inherit-default flag
- auth mode
- endpoint URL or host/path fields where applicable
- token/credential presence
- transport policy flags

Recommended backend-specific fields:

### Sensor.Community

- enabled
- protocol choice if still needed
- host override only if explicitly desired later
- pin and grouping behavior should remain adapter-internal, not user-configured

### Air360 API

- enabled
- base URL
- bearer token
- tenant/project/device-group fields only if the backend contract requires them

## Status Visibility

`/status` should expose a `backends[]` section with:

- backend key
- enabled
- configured
- state
- last attempt time
- last success time
- last result class
- last HTTP status
- retry count
- next retry time
- last short error

The root HTML page can stay shorter:

- enabled backend count
- last overall upload attempt
- number of degraded backends

The detailed diagnostics should live on `/backends` and `/status`.

## Phase Breakdown

The combined backend integration work should be delivered in seven concrete stages.

## Stage 0 — Contract Lock And Fixtures

Goal:

- freeze the first two backend contracts before writing runtime code

Work:

- extract exact Sensor.Community fixtures from `docs/airrohr-firmware-server-contract.md`
- freeze the first Air360 API request/response schema
- define canonical success/error classes
- define test fixtures for both adapters

Done when:

- both backend contracts have golden request fixtures
- the team agrees which parts are compatibility-critical and which are implementation choices

## Stage 1 — Upload Foundation

Maps primarily to Phase 4 foundation work.

Goal:

- create the generic upload architecture without binding it to one backend

Work:

- add canonical `MeasurementBatch`
- add `BackendRecord` / `BackendConfigList`
- add `BackendConfigRepository`
- add `IBackendUploader`
- add `BackendRegistry`
- add `UploadTransport`
- add `UploadManager`
- add status snapshot types

Done when:

- the firmware can build an upload batch from current sensor runtime state
- the batch can be passed through a mock uploader pipeline
- `/status` can expose empty backend state even before real uploads exist

## Stage 2 — Backend UI And Persistence

Still part of the pre-MVP backend slice.

Goal:

- make backend configuration manageable from the device UI

Work:

- add `/backends`
- list registry-provided backends
- create add/edit/delete flow
- save `BackendConfigList` to NVS
- reload backend config at boot
- render backend status placeholders

Done when:

- a user can enable or disable Sensor.Community and Air360 API in the UI
- backend settings survive reboot

## Stage 3 — Sensor.Community MVP

This is the actual Phase 4 functional milestone.

Goal:

- ship the first exact legacy-compatible upload path

Work:

- implement `SensorCommunityUploader`
- implement grouping of normalized measurements into legacy sensor-family requests
- generate `X-Sensor`, `X-MAC-ID`, `X-PIN`
- send one request per family
- add minimal retry policy:
  - retry on next interval
  - record last result

Done when:

- requests match fixtures
- uploads can be enabled or disabled in the UI
- failures are visible locally
- sensor polling continues even if uploads fail

## Stage 4 — Shared Multi-Backend Delivery

This is the first half of Phase 6.

Goal:

- turn the one-backend MVP into a real multi-backend delivery loop

Work:

- enable multiple backends simultaneously
- per-backend state and retry timers
- one batch fan-out to multiple adapters
- independent success/failure accounting
- bounded pending queue or small persistent outbox

Recommended first implementation:

- bounded in-memory queue with a small persistent checkpoint only if needed

Recommended follow-up:

- persistent bounded outbox when offline and retry pressure becomes real

Done when:

- one backend failing does not block another
- `/status` shows backend-specific health

## Stage 5 — Air360 Native Backend

This is the actual Phase 7 upload milestone.

Goal:

- implement the long-term native backend path

Work:

- implement `Air360ApiUploader`
- bearer token auth
- native `device + batch + measurements` body
- native response parsing and error classification
- dual-upload mode with Sensor.Community + Air360 enabled together

Done when:

- both backends can be active at once
- serialization is covered by host-side fixtures
- native backend errors do not affect legacy delivery

## Stage 6 — Hardening And Operational Visibility

This completes the combined backend integration slice before connectivity expansion.

Work:

- backoff tuning
- timeout tuning
- error classification cleanup
- queue depth visibility
- local backend diagnostics page polish
- test matrix for partial failure, no network, malformed config, and reboot recovery

Done when:

- backend health is understandable from `/status` and `/backends`
- retry behavior is deterministic and bounded

## Stage 7 — Phase 7 Connectivity Preparation

This stage is about keeping the backend stack compatible with future modem or relay work.

It does not need to implement modem or LoRa immediately.

Work:

- make `UploadTransport` provider-agnostic
- separate backend logic from Wi-Fi-specific assumptions
- define provider capability flags for:
  - direct HTTP allowed
  - metered connection
  - relay-only uplink
- define how Air360 native backend can support relay/gateway delivery later
- ensure legacy adapters can be disabled by provider policy without code changes

Done when:

- upload adapters do not call Wi-Fi-specific code directly
- a future cellular or relay provider can be added below the backend layer instead of rewriting adapters

## Suggested Implementation Order In Code

Recommended order of actual coding work:

1. batch model and upload status model
2. backend config repository
3. backend registry
4. upload transport
5. upload manager with no real adapters
6. `/backends` UI and persistence
7. Sensor.Community adapter
8. multi-backend fan-out and per-backend retries
9. Air360 adapter
10. hardening and connectivity-prep refactor

## Testing Strategy

## Host-side tests

- fixture-based serialization tests for Sensor.Community
- fixture-based serialization tests for Air360 API
- backend config validation tests
- retry scheduling tests
- per-backend status progression tests

## Firmware/integration tests

- enable one backend and verify upload attempts
- enable two backends and verify independent results
- disconnect network and verify retry/degraded state
- reboot with backend config present and verify startup reload
- malformed backend config should not break the rest of the runtime

## Hardware validation

- Wi-Fi connected, valid backend config
- Wi-Fi connected, invalid token
- Wi-Fi disconnected during upload
- one sensor active, multiple sensors active
- one backend active, two backends active

## Acceptance Criteria For The Combined Work

This combined Phase 4 + 6 + 7 plan is complete when:

- Sensor.Community legacy upload works exactly enough for migration-critical compatibility
- Air360 native upload works through a separate adapter
- both can be enabled together
- the UI lets the user configure and enable backends
- adding a third backend requires only a new adapter, a registry entry, and optional field metadata
- per-backend status is visible in `/status` and the UI
- upload logic stays decoupled from sensor drivers and Wi-Fi internals

## Final Recommendation

Do not implement this as:

- one-off upload code inside `App`
- backend-specific branches inside `WebServer`
- direct serialization from sensor drivers
- one global upload status bit

Implement it as:

- normalized batches
- backend registry
- per-backend adapters
- one upload manager
- separate backend config repository
- separate `/backends` UI
- provider-agnostic transport boundary

That is the cleanest path to deliver:

- legacy Sensor.Community compatibility
- Air360 native backend support
- future additional APIs
- later modem and relay expansion without redoing the upload architecture
