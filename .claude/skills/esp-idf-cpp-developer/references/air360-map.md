# Air360 firmware map

Full file-by-file description: `docs/firmware/PROJECT_STRUCTURE.md`. This is the short
version for "where do I edit".

```
firmware/
├── CMakeLists.txt, sdkconfig.defaults, sdkconfig, partitions.csv
├── CLAUDE.md                       firmware-local working contract (read first)
├── main/
│   ├── CMakeLists.txt              SRCS list (add every new .cpp here), EMBED_TXTFILES, REQUIRES
│   ├── Kconfig.projbuild           CONFIG_AIR360_* build-time defaults
│   ├── include/air360/             one header per module; sensors/, uploads/, data/, network/, platform/
│   ├── src/
│   │   ├── app_main.cpp, app.cpp   App::run() boot orchestration, LED, TWDT, maintenance loop
│   │   ├── platform/               PlatformLayer: BuildInfo, ConfigRepository/DeviceConfig, API secret
│   │   ├── network/                NetworkLayer: cellular + Wi-Fi boot steps
│   │   ├── data/                   DataLayer: sensor config/manager, measurement store, BLE, uploads
│   │   ├── power_gate.cpp          boot-time INA voltage gate + deep sleep
│   │   ├── network_manager.cpp     Wi-Fi station / setup AP / reconnect / SNTP / mDNS / scan
│   │   ├── cellular_manager.cpp    PPP modem lifecycle, backoff, PWRKEY escalation
│   │   ├── config_repository.cpp   DeviceConfig NVS blob, defaults, validation
│   │   ├── status_service.cpp      Overview/Diagnostics HTML + raw status JSON
│   │   ├── web_server.cpp          httpd setup, view models, form validators, page renderers
│   │   ├── web/                    route handlers: runtime, mutating (/config /sensors /backends), OTA
│   │   ├── sensors/                sensor_manager, sensor_registry (descriptors), config repo, drivers/
│   │   └── uploads/                measurement_store, upload_manager, backend config, adapters/
│   ├── webui/                      air360.css, air360.js, page_*.html templates ({{BINDING}})
│   └── third_party/sps30/
├── managed_components/             component-manager deps (esp_modem, esp-idf-lib drivers, led_strip, …)
└── test/host/                      CMake/CTest host tests (web form, request body, store, prune policy, …)
```

## Where a concern lives

| Concern | Code | Doc |
|---------|------|-----|
| Boot order, step numbering | `app.cpp`, `data/data_layer.cpp`, `network/network_layer.cpp`, `platform/platform_layer.cpp` | `startup-pipeline.md`, `ARCHITECTURE.md` |
| Device settings field | `include/air360/config_repository.hpp`, `config_repository.cpp`, `web_server.cpp`, `web/web_mutating_routes.cpp`, `webui/page_config.html` | `nvs.md`, `configuration-reference.md`, `web-ui.md` |
| Cellular settings | `cellular_config_repository.cpp`, same web files | `configuration-reference.md`, `cellular-manager.md` |
| Sensor type / driver | `sensors/sensor_registry.cpp`, `sensors/drivers/*`, `sensor_types.hpp` | `sensors/adding-new-sensor.md`, `sensors/<driver>.md`, `sensors/README.md` |
| Sensor scheduling, warmup, states | `sensors/sensor_manager.cpp` | `measurement-pipeline.md`, `ARCHITECTURE.md` |
| Wi-Fi join / recovery | `network_manager.cpp` | `network-manager.md` |
| Status JSON / Overview rows | `status_service.cpp` (`renderStatusJson`, `renderConnectionBlock`) | `web-ui.md` |
| Uploads / backends | `uploads/*` | `measurement-pipeline.md`, `upload-adapters.md` |
| Kconfig tunables | `main/Kconfig.projbuild`, `include/air360/tuning.hpp` | `configuration-reference.md` |
| Host-testable logic | `web/web_form.cpp`, `uploads/upload_prune_policy.cpp`, `measurement_store.cpp` + `test/host/` | `PROJECT_STRUCTURE.md` |

## Boot order (12 steps)

1 watchdog · 2 NVS · 3 netif/event loop · 4 device config · 5 sensor config + sensor task
(only `kBeforeNetwork` sensors run) · 6 backend config · 7 power gate (may deep-sleep) ·
8 cellular · 9 Wi-Fi / setup AP · 10 release after-network sensors + BLE · 11 uploads ·
12 web server. Details: `docs/firmware/startup-pipeline.md`.

## Long-lived tasks

`app_main` (maintenance loop, TWDT 30 s), `air360_sensor`, `air360_upload`,
`air360_net` (Wi-Fi worker), `cellular`, `air360_ble`, `httpd`. Table with stacks and
priorities: `docs/firmware/startup-pipeline.md` → "Long-lived tasks after boot".
