# OTA Firmware Update ADR

## Status

Proposed.

## Decision Summary

Add over-the-air firmware update support via the device web UI, using ESP-IDF's native OTA API and the `otadata` partition already present in the flash layout.

## Context

The current firmware has no remote update mechanism. Every firmware change requires physical access to the device with a USB cable and the `idf.py flash` toolchain. The flash partition layout already includes:

| Partition | Offset | Size | Purpose |
|-----------|--------|------|---------|
| otadata | 0xf000 | 8 KB | OTA state (boot slot selection) |
| factory | 0x20000 | 1 536 KB | Current single application slot |

The `factory` partition is the only application slot currently. To enable OTA, the partition table must be extended to include a second OTA slot (`ota_0` and `ota_1`), replacing the single `factory` slot.

For beta deployments and field installations, requiring physical access for firmware updates is a significant operational burden.

## Goals

- Enable firmware update from the device web UI without physical access.
- Use ESP-IDF's native `esp_ota` API — no third-party OTA library.
- Show upload progress and report success/failure in the UI.
- Preserve the current application on failure (rollback to previous slot).

## Non-Goals

- Pull-based OTA (device polling a remote URL for updates) in the first version.
- Cryptographic signature verification in the first version.
- Delta / differential updates.
- Update via the Air360 backend API (push from server) in the first version.

## Architectural Decision

### 1. Repartition flash

Replace the single `factory` slot with two OTA application slots. Update `partitions.csv`:

```
# Name,   Type, SubType,  Offset,  Size
nvs,      data, nvs,      0x9000,  24K
otadata,  data, ota,      0xf000,  8K
phy_init, data, phy,      0x11000, 4K
ota_0,    app,  ota_0,    0x20000, 1536K
ota_1,    app,  ota_1,    0x1A0000, 1536K
```

Note: the `storage` (SPIFFS) partition is lost in this layout unless flash size allows three slots + SPIFFS. With 16 MB flash, both OTA slots + SPIFFS fit comfortably. Verify sizes before committing.

### 2. New HTTP endpoint: `POST /ota`

The web server receives the firmware binary as a multipart/octet-stream upload:

1. Validate that a valid OTA partition exists via `esp_ota_get_next_update_partition()`.
2. Begin write: `esp_ota_begin(partition, OTA_WITH_SEQUENTIAL_WRITES, &handle)`.
3. Stream incoming HTTP body chunks to `esp_ota_write(handle, data, len)`.
4. On completion: `esp_ota_end(handle)`, then `esp_ota_set_boot_partition(partition)`.
5. Respond 200 OK, then call `esp_restart()` after a short delay.

On any error: `esp_ota_abort(handle)`, respond with error JSON. The previous boot partition is unchanged.

### 3. Progress feedback

Add `GET /ota/status` returning JSON with:
- `state`: `idle` / `in_progress` / `success` / `error`
- `bytes_written`: current byte count
- `error`: error message if failed

The web UI polls this endpoint during upload to show a progress indicator.

### 4. Rollback

ESP-IDF marks the new partition as `ESP_OTA_IMG_PENDING_VERIFY` after `esp_ota_set_boot_partition()`. Add a call to `esp_ota_mark_app_valid_cancel_rollback()` early in `App::run()` after basic initialization succeeds (after step 3, before step 4). If the new firmware crashes before reaching that point, the bootloader rolls back to the previous slot automatically.

### 5. UI addition

Add an "Update Firmware" section to the `/config` or a new `/ota` page with:
- Current firmware version (already in `BuildInfo`)
- File picker for `.bin` upload
- Upload progress bar
- Reboot confirmation on success

## Affected Files

- `firmware/partitions.csv` — add `ota_0`, `ota_1`, remove `factory`
- `firmware/main/src/app.cpp` — add `esp_ota_mark_app_valid_cancel_rollback()` after step 3
- `firmware/main/src/web_server.cpp` — register `/ota` POST handler and `/ota/status` GET handler
- `firmware/main/src/web/web_handler_ota.cpp` — new file: OTA handler implementation
- `firmware/main/webui/` — OTA upload page or section
- `firmware/main/CMakeLists.txt` — add `esp_app_format` and `app_update` to REQUIRES

## Alternatives Considered

### Option A. No OTA

Current state. Requires physical access for every update.

### Option B. Push OTA from Air360 backend

The backend notifies the device to pull a firmware URL. More powerful but requires backend infrastructure that does not yet exist.

### Option C. Web UI upload (accepted)

Self-contained, no backend dependency, uses native ESP-IDF OTA API. The simplest path to remote updates.

## Practical Conclusion

Repartition flash to dual OTA slots, add `POST /ota` endpoint with streaming write and rollback support. Mark the app valid early in `App::run()` to enable automatic rollback on crash. The partition change is the highest-risk part and must be verified against the 16 MB flash size before implementation.
