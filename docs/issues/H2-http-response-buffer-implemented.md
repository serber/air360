# H2 — HTTP response buffer 512 B is too small

- **Status:** Implemented
- **Severity:** High
- **Area:** Upload transport / reliability
- **Files:**
  - `firmware/main/src/uploads/upload_transport.cpp` (`esp_http_client_config_t::buffer_size = 512`)

## What is wrong

The response buffer for `esp_http_client` is configured at 512 B. This is the buffer used to parse response headers *and* incrementally hand response body chunks to the user.

Real-world backends commonly return:

- Cloudflare / WAF headers: `cf-ray`, `cf-cache-status`, `server-timing`, `strict-transport-security`, `report-to` — easily 1 KB combined.
- API gateways with tracing headers (`x-request-id`, `x-trace-id`, `traceparent`).
- Cookie headers on redirect paths.
- Error responses with signed 2–4 KB HTML pages from CDN edges.

When the buffer is too small for headers, `esp_http_client` can return errors like `ESP_ERR_HTTP_FETCH_HEADER` or silently truncate. `Retry-After` headers — critical for backoff behavior — can be lost.

## Why it matters

- Silent upload failures after a backend migration or CDN policy change.
- Missed `Retry-After` leads to hammering a throttled backend, compounding the problem.
- Truncated error bodies prevent useful diagnostics.

## Consequences on real hardware

- Works against the dev backend, breaks behind a new reverse-proxy or WAF.
- Sporadic upload failures with no useful log signature.

## Fix plan

1. **Raise the response buffer:**
   ```cpp
   cfg.buffer_size     = 2048;   // at minimum
   cfg.buffer_size_tx  = 1024;   // for request headers
   ```
2. **If memory-tight,** measure typical response header size with `esp_http_client_get_header()` across all configured backends and pick the max + 30 %.
3. **Always read `Retry-After`** and feed it into the `UploadManager` backoff logic. Currently the adapter may ignore it.
4. **Log header-parse failures** explicitly — today they are indistinguishable from transport errors.
5. **Review for large-body endpoints.** If any backend returns JSON bodies of significant size (health endpoints), ensure chunked reads are used rather than blocking on full body buffering.

## Verification

- Point at a Cloudflare-proxied test endpoint; verify headers parse cleanly.
- Inject a 429 with `Retry-After: 60`; verify the adapter honors it.
- Heap impact: 1.5 KB extra per active client — acceptable given H1 caches one per backend.

## Outstanding

- **Step 5 (large-body endpoints) — not implemented.** The fix plan asked to verify that any endpoint returning a significant JSON body (e.g. health checks) uses chunked reads rather than full-body buffering. The current transport calls `esp_http_client_perform()` and reads only `Content-Length`; it does not read the response body at all. No upload adapter currently issues health-check requests, so there is no large-body path to fix. If a future adapter adds body-reading (`esp_http_client_read()`), it must use a chunked loop rather than a single blocking read.

## Related

- H1 — both fixes live in the same `esp_http_client_config_t`.
