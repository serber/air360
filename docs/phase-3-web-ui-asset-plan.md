# Phase 3 Web UI Asset Plan

## Implementation Update

The first slice of this plan is now partially implemented in `firmware/`:

- shared CSS, JavaScript, and page templates live under [`../firmware/main/webui/`](../firmware/main/webui/)
- those assets are embedded directly into the firmware image through ESP-IDF `EMBED_TXTFILES`
- [`../firmware/main/src/web_assets.cpp`](../firmware/main/src/web_assets.cpp) serves them through `/assets/*`
- [`../firmware/main/src/web_ui.cpp`](../firmware/main/src/web_ui.cpp) provides a shared shell and template expansion for server-rendered pages

What is not implemented yet:

- a separate frontend build pipeline
- a `dist/` output directory
- a pure app-shell or SPA model
- `/api/...` routes for config and sensors

This document remains a planning note for the longer migration, but the repository has already moved away from repeated inline `<style>` blocks and toward embedded standalone assets.

## Purpose

This document proposes how to move the Air360 web UI out of C++ string literals and into standalone frontend assets that are easier to edit, style, and extend.

The main goal is to make the UI practical for:

- richer HTML and CSS
- JavaScript-driven forms
- reusable frontend libraries
- future sensor configuration screens
- future upload and status flows

## Current Problem

The current implementation builds pages directly in C++ inside [`firmware/main/src/web_server.cpp`](../firmware/main/src/web_server.cpp).

That is acceptable for a minimal bootstrap UI, but it becomes expensive very quickly:

- HTML is hard to read and maintain
- styling changes require C++ edits
- JavaScript-heavy UI is awkward to manage
- adding frontend libraries becomes painful
- backend and frontend concerns are tightly mixed

For the next phases, especially sensor configuration, this is the wrong long-term shape.

## Recommended Direction

The recommended approach is:

- keep the backend in ESP-IDF C++
- move the frontend into standalone source files
- build the frontend into static assets
- embed those built assets into the firmware image
- let the web server serve the embedded files

This keeps deployment simple while making the UI much easier to evolve.

## Recommended Architecture

### Backend

The firmware backend should remain responsible for:

- device boot
- persistence
- sensor runtime
- network mode
- JSON APIs
- static file serving

### Frontend

The frontend should become a separate buildable unit under `firmware/main/webui/`.

That frontend should be responsible for:

- page layout
- form rendering
- UI state
- client-side validation where useful
- calling JSON endpoints
- progressive enhancement of the local setup UI

## Preferred Delivery Model

The preferred model for Air360 is:

1. Keep shared frontend assets as standalone files under `firmware/main/webui/`
2. Embed those assets into the firmware binary at build time
3. Serve the embedded files from `WebServer`

This is the best immediate fit for the project because it avoids runtime filesystem complexity.

## Why Embedded Assets Are the Best First Step

This repository already has a `storage` partition in [`firmware/partitions.csv`](../firmware/partitions.csv), so a filesystem-backed UI is technically possible. However, that should not be the first implementation target.

Embedding assets is simpler because:

- there is no need to mount SPIFFS at boot
- there is no second flashing workflow for the UI
- firmware and UI stay version-aligned
- deployment remains a single flashable image
- backend routing stays straightforward

For Phase 3 and early Phase 4 work, that simplicity is more valuable than runtime-editable assets.

## Alternative: Filesystem-Hosted UI

The main alternative is to place HTML, CSS, and JavaScript into a flash filesystem such as SPIFFS and serve them from there.

That approach has real advantages:

- UI files are real files at runtime
- the asset image can potentially be updated separately
- large bundles do not increase `.rodata` in the application binary

But it also adds complexity:

- filesystem mount logic
- filesystem image generation
- separate flashing or combined packaging rules
- more boot failure modes
- more moving parts while the UI is still evolving rapidly

Because of that, the recommendation for now is:

- use embedded assets first
- revisit filesystem hosting later only if there is a clear reason

## Proposed Repository Layout

Recommended structure:

- `firmware/main/webui/`
- `firmware/main/src/web_server.cpp`
- `firmware/main/src/web_assets.cpp`
- `firmware/main/src/web_ui.cpp`
- `firmware/main/include/air360/web_assets.hpp`
- `firmware/main/include/air360/web_ui.hpp`

Suggested purpose of each area:

- `firmware/main/webui/`
  - hand-authored shared frontend files embedded as-is
  - currently CSS, progressive-enhancement JavaScript, and page body templates
- `firmware/main/src/web_assets.cpp`
  - embedded asset lookup and content-type mapping
- `firmware/main/src/web_ui.cpp`
  - shared page shell, embedded HTML template expansion, navigation, and notice rendering used by server-rendered routes
- `firmware/main/src/web_server.cpp`
  - HTTP routes for API and static asset delivery

## Suggested Frontend Model

The frontend should move toward a small single-page application or a lightly enhanced multi-page app that consumes JSON endpoints.

Either of these is acceptable:

- a simple vanilla JS app with a lightweight build tool
- a small framework-based app if the bundle size stays reasonable

The deciding factor should be complexity, not fashion.

For this firmware, a small and disciplined setup is preferable:

- minimal bundler
- no heavy runtime unless it solves a real problem
- keep the resulting asset footprint modest

## Recommended Build Approach

The cleanest current path is:

1. Keep shared UI assets inside `firmware/main/webui/`
2. Reference those files from ESP-IDF CMake using embedded asset support
3. Serve the embedded files through generic asset routes
4. Add a richer build step only when the UI actually outgrows hand-authored assets

### CMake Integration

At the firmware level, the frontend build should integrate with ESP-IDF in one of two ways.

#### Option A: Commit `dist/`

Workflow:

- frontend is built separately
- generated `dist/` files are committed
- ESP-IDF embeds those files directly

Pros:

- simple
- no Node.js dependency during firmware build
- easiest to make reproducible on different machines

Cons:

- generated files live in git
- contributors must remember to rebuild `dist/` when frontend source changes

#### Option B: Build `dist/` as part of firmware build

Workflow:

- CMake invokes the frontend build tool
- `dist/` is regenerated during firmware build
- generated files are embedded immediately

Pros:

- always fresh
- fewer manual steps

Cons:

- pulls web toolchain requirements into firmware build
- more brittle on clean machines
- less friendly for contributors who only want to build firmware

### Recommendation

For this repository, the recommended current step is:

- keep frontend source under `firmware/main/webui/`
- embed those source assets directly
- avoid a generated `dist/` until a real bundling need appears

If the UI later grows into modules, templating, or vendor libraries, revisit a generated `dist/` then.

## Serving Model

The backend should stop generating full HTML documents inline and instead serve:

- `GET /`
  - `index.html`
- `GET /assets/...`
  - CSS, JS, images, fonts
- `GET /config`
  - either redirect to the frontend route or serve the same app shell
- `GET /sensors`
  - same app shell or a dedicated page asset
- `GET /status`
  - JSON
- future API routes under something explicit such as `/api/...`

That creates a cleaner separation:

- static assets are static
- JSON data comes from API endpoints
- frontend decides how to render the data

## Recommended Routing Shape

To keep the backend generic, the server should distinguish between:

- page shell routes
- static asset routes
- API routes

Recommended shape:

- `/`
- `/config`
- `/sensors`
- `/assets/*`
- `/api/status`
- `/api/config`
- `/api/sensors`

The current `/status` JSON route can remain temporarily for backward compatibility, but the cleaner long-term shape is under `/api/`.

## Minimal Backend Refactor

The initial backend refactor should stay small.

### Step 1

Introduce an embedded asset module, for example:

- `web_assets.hpp`
- `web_assets.cpp`

It should provide:

- asset lookup by path
- content-type resolution
- optional cache policy helpers

### Step 2

Update `WebServer` so it can:

- serve `index.html`
- serve asset files such as `.js`, `.css`, `.svg`
- keep existing JSON/status logic

### Step 3

Move page-specific forms out of C++ and into the frontend.

At that point:

- `POST /config` can stay temporarily
- `/api/config` can be added
- `/api/sensors` can become the primary structured interface

## Frontend/Backend Contract

Once the UI is externalized, the backend should expose stable data contracts instead of HTML fragments.

At minimum, the frontend will need:

- current device config
- current network state
- configured sensors
- sensor runtime state
- save/update/delete endpoints for config and sensors

That means the next UI iteration should prefer:

- `GET /api/status`
- `GET /api/config`
- `POST /api/config`
- `GET /api/sensors`
- `POST /api/sensors`
- `POST /api/sensors/<id>`
- `POST /api/sensors/<id>/delete`

The existing form-style routes can remain during migration, but they should stop being the long-term UI contract.

## Caching Strategy

For a local device UI, caching should stay conservative at first.

Recommended initial policy:

- page routes such as `/`, `/config`, `/sensors`, and `/backends`: `Cache-Control: no-store`
- shared JS/CSS assets: longer cache headers are acceptable once filenames are stable

Later, if asset filenames become content-hashed, JS and CSS can move to stronger caching.

## Compression

If asset size becomes a concern, the embedded assets can be precompressed.

Possible later optimization:

- generate `.gz` versions of assets
- embed compressed files
- serve them with `Content-Encoding: gzip`

That is a valid later optimization, but it should not block the first refactor.

## JavaScript Libraries

Yes, this architecture allows using JavaScript libraries, but the recommendation is to stay disciplined.

Good use cases:

- form helpers
- lightweight state management
- validation helpers
- small UI components

Poor use cases:

- large frameworks with no strong payoff
- heavy design systems that bloat the bundle
- dependencies that complicate reproducible local builds

The firmware UI is not a general-purpose web app. The frontend stack should remain small and intentional.

## Migration Plan

### Stage 1

- keep current backend routes working
- add embedded static asset support
- add a shared shell for server-rendered pages

### Stage 2

- move `/config` UI into frontend assets
- keep backend save endpoint unchanged if that speeds migration

### Stage 3

- move `/sensors` UI into frontend assets
- consume structured sensor data from JSON endpoints

### Stage 4

- clean up old inline HTML generation
- reduce `web_server.cpp` to routing, API, and static serving

## Recommended First Implementation Slice

The first implementation slice should do only the following:

1. Create `firmware/main/webui/` for shared CSS, JavaScript, and HTML templates.
2. Add CMake support to embed those assets directly.
3. Introduce a generic static asset serving layer.
4. Move the repeated page shell out of per-route inline styling.
5. Keep existing `/config`, `/sensors`, `/backends`, and `/status` routes working for now.

That keeps risk low while creating the foundation for a full UI migration.

## Explicit Recommendation

For Air360, the recommended model is:

- frontend source lives outside C++
- built assets are embedded into the firmware image
- `WebServer` serves those assets and JSON APIs
- the current inline HTML pages are migrated gradually

This is the best tradeoff between:

- editability
- frontend richness
- firmware deployment simplicity
- future maintainability

## Non-Goals for the First Refactor

The first UI asset refactor should not try to solve everything at once.

It should not immediately require:

- SPIFFS hosting
- OTA-delivered frontend assets
- a large frontend framework
- a complex asset pipeline
- full replacement of every existing route in one pass

The goal is simply to stop treating HTML as C++ source code and create a clean path for the richer UI that later phases will need.
