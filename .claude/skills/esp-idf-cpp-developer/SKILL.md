---
name: esp-idf-cpp-developer
description: "Use for Air360 ESP-IDF firmware development in C++ (ESP32-S3, ESP-IDF 6.x, C++20): sensor drivers, FreeRTOS tasks, Wi-Fi/BLE/cellular features, NVS config fields, web UI form wiring, boot-order changes, CMakeLists or Kconfig updates, partition work, build-error triage, and embedded refactors. Do not use for documentation-only tasks or for backend/portal code."
---

# ESP-IDF C++ Developer (Air360)

Work as an ESP-IDF and embedded C++ engineer inside `firmware/`. The repository has one
component (`main/`), no `components/` directory, and a `-Werror` build. Repository
conventions beat generic ESP-IDF examples every time.

## Read first

1. `firmware/CLAUDE.md` — the canonical working contract: build command, invariants
   (TWDT, timer callbacks), log-tag convention, return-value rule, co-change rules,
   verification checklist. This skill does not repeat it; it adds what is not there.
2. The subsystem doc from the table in `firmware/CLAUDE.md` for the area you touch.
3. `references/air360-map.md` — where each concern lives in `main/`.
4. `references/coding-rules.md` — the `-Werror` traps and FreeRTOS/C++ patterns this
   repository uses.
5. `references/recipes.md` — step-by-step recipes for the recurring change types
   (config field + web form, new boot step, sensor descriptor field, status surfacing,
   new task).

## Workflow

- Inspect the existing code path end to end before editing: header, implementation,
  every call site, the doc that describes it. `grep -rn` across `main/` first.
- Make the smallest coherent patch. Do not mix a refactor into a feature change.
- Keep every intermediate state buildable; build after each logical unit, not once at
  the end.
- Follow the exact patterns already present in the file you are editing (locking
  style, `lock()/unlock()` scope, error propagation, log wording).
- Update the matching `docs/firmware/*.md` in the same change. The doc checker runs as
  part of validation and fails on broken links.

## Build and verification

`idf.py` is NOT on PATH. Use the bundled script from the repository root or from
anywhere inside it:

```bash
bash .claude/skills/esp-idf-cpp-developer/scripts/validate.sh
```

It sources the ESP-IDF environment, runs `idf.py build`, prints only errors and
warnings from `main/`, then runs `scripts/check_style.py`,
`scripts/check_firmware_docs.py`, and the host tests (`firmware/test/host`).
Pass `--build-only` to skip the checkers, `--no-build` to run only the checkers.

The canonical manual invocation, when the script is not appropriate, is exactly:

```bash
source ~/.espressif/v6.0/esp-idf/export.sh && idf.py build
```

run from `firmware/`. Run long builds in the background and read the log afterwards.

## Repository gotchas

- **`-Werror` is on.** A free function defined in a `.cpp` outside an anonymous
  namespace fails with `missing-declarations`; a `float` passed to `printf`-style
  formatting fails with `double-promotion`. See `references/coding-rules.md`.
- **Config blobs have no migration.** Changing a persisted struct changes its size;
  the repository's size check then replaces the stored blob with defaults on the next
  boot. Say so in `nvs.md` and in the change summary. Do not bump the schema version
  for a plain append unless the user asks.
- **Boot steps are numbered `N/M` in four files** (`app.cpp`, `data/data_layer.cpp`,
  `network/network_layer.cpp`, `platform/platform_layer.cpp`) plus their headers and
  the docs (`startup-pipeline.md`, `ARCHITECTURE.md`, `PROJECT_STRUCTURE.md`,
  `web-ui.md`). Renumber all of them together; `grep -rn "Boot step"` finds the code
  sites.
- **Sensor descriptors use designated initializers.** New `SensorDescriptor` fields
  go at the end of the struct with a default, and entries that set them must list
  them after `.create_driver` (and after any maintenance-action fields).
- **The web config page is one big form.** A hidden card still submits its inputs,
  so parse with a fallback to the stored value and validate ranges unconditionally.
- **Deep sleep and RTC memory.** `RTC_DATA_ATTR` values are valid only when
  `esp_reset_reason() == ESP_RST_DEEPSLEEP`; zero them on any other reset.
- **Timer and event callbacks** never block, allocate, or call Wi-Fi APIs; they set
  bits or notify the persistent worker. Arm retry timers before setting the event
  bit that releases a waiting task.

## Definition of done

A change is done when every item in the `firmware/CLAUDE.md` verification checklist is
ticked, `validate.sh` is clean, the diff has been self-reviewed as a reviewer would read
it, and the response lists: files changed, sdkconfig/CMake/partition implications,
runtime risks (stack, heap, timing, watchdog, reconnect behaviour), and what still
needs a hardware check.
