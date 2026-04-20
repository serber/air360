# Web Server Handler Split ADR

## Status

Partially implemented.

## Decision Summary

Split `web_server.cpp` and `status_service.cpp` into per-route handler files and a separate rendering layer to eliminate God-object compilation units.

Phase 7 has started the incremental split: read-only/runtime routes moved to `main/src/web/web_runtime_routes.cpp`, mutating config/sensor/backend routes moved to `main/src/web/web_mutating_routes.cpp`, and shared HTTP body/form helpers moved to `main/src/web/web_server_helpers.cpp`. `web_server.cpp` still owns URI registration and page rendering helpers.

## Context

Before Phase 7, `web_server.cpp` contained all HTTP route handler implementations in a single translation unit. `status_service.cpp` combines runtime state aggregation with full HTML and JSON rendering. Both files are among the largest in the firmware.

The practical consequences:

- Any change to a single route or any UI tweak forces a full recompile of the entire 79 KB / 36 KB translation unit — a significant part of incremental build time on ESP-IDF.
- All route handlers share the same `#include` graph, so changing one route's dependencies invalidates the whole file.
- The files are too large for effective code review and navigation.
- `StatusService` does two unrelated things: it tracks runtime state (updated from multiple tasks) and it renders that state into HTML/JSON (called only from the web server task). These concerns have different change rates and different threading requirements.

## Goals

- Reduce incremental build time for route-level changes.
- Make each route handler independently reviewable.
- Separate state aggregation from rendering in `StatusService`.
- Keep the public API of `WebServer` and `StatusService` unchanged at the call sites in `app.cpp`.

## Non-Goals

- Changing the HTTP framework (`esp_http_server` stays).
- Changing the rendered HTML/JSON structure.
- Runtime performance changes.

## Architectural Decision

### 1. Split `web_server.cpp` by route

Create one `.cpp` file per logical route group under `main/src/web/`:

| File | Routes |
|------|--------|
| `web_handler_root.cpp` | `GET /` |
| `web_handler_status.cpp` | `GET /status` |
| `web_handler_config.cpp` | `GET /config`, `POST /config` |
| `web_handler_sensors.cpp` | `GET /sensors`, `POST /sensors` |
| `web_handler_backends.cpp` | `GET /backends`, `POST /backends` |
| `web_handler_wifi.cpp` | `GET /wifi-scan` |
| `web_handler_assets.cpp` | `GET /assets/*` |

`web_server.cpp` becomes a thin registration file: constructs the server, calls `esp_http_server_register_uri()` for each handler, passes dependencies. Each handler file declares its registration function and implements only its own handler(s).

Current incremental implementation uses `web_runtime_routes.cpp` for `/`, `/diagnostics`, `/logs/data`, `/assets/*`, `/wifi-scan`, and `/check-sntp`, and `web_mutating_routes.cpp` for `/config`, `/sensors`, and `/backends`. Page rendering helpers remain in `web_server.cpp`; a later step can move them to a dedicated renderer file without touching persistence flows.

### 2. Split `status_service.cpp` into aggregator + renderer

- `status_service.cpp` — keeps only state fields, setters, and getters. No string building, no HTML, no JSON.
- `status_renderer.cpp` — takes a `StatusSnapshot` (a plain struct copy of all state) and produces JSON or HTML. Called only from web handler files.

`StatusSnapshot` is a value type — a full copy of all status fields taken under a short lock. The renderer operates on the snapshot with no locks held.

### 3. Update `CMakeLists.txt`

Add all new `.cpp` files to `idf_component_register(SRCS ...)` in `main/CMakeLists.txt`.

## Affected Files

- `firmware/main/src/web_server.cpp` — target end state is registration-only; current state keeps registration plus page rendering helpers
- `firmware/main/src/status_service.cpp` — remove rendering logic
- `firmware/main/src/web/web_runtime_routes.cpp` — implemented first route group for read-only/runtime endpoints
- `firmware/main/src/web/web_mutating_routes.cpp` — implemented route group for config, sensor, and backend mutation flows
- `firmware/main/src/web/web_server_helpers.cpp` — implemented shared request body, form parsing, and chunked HTML helpers
- `firmware/main/src/web/web_handler_*.cpp` — future files for remaining route groups
- `firmware/main/src/web/status_renderer.cpp` — new file
- `firmware/main/include/air360/web/` — new headers for handler registration functions
- `firmware/main/CMakeLists.txt` — add new sources

## Alternatives Considered

### Option A. Keep current structure

Acceptable for a small team. Compile time and review friction grow linearly with the number of routes added.

### Option B. Route split only, keep StatusService monolithic

Partial improvement. Still recompiles the entire rendering layer on any status field change.

### Option C. Full split (accepted)

Higher upfront effort, permanent improvement to build times and code navigation.

## Practical Conclusion

Split by route group and separate aggregation from rendering. The refactor touches many files but all changes are mechanical — no behavioral change. Can be done incrementally: start with the StatusService split, then move route handlers one at a time.
