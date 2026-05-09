# Upload Transport

## Status

Implemented. Keep this document aligned with the current HTTP execution helper shared by HTTP-backed upload adapters.

## Scope

This document covers the low-level HTTP transport used by the firmware's HTTP-backed upload adapters: request execution, TLS setup, response capture, and transport-level error handling.

## Source of truth in code

- `firmware/main/src/uploads/upload_transport.cpp`
- `firmware/main/include/air360/uploads/upload_transport.hpp`

## Read next

- [upload-adapters.md](upload-adapters.md)
- [measurement-pipeline.md](measurement-pipeline.md)

`UploadTransport` is the HTTP execution helper used by current HTTP-backed adapters. It takes a fully-constructed `UploadRequestSpec` and returns an `UploadTransportResponse`. It has no state — the class is a stateless executor with a single `execute()` method.

`UploadTransport` is not the backend integration interface. `UploadManager` calls each backend adapter's `deliver()` method, and HTTP adapters use `UploadTransport` internally through `BackendDeliveryContext`. Future non-HTTP backends can use a different delivery service while preserving the same queue and acknowledgement flow.

---

## Request execution

`UploadTransport::execute()` maps `UploadRequestSpec` to `esp_http_client` and performs the request synchronously. Steps:

1. Configure `esp_http_client_config_t` from the spec (URL, method, timeout).
2. Call `esp_http_client_init()`.
3. Set all headers from `request.headers` via `esp_http_client_set_header()`.
4. If `request.body` is non-empty, set it via `esp_http_client_set_post_field()`.
5. Attach a response event handler that captures a sanitized response-body snippet.
6. Call `esp_http_client_perform()` — blocks until the response is received or the timeout expires.
7. Extract HTTP status code, response content length, and `Retry-After`.
8. Compute total wall-clock duration from `esp_timer_get_time()`.
9. Call `esp_http_client_cleanup()` unconditionally.

The client handle is created and destroyed within a single `execute()` call — there is no connection reuse.

`execute()` does not feed the task watchdog itself. HTTP-backed adapters feed the TWDT through `BackendDeliveryContext` immediately before and after each call so multi-request batches do not starve the watchdog between blocking HTTP operations.

---

## Client configuration

| Parameter | Value | Notes |
|-----------|-------|-------|
| Timeout | `request.timeout_ms` | Set by adapter; default 15 000 ms |
| RX buffer | 2 048 bytes | `config.buffer_size` — sized for Cloudflare/WAF header sets |
| TX buffer | 1 024 bytes | `config.buffer_size_tx` |
| Keep-alive | disabled | `keep_alive_enable = false` |
| Auto-redirect | disabled | `disable_auto_redirect = true` |
| Address type | IPv4 (`HTTP_ADDR_TYPE_INET`) | |
| TLS | CRT bundle | `crt_bundle_attach = esp_crt_bundle_attach` |

TLS is supported via the ESP-IDF built-in certificate bundle (`esp_crt_bundle`). The same client configuration is used for both HTTP and HTTPS URLs — the library selects TLS automatically based on the URL scheme.

---

## `UploadTransportResponse`

```cpp
struct UploadTransportResponse {
    esp_err_t  transport_err;           // ESP_OK or ESP-IDF error code
    int        http_status;             // HTTP status code; 0 if request failed
    int        response_size;           // Content-Length from response; 0 if absent
    uint32_t   response_time_ms;        // Total wall-clock duration in ms
    uint32_t   connect_time_ms;         // Reserved — always 0
    uint32_t   request_send_time_ms;    // Reserved — always 0
    uint32_t   first_response_time_ms;  // Reserved — always 0
    uint32_t   retry_after_seconds;     // Parsed Retry-After value; 0 if absent or > 3600
    string     body_snippet;            // Sanitized response body or local setup error
};
```

`connect_time_ms`, `request_send_time_ms`, and `first_response_time_ms` are defined in the struct but not populated by the current implementation. They are reserved for future per-phase timing.

When `esp_http_client_perform()` completes with `ESP_OK`, `body_snippet` contains up to 512 printable bytes from the HTTP response body. Newlines and tabs are collapsed to spaces, other non-printable bytes become `?`, and longer bodies are truncated with `...`. Adapters copy this field into `UploadAttemptResult::response_body_snippet`; it is used only by the upload task warning log and is not exposed through backend status, diagnostics JSON, or the web UI.

`body_snippet` is also set to a short human-readable error string in the following early-exit cases:

| Failure point | `body_snippet` value |
|---------------|---------------------|
| `esp_http_client_init` returned `nullptr` | `"esp_http_client_init failed"` |
| Header set failed | `"failed to set request header"` |
| Body set failed | `"failed to set request body"` |
| `esp_http_client_perform` succeeded | sanitized response body snippet |

If `esp_http_client_perform` fails (network error, DNS failure, timeout), `transport_err` is set to the ESP-IDF error code and `http_status` remains 0. `body_snippet` is empty in this case — the error is in `transport_err`.

`retry_after_seconds` is populated whenever `transport_err == ESP_OK` and the response contains a numeric `Retry-After` header in the range 1–3600. Values outside that range, HTTP-date format, and absent headers all result in `retry_after_seconds == 0`.

---

## Response classification

`UploadTransport` does not interpret the HTTP status itself. HTTP-backed adapters read `transport_err` and `http_status` to determine `UploadResultClass` for the current backend delivery window:

- `transport_err != ESP_OK` → `kTransportError` (regardless of `http_status`)
- `http_status` in success range → `kSuccess`
- anything else → `kHttpError`

See [upload-adapters.md](upload-adapters.md) for the per-adapter classification rules.

When a request fails and `retry_after_seconds > 0`, `UploadManager` overrides the backend's next-action time to `now + retry_after_seconds` instead of the normal upload interval. This is an override in both directions: the server may request a longer wait (e.g. HTTP 429 throttle) or a shorter one. Values capped at 3600 s by the transport; values above that are silently zeroed and the normal interval applies.

---

## Logging

Every request is logged at `INFO` level before execution:

```
air360.http: HTTP request: method=POST endpoint=https://api.sensor.community body_len=142
```

The logged endpoint is limited to protocol, host, and non-default port. Path, query string, fragment, and URL userinfo are never logged. Explicit default ports are omitted (`http:80`, `https:443`); non-default ports remain visible.

If `esp_http_client_perform` returns `ESP_ERR_HTTP_FETCH_HEADER`, the transport logs an explicit `ERROR`-level message including the same sanitized endpoint, distinguishing this failure mode from generic transport errors:

```
air360.http: HTTP header parse failed (buffer too small?): https://api.example.test:8443
```

Log tag: `air360.http`.

Backend delivery failures are logged by `UploadManager` at `WARN` level. For HTTP status failures, the log includes the backend id, backend type, result class, HTTP status code, and adapter error message. When the server returned a response body, the same log line appends the sanitized body snippet as `response_body`:

```
air360.upload: Backend upload failed: backend_id=2 type=air360_api result=http_error http_status=400 error=HTTP 400 response_body={"error":"invalid payload"}
```

Log tag: `air360.upload`.
