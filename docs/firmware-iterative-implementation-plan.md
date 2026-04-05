# Iterative Implementation Plan for the New `sensor.community` Firmware

## Source Basis and Validation Notes

This plan is based primarily on:

- `docs/modern-replacement-firmware-architecture.md`
- `docs/repository-overview.md`
- `docs/airrohr-firmware-server-contract.md`
- `docs/airrohr-firmware-ui-analysis.md`
- current `airrohr-firmware/` source and `platformio.ini`

Important source-validated planning notes:

- The current onboarding flow is real and compatibility-critical: failed station join falls back to SoftAP at `192.168.4.1` with `/config`, wildcard DNS, and captive-portal aliases.
- Legacy backend compatibility is real and specific: Sensor.Community is one `POST` per sensor family; Madavi/OpenSenseMap/Feinstaub-App are aggregate uploads.
- The current ESP32 path is not a stable implementation baseline:
  - ESP32 build environments in `airrohr-firmware/platformio.ini` are disabled.
  - ESP32 upload/TLS handling is incomplete in the current code.
  - some UI settings shown on ESP32 are effectively inert there today.
- BME/BMP support already exists in the old code and is a good first environmental-sensor slice, but the new firmware should implement it through the new driver/runtime architecture rather than porting the old code structure.

# 1. Planning Assumptions

## Architecture direction

- The new firmware will follow the architecture proposal already documented:
  - ESP32-first
  - ESP-IDF + C++17
  - small core runtime
  - explicit module boundaries
  - legacy behavior isolated in adapters
- The new project should be created as a new firmware codebase, not as a direct refactor of `airrohr-firmware`.

## Compatibility priorities

- Highest priority compatibility for early releases:
  - Sensor.Community upload contract
  - AP onboarding flow centered on `/config`
  - core local status/config route expectations
  - import of the legacy `config.json` key model where practical
- Secondary compatibility to add after the first upload slice:
  - Madavi
  - OpenSenseMap
  - Feinstaub-App

## UI and onboarding expectations

- The first usable firmware must already support two operating modes:
  - setup mode via AP when no valid config exists
  - normal mode when config exists and the device joins the configured network
- The first UX target is functional, not polished:
  - minimal forms
  - save-and-reboot flow
  - status page
  - visible errors

## Backend and upload expectations

- Upload architecture must start with a normalized internal model, even before multiple backends exist.
- Legacy contract support must be implemented as adapters, not as the canonical domain model.
- The first upload implementation should be narrow and exact.

## Sensor extensibility expectations

- Sensor work should start with one sensor vertical slice.
- Drivers should bind to board/bus abstractions from the start.
- Adding later sensors should not require changing upload logic.

## Connectivity expectations

- Wi-Fi is the only connectivity mode required in the first implementation phases.
- The architecture must not hard-code Wi-Fi into measurement or upload logic.
- Cellular and LoRa should be planned for structurally, but not implemented in the first working milestones.

## Assumptions to validate early

- The chosen ESP32 board for the first target and its flash/PSRAM/storage expectations.
- The exact local storage choice for the new firmware:
  - LittleFS vs SPIFFS vs NVS split
- The exact Sensor.Community acceptance requirements:
  - headers
  - `X-PIN` values
  - field-name formatting
- Whether `/data.json` and `/metrics` are needed in MVP or can land one phase later.
- Whether importing existing `config.json` should be in MVP or immediately after MVP.

# 2. Delivery Strategy

## Overall strategy

The implementation order should follow thin vertical slices that produce a real device behavior at every step:

1. bootable firmware and local web serving
2. AP onboarding and persistent configuration
3. first real sensor read and local visibility
4. first exact legacy upload
5. broader sensor and backend expansion

## Why the first milestone is firmware base + configuration UI

- No backend or sensor work matters until the device can be configured reliably on real hardware.
- The highest early risk is not serialization or TSDB design; it is:
  - boot stability
  - storage
  - AP mode
  - joining Wi-Fi
  - local recovery when config is bad
- A functioning AP onboarding flow gives immediate hardware validation and user migration confidence.

## Why sensor support should be incremental

- Sensors vary heavily in bus model, warm-up, timing, and failure behavior.
- Implementing many sensors before the runtime model is proven will hide architecture mistakes behind hardware noise.
- A first environmental sensor slice is enough to validate:
  - bus manager
  - driver lifecycle
  - measurement normalization
  - local status rendering

## How compatibility influences sequencing

- Compatibility must enter early, but only at the narrowest meaningful edge.
- The first compatibility target should be Sensor.Community because it is the main migration path and the most contract-sensitive.
- Secondary legacy backends should wait until the first upload path is proven.

## How this reduces risk

- It keeps the first two phases almost entirely local and observable.
- It delays multi-backend complexity until one upload path is stable.
- It avoids building modem/LoRa abstractions by speculation before Wi-Fi and upload plumbing are proven.
- It ensures every phase is independently demoable.

# 3. Implementation Phases

## Phase 1 — Firmware Foundation

### Goal

Get a clean new ESP32 firmware project booting on hardware with basic diagnostics, configuration storage, and a minimal local web server.

### Scope

- new firmware project skeleton
- build system and board target
- boot path
- structured logging
- config repository foundation
- minimal HTTP server

### What will be implemented

- new codebase layout matching the architecture proposal
- authoritative build setup for ESP-IDF + C++17
- one supported ESP32 board profile
- basic runtime bootstrap:
  - app start
  - watchdog setup
  - logging
  - storage init
- config repository capable of:
  - loading defaults
  - reading a stored config
  - writing a stored config
- minimal HTTP server with:
  - `/`
  - `/status`
  - health/build info

### What will not be implemented yet

- AP onboarding
- Wi-Fi join logic beyond minimal bring-up test hooks
- sensors
- uploads
- legacy config import
- OTA

### Dependencies

- choice of first ESP32 board
- ESP-IDF project setup
- storage choice and partition layout

### Risks

- overbuilding abstractions before the board boots reliably
- picking a storage layout that complicates later config/outbox separation

### Validation / test criteria

- firmware flashes and boots on the target ESP32
- logs show deterministic boot sequence
- local HTTP server responds on the device in a controlled network setup
- config can be written and read back across reboot

### Expected output / artifacts

- new firmware project directory
- board profile for one ESP32 target
- config storage module
- basic status page
- first hardware boot notes

## Phase 2 — Onboarding and Local Configuration UX

### Goal

Make the device self-configurable on real hardware in both setup mode and normal mode.

### Scope

- AP mode when unconfigured
- config form
- Wi-Fi station join flow
- normal mode web access
- reboot/apply behavior

### What will be implemented

- config validity check at boot
- AP onboarding mode when config is missing or invalid
- AP IP `192.168.4.1`
- local HTTP routes for config and status
- basic config form containing:
  - Wi-Fi SSID/password
  - device name / AP name
  - optional local auth toggle placeholder
- save config and reboot flow
- normal-mode station join using saved credentials
- normal-mode local web UI availability
- status page showing:
  - boot mode
  - network state
  - stored config summary

### What will not be implemented yet

- rich UI tabs
- full legacy settings matrix
- sensor configuration
- upload configuration beyond placeholders
- live config apply without reboot

### Dependencies

- Phase 1 config repository and web server
- Wi-Fi provider skeleton

### Risks

- AP UX edge cases
- captive portal and browser behavior
- bad-config recovery loops

### Validation / test criteria

- with no config, device starts AP and serves config page
- user can submit Wi-Fi credentials and reboot
- device joins Wi-Fi in normal mode after reboot
- local UI is reachable in normal mode
- invalid config returns device to recoverable setup mode

### Expected output / artifacts

- working onboarding flow on hardware
- config persistence across reboot
- normal-mode local status/config access
- first user-facing demo path

## Phase 3 — First Sensor Vertical Slice

### Goal

Add one simple, highly testable sensor slice end to end, starting with a BME/BMX-class sensor.

### Scope

- first bus abstraction used for a real sensor
- first sensor driver
- normalized measurement model
- local measurement visibility

### What will be implemented

- I2C bus manager for the chosen board profile
- first sensor driver:
  - prefer BME280
  - BMP280 acceptable if humidity hardware is unavailable for early bring-up
- driver lifecycle:
  - init
  - state
  - sample
  - error state
- normalized measurement objects for:
  - temperature
  - pressure
  - humidity when available
- status page section for:
  - current values
  - sample age
  - sensor health

### What will not be implemented yet

- multiple sensors
- complex calibration UI
- measurement batching for multiple sensor instances
- PM sensor timing complexity

### Dependencies

- Phase 2 normal mode
- board-level I2C mapping

### Risks

- bus initialization bugs masked as sensor bugs
- trying to generalize driver abstractions too early

### Validation / test criteria

- sensor is detected on hardware
- values update on the local status page
- disconnected sensor produces a clear degraded/absent state
- repeated sampling is stable over time

### Expected output / artifacts

- first production-style sensor driver
- first normalized measurement path
- first sensor diagnostics surface

## Phase 4 — Legacy-Compatible Upload MVP

### Goal

Prove that the new firmware can send exact compatibility-critical data to the legacy ecosystem.

### Scope

- first backend adapter
- first upload config
- basic retry/error accounting

### What will be implemented

- backend uploader interface and registry with one concrete adapter
- Sensor.Community adapter using exact legacy request requirements:
  - per-sensor-family `POST`
  - `X-Sensor`
  - `X-MAC-ID`
  - `X-PIN`
  - `software_version`
  - `sensordatavalues`
  - string values
- upload enable/disable config
- minimal periodic reporting schedule
- basic retry behavior:
  - retry next interval
  - last result visible in status page

### What will not be implemented yet

- multiple backends
- outbox persistence beyond minimal in-memory or very small bounded queue
- native backend upload
- complex backoff tuning

### Dependencies

- Phase 3 measurement path
- validated legacy field mapping for the first sensor

### Risks

- subtle contract mismatches
- incorrect header construction on ESP32
- sending the wrong metric names because the internal model leaks into the compatibility layer

### Validation / test criteria

- device emits the exact expected request shape for the first sensor
- requests can be inspected and compared against fixtures
- upload success/failure state is visible locally
- upload failures do not crash sampling or the UI

### Expected output / artifacts

- first working legacy upload path
- golden request fixtures
- upload status on device

## Phase 5 — Sensor Expansion Framework

### Goal

Prove that adding sensors is routine rather than architectural.

### Scope

- add next sensors incrementally
- tighten sensor registration and bus-binding model
- define the team procedure for future sensor additions

### What will be implemented

- sensor registry and factory cleanup after first real driver experience
- second and third sensor drivers, chosen to widen coverage:
  - one PM sensor
  - one additional environmental or auxiliary sensor
- sensor instance config structure
- documented procedure and tests for adding a new driver

### What will not be implemented yet

- the full long-tail sensor matrix
- dynamic auto-detection for every bus and every driver
- advanced sensor calibration UX

### Dependencies

- stable Phase 3 driver model
- stable measurement normalization model

### Risks

- phase drifting into “support everything”
- PM sensor warm-up/timing complicating the runtime too early

### Validation / test criteria

- second and third sensors can be added without changing upload core
- status page can show multiple sensor states cleanly
- configuration model supports multiple sensor instances without confusion

### Expected output / artifacts

- stable sensor-extension pattern
- initial supported sensor set
- driver-addition checklist and tests

## Phase 6 — Multi-Backend Upload Support

### Goal

Extend the upload layer from one exact compatibility adapter to a real multi-target delivery system.

### Scope

- multiple backend adapters
- per-backend config
- per-backend status
- parallel or sequential fan-out policy

### What will be implemented

- backend registry used by multiple adapters
- adapters for:
  - Madavi
  - OpenSenseMap
  - Feinstaub-App
- aggregate JSON compatibility adapter path where required
- backend enable/disable config
- per-backend status visibility
- controlled multi-backend delivery loop

### What will not be implemented yet

- modem-aware upload policy
- LoRa relay delivery
- persistent durable outbox for long offline periods unless already clearly needed

### Dependencies

- stable Phase 4 uploader and normalized batch model

### Risks

- retry and error state becoming tangled across backends
- adapter-specific schema logic leaking into shared code

### Validation / test criteria

- two or more backends can be enabled simultaneously
- one backend failure does not block others
- per-backend status is visible and understandable

### Expected output / artifacts

- multi-backend delivery manager
- first wave of legacy secondary adapters
- backend-specific configuration and diagnostics

## Phase 7 — Native Backend Integration

### Goal

Establish the first clean non-legacy upload path for the new platform.

### Scope

- native API adapter
- request validation
- dual-upload migration mode

### What will be implemented

- native backend adapter targeting the documented contract:
  - `POST /api/v1/ingest/measurements`
  - bearer token auth
  - `device` + `batch` + numeric `measurements`
- native backend configuration
- dual-upload mode:
  - legacy backend(s)
  - native backend
- host-side schema fixtures and serialization tests

### What will not be implemented yet

- relay/gateway variant of the native API
- advanced auth rotation workflows
- server-driven commands/config

### Dependencies

- stable normalized measurement model
- stable upload registry
- available backend endpoint or mock service

### Risks

- API churn before the first backend consumer stabilizes
- overfitting the firmware to one storage backend

### Validation / test criteria

- firmware emits requests matching documented native examples
- backend accepts and stores sample data
- dual upload works without corrupting legacy compatibility behavior

### Expected output / artifacts

- first native upload path
- request fixtures and schema tests
- dual-upload migration demonstration

## Phase 8 — Hardening and Platform Growth

### Goal

Turn the working firmware into a stable platform base and prepare cleanly for later feature growth.

### Scope

- config versioning and migration
- stronger diagnostics
- recovery behavior
- test coverage growth
- OTA groundwork
- connectivity-growth groundwork

### What will be implemented

- config schema versioning and migration pipeline
- legacy config import if not already delivered
- improved fault handling:
  - boot-loop guard
  - bad-config recovery
  - clearer degraded states
- diagnostics improvements:
  - logs
  - queue/backend state
  - sensor visibility
- initial persistent outbox if needed by observed field behavior
- OTA design hooks
- connectivity abstraction clean-up for future modem/LoRa work

### What will not be implemented yet

- full modem support
- LoRa/mesh support
- final polished OTA feature
- full UI polish pass

### Dependencies

- earlier phases proven on hardware

### Risks

- hardening phase turning into endless cleanup
- adding deferred features before core stability is measured

### Validation / test criteria

- repeated reboot and recovery scenarios are survivable
- config upgrades work
- field diagnostics are good enough to troubleshoot failures without source-level debugging

### Expected output / artifacts

- stable pre-production baseline
- migration and diagnostics support
- foundation for OTA and additional uplinks

# 4. Recommended MVP Boundary

## Must be in MVP

- new ESP32 firmware project
- one supported ESP32 board
- local config storage
- AP onboarding on `192.168.4.1`
- normal-mode Wi-Fi join
- local status/config web UI
- one first sensor vertical slice:
  - preferably BME280
- Sensor.Community legacy upload for that first sensor
- basic upload health visibility

## Should not be in MVP

- multiple sensor families
- multiple backends
- modem support
- LoRa support
- OTA
- rich diagnostics export
- advanced UI polish
- live reconfiguration without reboot

## Mandatory compatibility in MVP

- AP onboarding behavior centered on `/config`
- save-and-reboot config apply flow
- Sensor.Community request contract for the first sensor

## Sensor support enough for MVP

- one BME280 or BMP280/BME280 driver is enough
- if a PM sensor is easier to validate against live Sensor.Community expectations in the first hardware setup, add it immediately after MVP, not inside MVP

## Backend support enough for MVP

- Sensor.Community only
- native backend can wait until after compatibility MVP unless the team needs dual-upload from day one

# 5. Sensor Rollout Order

## Recommended implementation order

| Order | Sensor | Why this order |
| --- | --- | --- |
| 1 | BME280 | low bus complexity, high observability, useful local status values, validates I2C + normalized model |
| 2 | BMP280 if needed as fallback | same path as BME280, useful for validating shared driver structure |
| 3 | SDS011 or one primary PM sensor | high compatibility value, validates legacy upload mapping more directly |
| 4 | SHT3x or equivalent environmental sensor | broadens environmental driver model without PM timing complexity |
| 5 | PMSx003 or SPS30 | extends PM model and timing behavior |
| 6 | DS18B20 | useful OneWire path, relatively isolated complexity |
| 7 | SCD30 / CO2-class sensor | slower timing and more complex sample cadence |

## Why BME280 first

- It is simple enough for early hardware validation.
- It provides immediate human-readable values on the local UI.
- It exercises core abstractions without forcing PM-specific timing and warm-up logic first.
- The current firmware already supports the BME/BMP path, so metric mapping is understood.

## Prioritization criteria

- architectural simplicity first
- hardware availability second
- compatibility importance third
- timing complexity later
- exotic sensors last

# 6. Early Validation Milestones

## Milestone 1

- device boots on ESP32
- serial logs show initialization
- `/status` responds on a controlled network

## Milestone 2

- with no config, device starts AP mode
- AP is reachable
- `/config` page loads at `192.168.4.1`

## Milestone 3

- user saves Wi-Fi credentials
- config persists across reboot
- device joins the configured network

## Milestone 4

- normal-mode local UI is reachable
- status page shows configuration summary and network state

## Milestone 5

- first sensor is detected and sampled
- current values are visible locally
- sensor disconnect produces a clear health/error state

## Milestone 6

- first legacy-compatible upload succeeds
- request shape matches fixture
- local UI shows upload result

## Milestone 7

- one backend can fail without hanging the device
- reboot after bad config remains recoverable

# 7. Risks by Phase

| Phase | Main risks | Risk reduction |
| --- | --- | --- |
| 1 | over-architecting before first boot | keep the first board target narrow and the module count minimal |
| 2 | AP onboarding complexity, storage bugs, recovery loops | prove save/reboot/recover paths on real hardware before adding sensors |
| 3 | sensor and bus bugs confusing the architecture | start with one I2C sensor and explicit sensor-state reporting |
| 4 | legacy contract mismatch | use golden request fixtures and compare exact headers/body |
| 5 | abstraction collapse under more sensors | add only a few representative sensors before expanding further |
| 6 | multi-backend failure state getting tangled | track status per backend from the start |
| 7 | native API churn | freeze a small contract first and test against a mock service |
| 8 | endless hardening and feature creep | enter only after measured hardware success in earlier phases |

# 8. What to Defer Intentionally

- **Cellular modem support**
  - defer because Wi-Fi onboarding, config, and upload architecture need to be proven first
- **LoRa / relay uplink**
  - defer because it changes transport assumptions and should not contaminate the initial direct-IP path
- **OTA**
  - defer until config storage, recovery behavior, and basic platform stability are proven
- **Many sensors at once**
  - defer to avoid turning early runtime validation into a hardware matrix problem
- **Advanced dashboard and cloud features**
  - defer because the firmware only needs a stable native upload contract first
- **Complex auth model**
  - bearer token for native API is enough initially
- **UI polish and rich client behavior**
  - defer because the first requirement is reliability and recoverability
- **Deep calibration workflows**
  - defer until sensor lifecycle and persistence are stable

The common rule is simple: if a feature does not improve the first hardware-tested setup, measurement, or upload slice, it should probably wait.

# 9. Work Breakdown for the First Two Phases

## Phase 1 task breakdown

| Task | Purpose | Dependencies | Deliverable | How to verify |
| --- | --- | --- | --- | --- |
| Create new firmware project | establish a clean codebase and build | none | new ESP-IDF project scaffold | project configures and builds locally |
| Select first target board | avoid hardware ambiguity | board availability | one documented target board profile | flashing and boot reproducible on the chosen board |
| Define partition/layout plan | prevent storage churn later | target board choice | initial partition table and storage notes | config storage partition mounts successfully |
| Build app bootstrap | prove runtime entry and logging | project scaffold | app start, logger init, watchdog init | serial log shows deterministic startup |
| Implement config repository v0 | persistent settings foundation | storage init | load/save config with defaults | config round-trips across reboot |
| Add basic status model | make boot state visible | app bootstrap | in-memory status snapshot | status contains build info and boot mode |
| Start minimal HTTP server | first local surface | app bootstrap | `/` and `/status` routes | browser or curl reaches endpoints |
| Wire storage-backed config load at boot | connect runtime to stored settings | config repository | boot reads config or defaults | logs show config path taken |
| Add reset-safe default config path | avoid dead-end configs | config repository | default config initialization | device boots cleanly with empty storage |
| Capture first hardware bring-up notes | stabilize early iteration | all above | short bring-up checklist | repeated flash/boot runs match notes |

## Phase 2 task breakdown

| Task | Purpose | Dependencies | Deliverable | How to verify |
| --- | --- | --- | --- | --- |
| Add Wi-Fi provider skeleton | support AP and station state | Phase 1 bootstrap | Wi-Fi module with state reporting | logs show provider state transitions |
| Add unconfigured-device detection | enter setup mode correctly | config repository | boot decision logic | erased config forces setup path |
| Implement AP startup at `192.168.4.1` | reproduce expected onboarding entry | Wi-Fi skeleton | AP mode start routine | phone/laptop can join AP and reach it |
| Add setup web routes | provide first config UI | HTTP server, AP mode | `/config` GET and POST | config page loads in AP mode |
| Implement config form for Wi-Fi/basic settings | collect minimum usable config | setup routes | HTML form and server-side parsing | submitted values persist correctly |
| Save-and-reboot flow | apply config safely | config repository | config commit + reboot handler | submit causes reboot and stored config remains |
| Add station join flow | support normal mode | saved Wi-Fi config | station-mode connect routine | device joins configured Wi-Fi after reboot |
| Add normal-mode UI routing | keep device manageable after setup | station join flow | `/`, `/status`, `/config` in normal mode | pages load on station IP |
| Add setup recovery path | prevent bricking on bad credentials | AP + station flows | fallback/recovery logic | invalid credentials return device to recoverable setup mode |
| Add visible network/config status | make setup results observable | status model, Wi-Fi provider | status page with network/config summary | user can see mode, IP, and config state |

# 10. Suggested First Development Sprint

## Sprint objective

Deliver the first hardware-tested slice that proves the new project can boot, persist config, and serve a minimal local UI.

## Best first sprint scope

- create the new ESP-IDF firmware project
- select one ESP32 target board
- implement app bootstrap and logging
- implement config repository v0
- implement minimal HTTP server
- expose `/status`
- add a stub `/config` page
- prove config save/load across reboot
- wire boot-mode decision for configured vs unconfigured state
- add AP mode skeleton, even if the first sprint ends before full form submission

## Why this is the right first sprint

- It keeps the work local and observable.
- It gives immediate hardware proof.
- It avoids premature sensor and backend complexity.
- It establishes the project structure that every later phase depends on.

## Sprint success definition

- the device boots reliably
- a browser can reach the device-hosted UI
- config persists across reboot
- the codebase is ready to add AP onboarding form handling next

# 11. Final Recommendation

Start with a new ESP32 firmware project that does only four things well in the first iteration:

- boot cleanly
- expose a local web surface
- persist config
- distinguish setup mode from normal mode

In the first week, prioritize Phase 1 plus the opening slice of Phase 2:

- project scaffold
- board profile
- storage
- `/status`
- `/config` skeleton
- AP startup

Avoid doing these too early:

- multi-sensor architecture for every future case
- multi-backend fan-out
- modem or LoRa implementation
- OTA
- polished UI work

Success for the first hardware-tested iteration is not “architecture completeness.” Success is:

- erased device boots into a recoverable setup path
- local UI is reachable
- config can be saved and survives reboot
- the device can transition toward normal mode without manual reflashing

## Start Here

1. Create the new ESP-IDF firmware project and commit the empty project skeleton.
2. Choose and document the single first ESP32 board target.
3. Define the initial partition table for app, config storage, and future headroom.
4. Implement app bootstrap with structured serial logging and boot reason reporting.
5. Implement storage initialization and a minimal config repository with defaults.
6. Add a `ResolvedConfig` model and boot decision logic for configured vs unconfigured state.
7. Start a minimal HTTP server and expose `/status`.
8. Add a stub `/config` page that renders in both configured and unconfigured modes.
9. Implement AP-mode startup on `192.168.4.1` for the unconfigured state.
10. Implement config save-and-reboot for Wi-Fi credentials and basic device settings.

## Initial Backlog

### `BOOT-001 Create ESP-IDF Project Skeleton`

Set up the new firmware as a fresh ESP-IDF + C++17 project with a clear component layout that matches the architecture proposal. The result should build on a clean machine and define the long-term code organization from the start. This item is complete when the project builds and flashes even if the application only logs startup.

### `BOOT-002 Select First ESP32 Board Profile`

Choose one concrete ESP32 board for the first supported target and define its pin, flash, and storage assumptions. This keeps all early work grounded in a real hardware profile instead of a generic abstraction. Completion means the board profile is documented and used by the build and bootstrap path.

### `BOOT-003 Add Bootstrap and Logging`

Implement the runtime entry path, early logger setup, watchdog-safe startup structure, and a small status object carrying boot metadata. This gives every later feature a visible execution trail. Completion means serial output clearly shows startup stages and build information.

### `CFG-001 Implement Config Repository v0`

Create the first persistent config storage module with default config generation, load, save, and reload support. Keep the schema small at first: enough to drive setup mode versus normal mode and hold Wi-Fi credentials later. Completion means config survives reboot and missing storage falls back to defaults safely.

### `WEB-001 Start Minimal HTTP Server`

Bring up the first on-device HTTP server with a basic `/status` endpoint and a placeholder root page. This proves the device can host a local management surface and that tasking/network foundations are sound. Completion means the page is reachable from a browser on hardware.

### `MODE-001 Add Configured vs Unconfigured Boot Decision`

Teach the firmware to decide at boot whether it has enough valid config to enter normal mode or should go into setup mode. This is the first real state-machine behavior in the new firmware. Completion means erased storage deterministically enters the unconfigured path.

### `WIFI-001 Implement AP Setup Skeleton`

Add AP-mode startup for the unconfigured path, using the compatibility-oriented local address and a minimal page-serving setup. Do not chase full captive portal fidelity yet; get the AP and local HTTP path working first. Completion means a user can connect to the device AP and load the config page.

### `WEB-002 Add Config Form v0`

Implement the first config form and server-side POST handling for only the minimum useful fields: Wi-Fi credentials and basic device/AP naming. Keep validation simple and explicit. Completion means form submission updates stored config correctly.

### `CFG-002 Add Save and Reboot Flow`

Wire config submission to a safe save-and-reboot path so changes take effect using the same operational model planned for MVP. This matches the compatibility direction and keeps early runtime complexity down. Completion means submitted config persists and the firmware restarts into the expected mode.

### `WIFI-002 Add Station Join Flow`

Use the saved Wi-Fi credentials to bring the device up in normal mode and keep the local UI reachable on the joined network. This closes the first full setup-to-normal operation loop. Completion means a newly configured device joins Wi-Fi after reboot and serves the status/config pages on the network.
