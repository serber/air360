# Air360 firmware issue tracker

Snapshot of a senior-level firmware review conducted on 2026-04-20 against the `code_review` branch. Each file documents one issue: location, symptom, consequence on real hardware, and a concrete fix plan.

## Severity levels

- **Critical** — must be fixed before any external deployment. Current behavior can silently lose data, hang the device, or cause non-deterministic crashes under ordinary field conditions.
- **High** — fix this quarter. Production-grade problems that will bite within weeks of real-world deployment.
- **Medium** — fix before the next release. Latent defects, maintainability hazards, and board-revision landmines.
- **Low** — cosmetics and discipline. Address opportunistically.

## Index

### Critical
- [C1 — Global mutable I²C context in the Sensirion HAL shim](C1-sensirion-hal-global-state.md)
- [C2 — Measurement queue is RAM-only, bounded at 256](C2-measurement-queue-ram-only.md)
- [C3 — Cellular reconnect blocks on `portMAX_DELAY` with no watchdog](C3-cellular-portmax-delay-implemented.md)
- [C4 — Main task removes itself from watchdog, subsystem coverage incomplete](C4-watchdog-audit-gap-implemented.md)
- [C5 — `BleAdvertiser::stop()` kills BLE task from foreign context](C5-ble-vtaskdelete-foreign.md)
- [C6 — Timer daemon callbacks spawn full tasks](C6-timer-spawns-tasks.md)

### High
- [H1 — `esp_http_client_init`/`cleanup` per upload, no keep-alive](H1-http-no-keepalive.md)
- [H2 — HTTP response buffer 512 B is too small](H2-http-response-buffer-implemented.md)
- [H3 — Homegrown HTML templating in `web_server.cpp`, no escaping](H3-web-server-xss.md)
- [H4 — In-place NVS schema evolution via reserved bytes](H4-nvs-schema-reserved-bytes.md)
- [H5 — Wipe-and-default on NVS corruption](H5-nvs-wipe-to-default.md)
- [H6 — `SensorManager` retries driver init with no backoff](H6-sensor-init-no-backoff.md)
- [H7 — STL containers on hot paths, heap fragmentation risk](H7-stl-hot-paths.md)
- [H8 — Cellular 5-attempt PWRKEY cadence is wear-prone](H8-cellular-pwrkey-cadence.md)
- [H9 — `using namespace esp_modem;` in a translation unit](H9-using-namespace-esp-modem.md)
- [H10 — Cross-backend prune cursor lacks property tests](H10-upload-manager-prune-tests.md)

### Medium
- [M1 — `scanAvailableNetworks` uses blocking Wi-Fi scan](M1-wifi-scan-blocking.md)
- [M2 — Positional aggregate initialization of `SensorDescriptor`](M2-sensor-descriptor-positional-init.md)
- [M3 — Transport binding hardcodes bus 0 and UART 1/2](M3-transport-binding-hardcoded.md)
- [M4 — Ignored return values via `static_cast<void>`](M4-void-cast-returns.md)
- [M5 — DHT/BME280 drivers force full re-init on any error](M5-sensor-reinit-on-error.md)
- [M6 — `ConnectivityChecker` allocates an event group per call](M6-connectivity-checker-allocations.md)
- [M7 — `web_server.cpp` stack 10 KB with `std::string` rendering](M7-web-server-stack.md)
- [M8 — Manual BLE advertisement build to dodge NimBLE API churn](M8-ble-manual-adv-build.md)
- [M9 — GPS poll budget/timeout combination can starve](M9-gps-poll-budget.md)
- [M10 — `connect_attempt_task` handle read without synchronization](M10-connect-attempt-task-race.md)

### Low
- [L1 — Inconsistent log tag conventions](L1-log-tag-convention.md)
- [L2 — Unreadable byte-packing casts in `ble_advertiser`](L2-ble-byte-packing-casts.md)
- [L3 — Raw array + `sizeof`/`sizeof[0]` instead of `std::array`](L3-kbthomemap-std-array.md)
- [L4 — C++ headers inside `extern "C"` blocks](L4-extern-c-headers.md)
- [L5 — Magic constants lack rationale](L5-magic-constants.md)

## How to use this directory

- One issue per file. Title and severity in the frontmatter-like header.
- When an issue is fixed, move the file to `docs/issues/resolved/<id>-<slug>.md` and add a `## Resolved` section with commit SHA and date.
- Reference these IDs in commit messages (e.g. `Fix C3: bounded cellular reconnect wait`).
- Scores and the prioritized refactor plan live in the original review (see conversation context, 2026-04-20).
