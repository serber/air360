# OTA Firmware Update ADR

## Status

Implemented.

## Decision Summary

Over-the-air firmware update support is available through the device web UI, using ESP-IDF's native OTA API and the `otadata`, `ota_0`, and `ota_1` partitions in the flash layout.

## Context

Prior to this change, every firmware change required physical access to the device with a USB cable and the `idf.py flash` toolchain. The flash partition layout already included:

| Partition | Offset | Size | Purpose |
|-----------|--------|------|---------|
| otadata | 0xf000 | 8 KB | OTA state (boot slot selection) |
| ota_0 | 0x20000 | 6 MB | Primary application slot |
| ota_1 | 0x620000 | 6 MB | Secondary application slot |
| storage | 0xc20000 | 3 MB | Reserved SPIFFS storage |

The partition table was already OTA-ready; this change adds the runtime that performs firmware upload, boot partition switching, and rollback marking.

For beta deployments and field installations, requiring physical access for firmware updates was a significant operational burden.

## Goals

- Enable firmware update from the device web UI without physical access.
- Use ESP-IDF's native `esp_ota` API — no third-party OTA library.
- Show upload progress and report success/failure in the UI.
- Preserve the current application on failure (rollback to previous slot).

## Non-Goals

- Pull-based OTA (device polling a remote URL for updates) in this version.
- Cryptographic signature verification in this version.
- Delta / differential updates.
- Update via the Air360 backend API (push from server) in this version.

## Architectural Decision

### 1. Partition precondition

The project uses a 16 MB flash layout with two OTA application slots and a reserved SPIFFS partition:

```
# Name,   Type, SubType,  Offset,  Size
nvs,      data, nvs,      0x9000,  24K
otadata,  data, ota,      0xf000,  8K
phy_init, data, phy,      0x11000, 4K
ota_0,    app,  ota_0,    0x20000,  0x600000
ota_1,    app,  ota_1,    0x620000, 0x600000
storage,  data, spiffs,   0xc20000, 0x300000
```

Each OTA slot is 6 MB, leaving enough space for current firmware growth while preserving a 3 MB reserved SPIFFS partition.

### 2. HTTP endpoints

`POST /ota` accepts the firmware binary as a raw `application/octet-stream` body, streamed by `OtaService`:

1. Validate that an inactive OTA partition exists via `esp_ota_get_next_update_partition()`.
2. Begin write: `esp_ota_begin(partition, OTA_WITH_SEQUENTIAL_WRITES, &handle)`.
3. Stream incoming HTTP body chunks (4 KB each) to `esp_ota_write(handle, data, len)`.
4. On completion: `esp_ota_end(handle)`; read `esp_ota_get_partition_description(part, &desc)` and reject any image whose `project_name` does not match `air360_firmware`; then `esp_ota_set_boot_partition(partition)`.
5. Respond with JSON `state=success`, then a short-lived `air360_ota_reboot` task calls `esp_restart()` after ~1.5 s so the response can flush.

On any error: `esp_ota_abort(handle)`, respond with JSON `state=error` and an `error` string. The previous boot partition is unchanged.

`GET /ota/status` returns the current status as JSON:
- `state`: `idle` / `in_progress` / `success` / `error`
- `bytes_written`, `content_length`: progress counters
- `running_version`, `running_slot`, `target_slot`, `target_slot_size`, `pending_version`, `rollback_armed`
- `error`: last error string if `state == error`

`POST /ota/rollback?confirm=yes` calls `esp_ota_mark_app_invalid_rollback_and_reboot()` to force a manual rollback to the previous slot. The endpoint requires the explicit confirm query so it cannot be triggered by accident.

### 3. Progress feedback

Progress is rendered locally in the browser using `XMLHttpRequest.upload.onprogress` rather than polling `/ota/status`. The device runs a single `esp_http_server` task; while it is streaming the body, concurrent GET requests would queue behind the upload and time out, so the browser-side progress avoids any extra device load. `GET /ota/status` is still served (page load + diagnostics + future automation), but the Device page never polls it during an upload.

### 4. Rollback

`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` is enabled in `sdkconfig.defaults`. After `esp_ota_set_boot_partition()` the new image is marked `ESP_OTA_IMG_PENDING_VERIFY`. `App::run()` calls `OtaService::confirmRunningImage()` after `indicateReady()` — only after every manager and the HTTP server have reached steady state. That call invokes `esp_ota_mark_app_valid_cancel_rollback()` when the image is in `PENDING_VERIFY`. If the new firmware crashes before reaching that point, the bootloader rolls back automatically.

### 5. UI

The "Firmware update" card is part of the Device page (`/config`) and contains:
- Card subtitle showing the running version, the active OTA slot, and the inactive (target) slot with its capacity.
- A "pending verification" banner shown only when the running image is still in `ESP_OTA_IMG_PENDING_VERIFY`.
- File picker (`<input type='file'>`) and an "Upload and install" button.
- Progress bar plus textual status, driven by `XMLHttpRequest.upload.onprogress` on the client side; the device is not polled during the upload.
- The card lives on `/config`, which is the only navigable page in setup-AP mode — so an operator can flash recovery firmware on a device whose station credentials are unknown.

## Affected Files

- `firmware/main/include/air360/ota_service.hpp` — new: OTA state machine and snapshot.
- `firmware/main/src/ota_service.cpp` — new: implements `esp_ota_*` calls, mutex, deferred reboot task.
- `firmware/main/include/air360/app.hpp`, `firmware/main/src/app.cpp` — owns `OtaService`, calls `confirmRunningImage()` after `indicateReady()`, passes it into `WebServer::start()`.
- `firmware/main/include/air360/web_server.hpp`, `firmware/main/src/web_server.cpp` — register `POST /ota`, `GET /ota/status`, `POST /ota/rollback`; raised `kHttpServerMaxUriHandlers` from 21 to 24; threaded `OtaStatus` through `renderConfigPage()` and `buildConfigPageViewModel()` so the Firmware update card can render on `/config`.
- `firmware/main/include/air360/web_server_internal.hpp` — `renderConfigPage()` signature now takes `const OtaStatus&`.
- `firmware/main/src/web/web_mutating_routes.cpp` — `handleConfig` passes `server->ota_service_->snapshot()` into `renderConfigPage()`.
- `firmware/main/src/web/web_ota_routes.cpp` — new: HTTP handlers for `POST /ota`, `GET /ota/status`, `POST /ota/rollback`.
- `firmware/main/webui/page_config.html` — Firmware update card appended after the configuration form.
- `firmware/main/webui/air360.js` — OTA submit handler bound via `data-ota-form` (XHR + `upload.onprogress` for progress; no polling of `/ota/status`).
- `firmware/main/CMakeLists.txt` — added `app_update` to REQUIRES; added `src/ota_service.cpp` and `src/web/web_ota_routes.cpp`.
- `firmware/sdkconfig.defaults` — `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`.

## Alternatives Considered

### Option A. No OTA

Prior state. Requires physical access for every update.

### Option B. Push OTA from Air360 backend

The backend notifies the device to pull a firmware URL. More powerful but requires backend infrastructure that does not yet exist.

### Option C. Web UI upload (accepted)

Self-contained, no backend dependency, uses native ESP-IDF OTA API. The simplest path to remote updates.

## Practical Conclusion

`POST /ota` streams raw `.bin` uploads into the inactive OTA slot via `OtaService`, validates `project_name` before activating the new slot, and reboots via a short-lived task so the HTTP response flushes first. `OtaService::confirmRunningImage()` is called after `indicateReady()` in `App::run()` so a freshly-flashed image that fails to reach the steady state is automatically rolled back by the bootloader.
