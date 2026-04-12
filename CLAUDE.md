# Air360 — project guidance for Claude Code

## Project overview

Air360 is an air-quality monitoring device. It has three deployable components:

| Component | Language / Stack | Status |
|-----------|-----------------|--------|
| `firmware/` | C++17, ESP-IDF 6.x, FreeRTOS, ESP32-S3 | Feature-complete |
| `backend/` | TypeScript, Fastify 5, Node ≥20 | Early scaffold |
| `portal/` | TypeScript, Next.js 16, React 19, Tailwind 4 | Early scaffold |

Design documents and ADRs live in `docs/`. `firmware/` is the source of truth for implemented behavior; `docs/` is planning and architecture context.

---

## Repository layout

```
firmware/               ESP-IDF application
  main/src/             C++17 application sources (~30 files)
  main/include/air360/  Public headers
  main/webui/           Embedded web-UI assets (HTML/CSS/JS)
  main/third_party/     Vendored C library (SPS30)
  managed_components/   ESP-IDF component manager dependencies
  sdkconfig.defaults    Baseline build configuration
  partitions.csv        Flash layout
backend/
  src/                  Fastify TypeScript sources
portal/
  src/app/              Next.js App Router pages
docs/
  firmware/             Implementation docs + ADRs
  backend/              API contract drafts
  portal/               Deployment guides
  ecosystem/            External integration notes
.agents/skills/         Reusable AI-agent skill definitions
```

---

## Dev commands

### Firmware (ESP-IDF)

```bash
idf.py build                  # compile
idf.py flash                  # flash to connected ESP32-S3
idf.py monitor                # open serial monitor
idf.py flash monitor          # flash then monitor
idf.py size                   # binary size report
idf.py menuconfig             # interactive Kconfig editor
idf.py fullclean              # wipe build directory
```

Target chip: **ESP32-S3** (`idf_target=esp32s3` in sdkconfig.defaults).

### Backend

```bash
cd backend
npm run dev        # tsx watch src/server.ts  (hot-reload)
npm run build      # tsc -p tsconfig.json
npm run start      # node dist/server.js
npm run typecheck  # tsc --noEmit
```

Node.js ≥20.0.0 required.

### Portal

```bash
cd portal
npm run dev        # next dev
npm run build      # next build
npm run start      # next start
npm run lint       # eslint
```

Node.js ≥20.9.0 required.

---

## Tech stack details

### Firmware

- **MCU**: ESP32-S3, 16 MB flash
- **Framework**: ESP-IDF 6.x (no Arduino unless mixed in third_party)
- **Language**: C++17, no exceptions, no RTTI
- **RTOS**: FreeRTOS (tasks, queues, event groups)
- **Storage**: NVS (key-value config), SPIFFS partition (384 KB), OTA data partition
- **Networking**: Wi-Fi station + soft-AP (lab AP SSID: `air360`)
- **Peripherals**: I2C0 (SDA=GPIO8, SCL=GPIO9), UART1 GPS (RX=GPIO18, TX=GPIO17, 9600 baud), 3 configurable GPIO sensor pins
- **HTTP server**: port 80, embedded web UI
- **Upload targets**: Sensor.Community API, Air360 backend API
- **Managed components** (idf_component.yml): bme280, bme680, ds18b20, scd30, sht4x, si7021, veml7700

#### Sensor drivers (firmware/main/src/sensors/drivers/)

BME280, BME680, DHT, DS18B20, GPS NMEA, HTU2X, ME3 (NO2), SCD30 (CO2), SHT4X, SPS30 (particulates), VEML7700 (light), I2C support, Sensirion HAL.

### Backend

- **Runtime**: Node.js ≥20
- **Framework**: Fastify 5
- **Language**: TypeScript 5
- **Key routes**: `GET /health`, `PUT /v1/devices/:chip_id/batches/:client_batch_id`

### Portal

- **Framework**: Next.js 16 (App Router)
- **UI**: React 19, Tailwind CSS 4
- **Language**: TypeScript 5
- **Linting**: ESLint 9 (core-web-vitals + typescript rules)

---

## Firmware documentation

Full implementation documentation lives in [`docs/firmware/`](docs/firmware/README.md). Start with the index:

| Document | What it covers |
|----------|----------------|
| [docs/firmware/README.md](docs/firmware/README.md) | Documentation index and cross-reference map |
| [docs/firmware/ARCHITECTURE.md](docs/firmware/ARCHITECTURE.md) | System overview, task model, data flow, GPIO allocation |
| [docs/firmware/startup-pipeline.md](docs/firmware/startup-pipeline.md) | 9-step boot sequence, long-lived tasks, failure modes |
| [docs/firmware/configuration-reference.md](docs/firmware/configuration-reference.md) | All NVS config fields: defaults, ranges, validation |
| [docs/firmware/network-manager.md](docs/firmware/network-manager.md) | Wi-Fi station / AP modes, SNTP, state transitions |
| [docs/firmware/measurement-pipeline.md](docs/firmware/measurement-pipeline.md) | Sensor polling → queue → upload cycle |
| [docs/firmware/nvs.md](docs/firmware/nvs.md) | NVS storage format and integrity guards |
| [docs/firmware/web-ui.md](docs/firmware/web-ui.md) | HTTP routes, pages, JS behaviour |
| [docs/firmware/time.md](docs/firmware/time.md) | Uptime vs Unix time, SNTP sync, time gates |
| [docs/firmware/sensors/README.md](docs/firmware/sensors/README.md) | Per-driver documentation index |
| [docs/firmware/transport-binding.md](docs/firmware/transport-binding.md) | I2C bus manager, UART port manager |
| [docs/firmware/upload-adapters.md](docs/firmware/upload-adapters.md) | Sensor.Community and Air360 API adapters |
| [docs/firmware/upload-transport.md](docs/firmware/upload-transport.md) | HTTP execution layer (esp_http_client) |

---

## Documentation rules

- `firmware/` is the source of truth for **implemented** behavior.
- `docs/firmware/` explains the current implementation — keep it in sync with the code, not ahead of it.
- `docs/` (root level) is design and planning context; do not treat it as proof of implementation.
- Separate firmware-scope docs (`docs/firmware/`) from repository-scope docs (`docs/`).
- Never document `firmware/build/` as maintained project structure.
- Preserve project-specific terminology; avoid generic ESP-IDF boilerplate.

---

## Firmware coding rules

- Use modern C++ conservatively: `enum class`, `constexpr`, `std::array`, thin wrappers around C APIs.
- **No exceptions. No RTTI.**
- No blocking delays > 50 ms in shared tasks.
- Prefer `esp_err_t` over `bool` for fallible operations.
- Log exclusively through `ESP_LOGI/W/E/D`. Use `ESP_ERROR_CHECK` for fatal errors.
- Use `const` aggressively. Minimize global mutable state.
- Separate hardware abstraction, application logic, and task orchestration.
- Use RAII where helpful; do not fight ESP-IDF C APIs unnecessarily.
- All new FreeRTOS tasks must document: purpose, stack size, priority, and lifecycle.
- Avoid busy loops — use delays, notifications, queues, or event groups.
- Keep ISRs short; use ISR-safe APIs only; defer work to tasks.
- Document timing assumptions, units, pin mappings.

### Adding a new component or source file

1. Place it in the right component (`components/` for reusable, `main/` for app-level).
2. Update `CMakeLists.txt` and `idf_component_register(SRCS ... INCLUDE_DIRS ... REQUIRES ...)`.
3. Check `REQUIRES` / `PRIV_REQUIRES` for every new dependency.
4. Respect `sdkconfig` feature gates; do not hard-code what Kconfig controls.

### Build failure triage order

1. Missing component requirements (`REQUIRES` / `PRIV_REQUIRES`)
2. Bad include paths
3. Source file not added to `CMakeLists.txt`
4. Kconfig / sdkconfig mismatch
5. Target-specific API differences
6. Linker errors from duplicate or undefined symbols

---

## Key firmware source modules

| File | Responsibility |
|------|----------------|
| `app_main.cpp` / `app.cpp` | Entry point and initialization sequence |
| `network_manager.cpp` | Wi-Fi station + soft-AP management |
| `web_server.cpp` | HTTP server, all route handlers (~81 KB) |
| `status_service.cpp` | Device status aggregation (~37 KB) |
| `config_repository.cpp` | NVS-backed device configuration |
| `sensors/sensor_manager.cpp` | Sensor polling and registration |
| `sensors/sensor_registry.cpp` | Sensor factory / catalog |
| `uploads/upload_manager.cpp` | Background upload task |
| `uploads/measurement_store.cpp` | Bounded in-memory measurement queue |
| `uploads/adapters/air360_api_uploader.cpp` | Air360 backend API adapter |
| `uploads/adapters/sensor_community_uploader.cpp` | Sensor.Community API adapter |

---

## Flash partition layout

| Name | Offset | Size | Purpose |
|------|--------|------|---------|
| nvs | 0x9000 | 24 KB | Non-volatile config storage |
| otadata | 0xf000 | 8 KB | OTA state |
| phy_init | 0x11000 | 4 KB | PHY calibration |
| factory | 0x20000 | 1536 KB | Main application |
| storage | 0x1a0000 | 384 KB | SPIFFS data |

---

## What is not present

- No CI/CD workflows (no `.github/`)
- No automated test suite
- No `.prettierrc` or `.clang-format`
- `backend/` and `portal/` are early scaffolds; business logic is minimal
