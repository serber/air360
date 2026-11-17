# OTA Firmware Update

## Status

Implemented. Keep this document aligned with `OtaService` and the OTA web routes.

## Scope

This document covers the implemented over-the-air firmware update flow: the streaming write session, the HTTP endpoints that drive it, the deferred reboot, and the rollback / first-boot confirmation behavior. For the design rationale see the [OTA ADR](adr/implemented-ota-firmware-update-adr.md).

## Source of truth in code

- `firmware/main/src/ota_service.cpp` / `firmware/main/include/air360/ota_service.hpp`
- `firmware/main/src/web/web_ota_routes.cpp`

## Read next

- [adr/implemented-ota-firmware-update-adr.md](adr/implemented-ota-firmware-update-adr.md) — decision record and partition rationale
- [web-ui.md](web-ui.md) — full HTTP route reference
- [startup-pipeline.md](startup-pipeline.md) — where `confirmRunningImage()` runs during boot

## Overview

`OtaService` is a thin, mutex-protected wrapper over the ESP-IDF `app_update` API. It owns a single OTA write session at a time so the streaming HTTP handler and the status renderer can call `snapshot()` without racing the writer. Firmware images are written to the inactive OTA slot (`ota_0` / `ota_1`), then the bootloader switches slots on the scheduled reboot.

## HTTP endpoints

All OTA routes are registered by `web_ota_routes.cpp`:

| Route | Behavior |
|-------|----------|
| `POST /ota` | Streams a firmware image to the next OTA slot, then schedules a reboot. Requires a positive `Content-Length`. |
| `GET /ota/status` | Returns the current `OtaStatus` as JSON (state, bytes written, slot labels, versions, rollback flag, error). |
| `POST /ota/rollback` | Marks the running image invalid and reboots into the previous slot. Requires the query string `confirm=yes`. |

The `POST /ota` handler reads the body in `kOtaChunkBytes` (4096-byte) chunks and writes each chunk via `OtaService::writeChunk`. Limits and failure responses:

- Missing/zero `Content-Length` → `400 Bad Request`.
- Payload larger than `kMaxOtaPayloadBytes` (4 MB cap; slots are 6 MB) → `413 Payload Too Large`.
- A transfer already in progress, or an update already committed and waiting for its deferred reboot (`begin()` returns `ESP_ERR_INVALID_STATE`) → `409 Conflict`. Without the second case, `esp_ota_get_next_update_partition()` would pick the just-committed slot again and a second upload could overwrite it.
- `begin()` failing for other reasons → `503 Service Unavailable`.
- More than `kMaxOtaRecvTimeouts` (5) consecutive socket timeouts, a client disconnect, or a write/commit failure → `500 Internal Server Error`; the transfer is aborted and the slot left untouched.

All responses are JSON status snapshots with `Cache-Control: no-store`.

## Write session lifecycle

1. `begin(content_length)` selects the next update partition and starts `esp_ota_begin`. `content_length` is informational (drives the progress UI) and may be 0.
2. `writeChunk(data, size)` streams each received chunk to the target slot. On error it calls `esp_ota_abort` internally and records the error in `OtaStatus`.
3. `commitAndScheduleReboot()` runs `esp_ota_end`, validates the image header, calls `esp_ota_set_boot_partition`, and schedules an `esp_restart()` ~1.5 s later so the HTTP handler can flush its response first. It also sets `reboot_pending_`, which is never cleared: until the restart, `begin()` and `requestRollback()` return `ESP_ERR_INVALID_STATE` and `abortTransfer()` is a no-op, so the first upload's success status stays intact.
4. `abortTransfer(reason)` cancels an active transfer from any state (except after a commit, see above).

## Rollback and first-boot confirmation

- After a fresh OTA, the new image boots in `ESP_OTA_IMG_PENDING_VERIFY`. During startup `OtaService::confirmRunningImage()` is called once the system reaches a known-good state (WebServer + managers running) and invokes `esp_ota_mark_app_valid_cancel_rollback()`, so future reboots keep the slot. If the new image had crashed before this point, the bootloader would have already rolled back automatically. See [startup-pipeline.md](startup-pipeline.md).
- `POST /ota/rollback?confirm=yes` calls `requestRollback()`, which marks the running image invalid and reboots into the previous slot. It only returns on failure (success reboots the device). While a transfer is running or a committed update is waiting for its reboot it returns `ESP_ERR_INVALID_STATE` → `409 Conflict`; other failures (e.g. the image is already committed) yield `500`. Both carry an error JSON body.

## Log tag

`air360.ota`
