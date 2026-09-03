# Recipes for recurring Air360 firmware changes

Each recipe lists the files in edit order and the docs that must change with them.
Build after the code steps, run `validate.sh` at the end.

## 1. Add a `DeviceConfig` field with a web form control

1. `include/air360/config_repository.hpp` — append the field at the end of the struct;
   add `constexpr` default/min/max constants next to the other tunables.
2. `config_repository.cpp` — set the default in `makeDefaultDeviceConfig()` (or a
   dedicated `apply…Defaults()`), range-check it in `isValid()` (or a dedicated
   `validate…Config()` that the web layer can reuse).
3. `web_server.cpp` — add the value to `ConfigPageViewModel`, fill it in
   `buildConfigPageViewModel()`, add `{"BINDING", …}` in `renderConfigPage()`, extend
   `validateConfigForm()` (both the implementation and the `web::` wrapper) and the
   declaration in `include/air360/web_server_internal.hpp`.
4. `webui/page_config.html` — add the input inside an existing card or a new
   `<section class='card'>`; use `type='number' min max` for ranges, a `.switch` for
   booleans.
5. `web/web_mutating_routes.cpp` — parse with `findFormValue()` (numbers fall back to
   the stored value), pass to `validateConfigForm()`, copy into both the `preview`
   and the `updated` record.
6. Docs: `nvs.md` (struct listing + table + note that old blobs reset),
   `configuration-reference.md` (field table, validation, notes), `web-ui.md` (form
   table). `docs/guide/device-features.md` if end users see it.

## 2. Add or move a boot step

1. `app.cpp` `App::run()` — insert the call; add a `boot…()` method on `App` or the
   owning facade (`DataLayer`, `NetworkLayer`, `PlatformLayer`).
2. Renumber every `"Boot step N/M"` log line and header comment
   (`grep -rn "Boot step" main/`). Keep the order in the log identical to the call order.
3. Anything that must not run before the radios goes after step 7 (power gate).
4. Docs: `startup-pipeline.md` (overview tree, step table, per-step section, log
   sample, long-lived task table, failure modes), `ARCHITECTURE.md` (step table,
   facade paragraph, "Confirmed in implementation"), `PROJECT_STRUCTURE.md`
   (`app.cpp` line), `web-ui.md` ("server starts during boot step M/M").

## 3. Add a `SensorDescriptor` capability field

1. `include/air360/sensors/sensor_registry.hpp` — add the enum/field at the end of
   `SensorDescriptor` with a default.
2. `sensors/sensor_registry.cpp` — set it only on the entries that need it, placed
   after `.create_driver` in the designated-initializer list.
3. Consume it in `sensors/sensor_manager.cpp` (`buildManagedSensors()` copies what the
   task needs into `ManagedSensor`/`SensorRuntimeInfo`).
4. If it adds a `SensorRuntimeState`: `sensor_types.hpp` (`sensorRuntimeStateKey`),
   `status_service.cpp` (health switch + chip colour switch).
5. Docs: `sensors/adding-new-sensor.md` (descriptor example/notes), `sensors/README.md`,
   the affected `sensors/<driver>.md`, `ARCHITECTURE.md` state table, `web-ui.md`.

## 4. Surface a runtime decision in status JSON and on the Overview page

1. Define a plain struct for the decision (copyable, `std::string` for text) and a
   `…Key()` for its enum.
2. `include/air360/status_service.hpp` — add `set…()` and a member; `status_service.cpp`
   — copy it into `StatusServiceRenderSnapshot` at all three snapshot sites
   (`grep -n "render_snapshot.reset_reason = reset_reason_"` shows them).
3. JSON: a `render…Json()` helper in the anonymous namespace, emitted from
   `renderStatusJson()` next to the related top-level keys.
4. Overview: extend `renderConnectionBlock()` (System card) or the relevant block with a
   `renderTemplate(WebTemplateKey::kSectionRow, …)` row, gated on "enabled".
5. Docs: `web-ui.md` (Overview section + Raw Status JSON keys), the subsystem doc.

## 5. Add a FreeRTOS task

1. Constants: stack size, priority, loop period, each with a *why* comment.
2. Entry: `esp_task_wdt_add(nullptr)`; loop: `esp_task_wdt_reset()` per iteration and
   inside long waits; exit: `esp_task_wdt_delete(nullptr)` then `vTaskDelete(nullptr)`.
3. Cooperative stop: atomic flag + `xTaskNotifyGive`, acknowledged via an event-group
   bit the stopper waits on with a timeout.
4. Docs: `watchdog.md` (task table), `startup-pipeline.md` (long-lived task table, the
   step that spawns it), `ARCHITECTURE.md` (runtime model table).

## 6. Add a Kconfig tunable

1. `main/Kconfig.projbuild` — `config AIR360_…` with `range` and `default`.
2. `include/air360/tuning.hpp` — expose it as a `constexpr` in the matching namespace
   with a comment; consume the constant, never the raw `CONFIG_` macro, in code.
3. `sdkconfig.defaults` only if the default must differ from the Kconfig default.
4. Docs: `configuration-reference.md` (runtime tuning table), the subsystem doc's
   constants table, `firmware/README.md` if user-facing.

## 7. Diagnose a `-Werror` build failure

1. Read the first `error:` line in the build log, ignore `managed_components` noise:
   `grep -n "error\|warning:" build.log | grep -v managed_components | head`.
2. Match it against the table in `coding-rules.md` (`missing-declarations`,
   `double-promotion`, format specifiers) before changing anything else.
3. Rebuild; `idf.py build` is incremental, a fix takes under a minute.
