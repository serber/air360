# Finding F03: Upload task can trip TWDT during multi-request batches

## Status

Confirmed audit finding. Not implemented.

## Scope

Task file for preventing upload-task watchdog starvation during blocking HTTP upload batches.

## Source of truth in code

- `firmware/main/src/uploads/upload_manager.cpp`
- `firmware/main/src/uploads/upload_transport.cpp`
- `firmware/main/src/uploads/adapters/sensor_community_uploader.cpp`

## Read next

- `docs/firmware/watchdog.md`
- `docs/firmware/measurement-pipeline.md`
- `docs/firmware/upload-transport.md`

**Priority:** High
**Category:** FreeRTOS / Reliability
**Files / symbols:** `UploadManager::taskMain`, `UploadTransport::execute`, `SensorCommunityUploader::buildRequests`

## Problem

The upload task is subscribed to the task watchdog, but it can execute multiple blocking HTTP requests back-to-back without feeding the watchdog between requests. `SensorCommunityUploader` can generate one HTTP request per sensor group, and each request has a 15 second timeout.

## Why it matters

Three slow Sensor.Community requests can block around 45 seconds. The configured TWDT budget is 30 seconds. A transient cellular or Wi-Fi outage during backlog replay can therefore reboot the device instead of letting the upload retry policy handle the failure.

## Evidence

- `firmware/main/src/uploads/upload_manager.cpp:165` calls `esp_task_wdt_add(nullptr)`.
- `firmware/main/src/uploads/upload_manager.cpp:371` calls `transport_.execute(request)` inside a loop over `requests`.
- There is no `esp_task_wdt_reset()` before or after each `transport_.execute(request)` call.
- `firmware/main/src/uploads/upload_manager.cpp:539` resets the watchdog only after finishing the backend drain path.
- `firmware/main/src/uploads/adapters/sensor_community_uploader.cpp:257` pushes one `UploadRequestSpec` per compatible sensor group.
- `firmware/main/src/uploads/adapters/sensor_community_uploader.cpp:262` sets `request.timeout_ms = 15000`.

## Recommended Fix

Feed the TWDT before and after each blocking HTTP request, and chunk long waits so no path can exceed the watchdog window. Also cap the number of requests handled in one loop turn or reduce per-request timeout when multiple requests are emitted.

## Where To Change

- `firmware/main/src/uploads/upload_manager.cpp`
- `firmware/main/src/uploads/upload_transport.cpp`
- `firmware/main/include/air360/uploads/upload_transport.hpp`
- `docs/firmware/watchdog.md`
- `docs/firmware/upload-transport.md`
- `docs/firmware/measurement-pipeline.md`

## How To Change

1. Add a small helper such as `resetUploadTaskWatchdog()` in `upload_manager.cpp`.
2. Feed TWDT:
   - before `transport_.execute(request)`
   - immediately after it returns
   - between consecutive Sensor.Community requests
3. Consider returning timing phases from `UploadTransportResponse` if the transport is later converted to open/write/fetch/read so long operations can be chunked.
4. Add a maximum request count per loop turn if needed.
5. Preserve retry semantics; do not acknowledge a window until all generated requests succeed.

## Example Fix

```cpp
for (const auto& request : requests) {
    if (stopRequested()) {
        break;
    }

    esp_task_wdt_reset();
    const UploadTransportResponse response = transport_.execute(request);
    esp_task_wdt_reset();

    const UploadResultClass request_result = uploader->classifyResponse(response);
    // Existing classification and retry logic...
}
```

## Validation

- Add a fake uploader host test or instrumented hardware test that emits three requests with 15 second timeouts.
- Confirm no TWDT reset occurs.
- Confirm failed multi-request batches are not acknowledged.
- Run a Sensor.Community configuration with at least three compatible sensor groups over a blocked endpoint.
- Run `source ~/.espressif/v6.0/esp-idf/export.sh && idf.py build` from `firmware/`.
- Run `python3 scripts/check_firmware_docs.py`.

## Risk Of Change

Low. Feeding TWDT around blocking calls should not change upload semantics.

## Dependencies

None.

## Suggested Agent Type

FreeRTOS agent / ESP-IDF agent
