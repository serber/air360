# Air360 firmware guidance

## Scope

- This file is the firmware-local working contract for AI agents operating inside `firmware/`.
- The canonical project root for build commands is `firmware/`.
- `firmware/` is the source of truth for implemented behavior. `../docs/firmware/` explains that implementation.

## Canonical build command

`idf.py` is NOT on PATH. The only working invocation:

```bash
source ~/.espressif/v6.0/esp-idf/export.sh && idf.py build
```

Run it verbatim from this directory.

## Read first

| If the task is about... | Read first |
|-------------------------|------------|
| Overall runtime architecture | [`../docs/firmware/ARCHITECTURE.md`](../docs/firmware/ARCHITECTURE.md) |
| Source layout and ownership | [`../docs/firmware/PROJECT_STRUCTURE.md`](../docs/firmware/PROJECT_STRUCTURE.md) |
| Boot and initialization | [`../docs/firmware/startup-pipeline.md`](../docs/firmware/startup-pipeline.md) |
| Device, sensor, backend, or cellular persistence | [`../docs/firmware/nvs.md`](../docs/firmware/nvs.md) + [`../docs/firmware/configuration-reference.md`](../docs/firmware/configuration-reference.md) |
| Wi-Fi / AP / SNTP behavior | [`../docs/firmware/network-manager.md`](../docs/firmware/network-manager.md) + [`../docs/firmware/time.md`](../docs/firmware/time.md) |
| Modem runtime | [`../docs/firmware/cellular-manager.md`](../docs/firmware/cellular-manager.md) |
| Sensor development | [`../docs/firmware/sensors/adding-new-sensor.md`](../docs/firmware/sensors/adding-new-sensor.md) + [`../docs/firmware/sensors/README.md`](../docs/firmware/sensors/README.md) |
| Web UI or HTTP routes | [`../docs/firmware/web-ui.md`](../docs/firmware/web-ui.md) |
| Queueing and uploads | [`../docs/firmware/measurement-pipeline.md`](../docs/firmware/measurement-pipeline.md) + [`../docs/firmware/upload-adapters.md`](../docs/firmware/upload-adapters.md) |

## First code files to inspect

- Startup: `main/src/app_main.cpp`, `main/src/app.cpp`
- Network: `main/src/network_manager.cpp`, `main/include/air360/network_manager.hpp`
- Cellular: `main/src/cellular_manager.cpp`, `main/src/modem_gpio.cpp`
- Persistence: `main/src/config_repository.cpp`, `main/src/sensors/sensor_config_repository.cpp`, `main/src/uploads/backend_config_repository.cpp`, `main/src/cellular_config_repository.cpp`
- Sensors: `main/src/sensors/sensor_manager.cpp`, `main/src/sensors/sensor_registry.cpp`, `main/src/sensors/drivers/*`
- Web UI: `main/src/web_server.cpp`, `main/src/web_ui.cpp`, `main/webui/*`
- Uploads: `main/src/uploads/upload_manager.cpp`, `main/src/uploads/measurement_store.cpp`, `main/src/uploads/upload_transport.cpp`, `main/src/uploads/adapters/*`

## Invariants

**TWDT:** Every `xTaskCreate`-spawned task that runs for more than ~5 seconds MUST call `esp_task_wdt_add(nullptr)` on entry, `esp_task_wdt_reset()` at its natural loop checkpoint, and `esp_task_wdt_delete(nullptr)` before `vTaskDelete(nullptr)`. If the task can sleep longer than 15 s, use `wdtFeedingDelay()` or an equivalent chunked wait instead of a single `vTaskDelay`. See [`../docs/firmware/watchdog.md`](../docs/firmware/watchdog.md).

**Timer callbacks:** Timer callbacks MUST NOT call `xTaskCreate`, allocate memory, or block. Their only permitted actions are setting atomic flags, calling `xTaskNotifyGive`, or posting to a queue with `xQueueSendFromISR`-style non-blocking primitives.

## Co-change expectations

- If you touch `main/Kconfig.projbuild` or `sdkconfig.defaults`, review `../docs/firmware/configuration-reference.md`, `../docs/firmware/ARCHITECTURE.md`, and any affected subsystem doc.
- If you add or rename HTTP routes in `main/src/web_server.cpp`, review `../docs/firmware/web-ui.md`.
- If you change NVS blob layout or defaults, review `../docs/firmware/nvs.md`, `../docs/firmware/configuration-reference.md`, and any matching ADR.
- If you add a sensor or transport option, review `../docs/firmware/sensors/README.md`, `../docs/firmware/sensors/supported-sensors.md`, `../docs/firmware/sensors/adding-new-sensor.md`, and `../docs/firmware/transport-binding.md`.
- If you change queue, batching, or backend semantics, review `../docs/firmware/measurement-pipeline.md`, `../docs/firmware/upload-adapters.md`, and `../docs/firmware/upload-transport.md`.
- **If you add a new FreeRTOS task**, subscribe it to the TWDT, add it to the table in `../docs/firmware/watchdog.md`, and update `../docs/firmware/startup-pipeline.md` if it is spawned during boot.

## Verification checklist

Before closing firmware work, do the smallest applicable set:

1. **Re-read the issue/plan that motivated the change.** Tick every numbered step in the Fix plan. If a step is skipped, document it in `## Outstanding` in the issue file — never leave it implied.
2. **Check Co-change expectations below** for every file touched. Apply all matching rules.
3. Re-read the affected subsystem docs and make them match the code.
4. Run the canonical firmware build if the change touched C++ or build config.
5. Run `python3 ../scripts/check_firmware_docs.py`.
6. If the change introduced a new document or link, confirm the doc checker stays clean.
7. **Self-review the diff before reporting done.** Read every changed line as a reviewer would. Check for: redundant conditions already implied by surrounding context; skipped plan steps without an `## Outstanding` entry; logic that silently changes behaviour in both directions when the intent was one-directional; inconsistent application of a pattern across similar call sites in the same file.

## Code style

### Log tags

Every translation unit that uses `ESP_LOG*` must define its tag as:

```cpp
namespace {
constexpr char kTag[] = "air360.<subsystem>";
}
```

Rules:
- Name must be `kTag` (not `TAG`, `LOG_TAG`, or any other identifier).
- Declare inside an anonymous `namespace {}` block.
- Tag value must start with `air360.` followed by a short subsystem identifier (dots allowed for sub-levels, e.g. `air360.cellular.cfg`).
- Do not use `static const char* TAG` or macro-based tags.

Subsystem identifiers in use: `app`, `backend_cfg`, `ble`, `cellular`, `cellular.cfg`, `config`, `connectivity`, `http`, `modem_gpio`, `net`, `sensor`, `sensor_cfg`, `upload`, `web`.

Run `python3 ../scripts/check_style.py` to verify; this check fails on any deviation.

### Return values

- Discarding a return value requires either a function with no `[[nodiscard]]` contract, or an explicit one-line comment immediately before `static_cast<void>(...)` explaining why the result is intentionally unused.
- Return values that affect observability, recovery, persistence, or task lifecycle should be logged, exposed through status, counted, or propagated instead of discarded.

## Documentation conventions

- Prefer implementation docs in `../docs/firmware/` over repository-level planning docs.
- Use the standard header shape from [`../docs/firmware/doc-template.md`](../docs/firmware/doc-template.md).
- Keep “what to read next” links concrete.
- Link to code paths, not generated build outputs.

## Skills

See the full skill index and trigger table in [`../AGENTS.md`](../AGENTS.md).

| Skill | Use when |
|-------|----------|
| `esp-idf-cpp-developer` | Any C++ code change: driver, task, NVS, CMake, Kconfig, build failure |
| `firmware-change-checklist` | Before or after a code change — confirm co-change files and docs |
| `firmware-doc-audit` | Broad doc hygiene pass or post-change doc accuracy check |
| `firmware-subsystem-walkthrough` | First-time exploration of boot, sensors, networking, uploads, or web UI |
| `air360-firmware-docs` | Creating or rewriting a doc in `docs/firmware/` or `firmware/README.md` |
