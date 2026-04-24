# Finding F11: Upload transport logs full backend URLs

## Status

Confirmed audit finding. Not implemented.

## Scope

Task file for redacting sensitive backend URL data from firmware logs.

## Source of truth in code

- `firmware/main/src/uploads/upload_transport.cpp`
- `firmware/main/src/uploads/adapters/custom_upload_uploader.cpp`
- `firmware/main/src/web/web_runtime_routes.cpp`

## Read next

- `docs/firmware/upload-transport.md`
- `docs/firmware/upload-adapters.md`
- `docs/firmware/web-ui.md`

**Priority:** Medium
**Category:** Security / Observability
**Files / symbols:** `UploadTransport::execute`, backend upload adapters

## Problem

`UploadTransport::execute()` logs the full request URL for every upload. Custom backend URLs are user-controlled, and many HTTP integrations carry tokens or identifying information in paths or query strings.

## Why it matters

The firmware exposes logs through `/logs/data`, and the web UI is currently unauthenticated. Full URLs can leak device identifiers, backend hostnames, tenant paths, or query-string tokens. Even after local auth is added, logs should avoid secrets by default.

## Evidence

- `firmware/main/src/uploads/upload_transport.cpp:43` logs:
  - method
  - full `request.url`
  - body length
- `firmware/main/src/uploads/adapters/custom_upload_uploader.cpp:57` builds request URLs from user-configured backend records.
- `firmware/main/src/uploads/adapters/air360_api_uploader.cpp:34` can include `{chip_id}` and `{batch_id}` in the path.
- `firmware/main/src/web/web_runtime_routes.cpp:79` serves log buffer contents at `/logs/data`.

## Recommended Fix

Redact URLs in logs. Log scheme, host, backend key, and body length, but strip query strings and avoid path segments that may carry tokens unless debug logging is explicitly enabled.

## Where To Change

- `firmware/main/src/uploads/upload_transport.cpp`
- `firmware/main/include/air360/uploads/backend_uploader.hpp` if adding a redacted display field
- `firmware/main/src/log_buffer.cpp` if adding log redaction globally
- `docs/firmware/upload-transport.md`
- `docs/firmware/web-ui.md`

## How To Change

1. Add `redactUrlForLog(std::string_view url)`.
2. Strip everything after `?`.
3. Consider replacing path with `/...` unless the backend adapter marks it safe.
4. Do not log authorization headers or request bodies.
5. Gate full URL logging behind a compile-time debug option that is off in production.

## Example Fix

```cpp
ESP_LOGI(
    kTag,
    "HTTP request: method=%s endpoint=%s body_len=%u",
    request.method == UploadMethod::kPut ? "PUT" : "POST",
    redactUrlForLog(request.url).c_str(),
    static_cast<unsigned>(request.body.size()));
```

## Validation

- Unit-test redaction:
  - `https://api.example/a/b?token=secret` logs `https://api.example/...`
  - `http://host/path` does not include query data
- Confirm `/logs/data` does not expose tokens or full custom paths.
- Run `source ~/.espressif/v6.0/esp-idf/export.sh && idf.py build` from `firmware/`.
- Run `python3 scripts/check_firmware_docs.py`.

## Risk Of Change

Low.

## Dependencies

F02 should restrict access to logs, but redaction should still be implemented independently.

## Suggested Agent Type

Security agent / C++ refactoring agent
