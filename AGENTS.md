# Air360 project guidance

## Current focus

- Active implementation work is centered on `firmware/` and `docs/firmware/`.
- `backend/` and `portal/` exist in the repository, but they are still early scaffolds and should not drive firmware assumptions.
- Treat firmware code and firmware implementation docs as the authoritative layer for current device behavior.

## Build commands

### Firmware — how to compile

`idf.py` is NOT on PATH. The only working invocation:

```bash
source ~/.espressif/v6.0/esp-idf/export.sh && idf.py build
```

Do not try `which idf.py`, do not search for it, do not try other paths. Use the line above verbatim from the `firmware/` directory.

## Source-of-truth rules

- Treat `firmware/` as the source of truth for implemented firmware behavior.
- Treat `docs/firmware/` as explanatory documentation for the current firmware implementation, not as an independent source of truth.
- Treat the rest of `docs/` as design, planning, or ecosystem context unless firmware implementation confirms it.
- Keep repository-level docs and firmware-level docs clearly separated.
- Avoid documenting generated files in `firmware/build/` except when narrow factual hints are needed.
- Preserve project-specific terminology and avoid generic boilerplate.

## Firmware entry points

Start here depending on the task:

| Task | Read first |
|------|------------|
| Understand the system at a glance | [`docs/firmware/ARCHITECTURE.md`](docs/firmware/ARCHITECTURE.md) |
| Navigate the source tree | [`docs/firmware/PROJECT_STRUCTURE.md`](docs/firmware/PROJECT_STRUCTURE.md) |
| Trace boot and init ordering | [`docs/firmware/startup-pipeline.md`](docs/firmware/startup-pipeline.md) |
| Understand persisted config and NVS | [`docs/firmware/nvs.md`](docs/firmware/nvs.md) + [`docs/firmware/configuration-reference.md`](docs/firmware/configuration-reference.md) |
| Understand Wi-Fi and SNTP | [`docs/firmware/network-manager.md`](docs/firmware/network-manager.md) + [`docs/firmware/time.md`](docs/firmware/time.md) |
| Understand cellular uplink | [`docs/firmware/cellular-manager.md`](docs/firmware/cellular-manager.md) + [`docs/firmware/sensors/sim7600e.md`](docs/firmware/sensors/sim7600e.md) |
| Understand sensor runtime | [`docs/firmware/sensors/README.md`](docs/firmware/sensors/README.md) + [`docs/firmware/transport-binding.md`](docs/firmware/transport-binding.md) |
| Add or modify a sensor | [`docs/firmware/sensors/adding-new-sensor.md`](docs/firmware/sensors/adding-new-sensor.md) + [`docs/firmware/sensors/supported-sensors.md`](docs/firmware/sensors/supported-sensors.md) |
| Understand measurement upload flow | [`docs/firmware/measurement-pipeline.md`](docs/firmware/measurement-pipeline.md) + [`docs/firmware/upload-adapters.md`](docs/firmware/upload-adapters.md) + [`docs/firmware/upload-transport.md`](docs/firmware/upload-transport.md) |
| Understand web UI routes and form behavior | [`docs/firmware/web-ui.md`](docs/firmware/web-ui.md) |
| Estimate change impact before editing | [`docs/firmware/change-impact-map.md`](docs/firmware/change-impact-map.md) |

## Common firmware tasks

Use these read paths before changing code:

| Change area | Read docs | Open code next |
|-------------|-----------|----------------|
| Boot sequence, startup bugs | `startup-pipeline.md`, `PROJECT_STRUCTURE.md` | `firmware/main/src/app_main.cpp`, `firmware/main/src/app.cpp` |
| Device config, defaults, NVS schemas | `nvs.md`, `configuration-reference.md` | `config_repository.cpp`, `sensors/sensor_config_repository.cpp`, `uploads/backend_config_repository.cpp`, `cellular_config_repository.cpp` |
| Wi-Fi, AP fallback, SNTP | `network-manager.md`, `time.md` | `network_manager.cpp`, `connectivity_checker.cpp` |
| Cellular uplink and modem GPIO | `cellular-manager.md`, `sensors/sim7600e.md` | `cellular_manager.cpp`, `cellular_config_repository.cpp`, `modem_gpio.cpp` |
| Sensor runtime or driver work | `sensors/README.md`, `transport-binding.md`, `sensors/adding-new-sensor.md` | `sensors/sensor_manager.cpp`, `sensors/sensor_registry.cpp`, `sensors/drivers/*` |
| Web UI routes or form handling | `web-ui.md`, `configuration-reference.md` | `web_server.cpp`, `web_ui.cpp`, `web_assets.cpp`, `main/webui/*` |
| Status page or JSON diagnostics | `web-ui.md`, `ARCHITECTURE.md` | `status_service.cpp`, `web_server.cpp` |
| Upload batching or backend logic | `measurement-pipeline.md`, `upload-adapters.md`, `upload-transport.md` | `uploads/upload_manager.cpp`, `uploads/measurement_store.cpp`, `uploads/backend_registry.cpp`, `uploads/adapters/*` |

## Firmware doc sync rules

When code changes, update the matching docs in the same change:

- Route or page changes: update `docs/firmware/web-ui.md`.
- NVS struct, defaults, or validation changes: update `docs/firmware/nvs.md` and `docs/firmware/configuration-reference.md`.
- Task lifecycle or boot order changes: update `docs/firmware/startup-pipeline.md` and usually `docs/firmware/ARCHITECTURE.md`.
- New sensor driver or binding behavior: update `docs/firmware/sensors/README.md`, `docs/firmware/sensors/supported-sensors.md`, `docs/firmware/sensors/adding-new-sensor.md`, and the per-driver page.
- Upload semantics or backend config changes: update `docs/firmware/measurement-pipeline.md`, `docs/firmware/upload-adapters.md`, and `docs/firmware/configuration-reference.md`.
- Structural refactors: update `docs/firmware/PROJECT_STRUCTURE.md` and, if responsibilities move, `docs/firmware/ARCHITECTURE.md`.

## Agent-oriented files

- Firmware-local working contract: [`firmware/AGENTS.md`](firmware/AGENTS.md)
- Firmware documentation index: [`docs/firmware/README.md`](docs/firmware/README.md)
- Firmware doc template: [`docs/firmware/doc-template.md`](docs/firmware/doc-template.md)
- Firmware doc hygiene checker: `python3 scripts/check_firmware_docs.py`

## Local firmware skills

- `.agents/skills/air360-firmware-docs/`
- `.agents/skills/esp-idf-cpp-developer/`
- `.agents/skills/firmware-doc-audit/`
- `.agents/skills/firmware-subsystem-walkthrough/`
- `.agents/skills/firmware-change-checklist/`
