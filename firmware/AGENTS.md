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

## Co-change expectations

- If you touch `main/Kconfig.projbuild` or `sdkconfig.defaults`, review `../docs/firmware/configuration-reference.md`, `../docs/firmware/ARCHITECTURE.md`, and any affected subsystem doc.
- If you add or rename HTTP routes in `main/src/web_server.cpp`, review `../docs/firmware/web-ui.md`.
- If you change NVS blob layout or defaults, review `../docs/firmware/nvs.md`, `../docs/firmware/configuration-reference.md`, and any matching ADR.
- If you add a sensor or transport option, review `../docs/firmware/sensors/README.md`, `../docs/firmware/sensors/supported-sensors.md`, `../docs/firmware/sensors/adding-new-sensor.md`, and `../docs/firmware/transport-binding.md`.
- If you change queue, batching, or backend semantics, review `../docs/firmware/measurement-pipeline.md`, `../docs/firmware/upload-adapters.md`, and `../docs/firmware/upload-transport.md`.

## Verification checklist

Before closing firmware work, do the smallest applicable set:

1. Re-read the affected subsystem docs and make them match the code.
2. Run the canonical firmware build if the change touched C++ or build config.
3. Run `python3 ../scripts/check_firmware_docs.py`.
4. If the change introduced a new document or link, confirm the doc checker stays clean.

## Documentation conventions

- Prefer implementation docs in `../docs/firmware/` over repository-level planning docs.
- Use the standard header shape from [`../docs/firmware/doc-template.md`](../docs/firmware/doc-template.md).
- Keep “what to read next” links concrete.
- Link to code paths, not generated build outputs.
