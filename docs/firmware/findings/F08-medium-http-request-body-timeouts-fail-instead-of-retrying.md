# Finding F08: HTTP request body timeouts fail instead of retrying

## Status

Confirmed audit finding. Not implemented.

## Scope

Task file for improving ESP-IDF HTTP request-body handling on slow clients.

## Source of truth in code

- `firmware/main/src/web/web_server_helpers.cpp`
- `firmware/main/src/web/web_mutating_routes.cpp`
- `firmware/main/src/web/web_runtime_routes.cpp`

## Read next

- `docs/firmware/web-ui.md`

**Priority:** Medium
**Category:** ESP-IDF / Reliability
**Files / symbols:** `air360::web::readRequestBody`, `WebServer::handleConfig`, `WebServer::handleSensors`, `WebServer::handleBackends`, `WebServer::handleCheckSntp`

## Problem

`readRequestBody()` treats every `httpd_req_recv()` return value `<= 0` as a hard failure. ESP-IDF HTTP server can return timeout errors for slow clients; handlers are expected to retry on timeout instead of failing the request immediately.

## Why it matters

On weak Wi-Fi or captive-portal phone clients, valid config form submissions can fail. This is especially painful during setup because the user may be trying to recover network credentials.

## Evidence

- `firmware/main/src/web/web_server_helpers.cpp:26` loops until `received_total < request->content_len`.
- `firmware/main/src/web/web_server_helpers.cpp:31` calls `httpd_req_recv()`.
- `firmware/main/src/web/web_server_helpers.cpp:35` returns `ESP_FAIL` for any `received <= 0`.
- Callers render generic "Failed to read form body." messages:
  - `firmware/main/src/web/web_mutating_routes.cpp:107`
  - `firmware/main/src/web/web_mutating_routes.cpp:416`
  - `firmware/main/src/web/web_mutating_routes.cpp:585`
  - `firmware/main/src/web/web_runtime_routes.cpp:170`

## Recommended Fix

Handle `HTTPD_SOCK_ERR_TIMEOUT` as a retryable condition with a bounded retry budget. Keep other negative returns as hard failures.

## Where To Change

- `firmware/main/src/web/web_server_helpers.cpp`
- `firmware/main/include/air360/web_server_internal.hpp` if return detail is expanded
- `firmware/test/host/test_web_form.cpp` or a new helper-level host test if the receive loop is abstracted
- `docs/firmware/web-ui.md`

## How To Change

1. Include the ESP-IDF HTTP server error constants where needed.
2. Retry `HTTPD_SOCK_ERR_TIMEOUT` until either:
   - all bytes are received, or
   - a bounded retry/elapsed-time limit is reached.
3. Return a distinct error for client timeout so the UI can show a more specific message.
4. Keep the 4096 byte body limit.

## Example Fix

```cpp
int timeout_count = 0;
while (received_total < request->content_len) {
    const int received = httpd_req_recv(
        request,
        out_body.data() + received_total,
        request->content_len - received_total);

    if (received == HTTPD_SOCK_ERR_TIMEOUT) {
        if (++timeout_count > kMaxBodyReadTimeouts) {
            return ESP_ERR_TIMEOUT;
        }
        continue;
    }
    if (received <= 0) {
        return ESP_FAIL;
    }
    timeout_count = 0;
    received_total += received;
}
```

## Validation

- Add a test seam around body receiving, or manually test with a slow client that sends a form body in chunks.
- Confirm oversized bodies still return 413.
- Confirm disconnected clients still fail quickly.
- Run `source ~/.espressif/v6.0/esp-idf/export.sh && idf.py build` from `firmware/`.
- Run `python3 scripts/check_firmware_docs.py`.

## Risk Of Change

Low.

## Dependencies

None.

## Suggested Agent Type

ESP-IDF agent / testing agent
