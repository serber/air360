# H1 — `esp_http_client_init`/`cleanup` per upload, no keep-alive

- **Severity:** High
- **Area:** Performance / reliability on cellular
- **Files:**
  - `firmware/main/src/uploads/upload_transport.cpp`
  - `firmware/main/src/uploads/upload_manager.cpp`

## What is wrong

Each upload call:

1. `esp_http_client_init(&cfg)` — allocates client, parses URL, creates TCP + TLS state.
2. Sends the request.
3. `esp_http_client_cleanup(...)` — tears everything down.

`keep_alive_enable = false`. No connection is reused across calls.

## Why it matters

- **Cost on cellular.** TLS handshake is ~1–3 s and several KB of heap allocation. Over a weak LTE link, 5 s is not unusual.
- **Backlog replay storms.** After a disconnect, `UploadManager` may flush 256 queued samples. That currently means 256 independent TLS handshakes.
- **Heap fragmentation.** Allocating and freeing the mbedTLS session + buffers on every call leaves small holes in the heap that, over days of uptime, compound.
- **Bandwidth cost.** For paid cellular plans this is real money.

## Consequences on real hardware

- After a 1-hour cellular outage, reconnect-and-flush takes many minutes, during which another outage can happen — compounding.
- Long-soak devices run out of contiguous heap for `malloc(~8 KB)` even while total free heap looks ample.

## Fix plan

1. **Cache the client handle per backend URL.**
   - `UploadTransport` owns a `std::unordered_map<std::string, esp_http_client_handle_t>` keyed by `scheme+host+port`, or a small fixed array if the number of backends is bounded.
   - First call initializes; subsequent calls reuse.
2. **Enable keep-alive:**
   ```cpp
   esp_http_client_config_t cfg = {};
   cfg.url = backend_url;
   cfg.keep_alive_enable = true;
   cfg.keep_alive_idle = 5;
   cfg.keep_alive_interval = 5;
   cfg.keep_alive_count = 3;
   cfg.timeout_ms = 10'000;
   cfg.buffer_size = 2048;        // see H2
   cfg.buffer_size_tx = 1024;
   ```
3. **Rotate on error.** If a request fails with a connection-level error (`ESP_ERR_HTTP_CONNECT`, TLS handshake failure, or N consecutive non-2xx), destroy the handle and re-init on next call.
4. **Rotate on schedule.** Force-rotate every N requests (e.g. 100) or every M minutes, to avoid long-lived TLS sessions that the server may silently drop.
5. **Respect `Connection: close`** from server responses — if present, rotate.
6. **Thread-safety.** If multiple upload tasks can call `UploadTransport` concurrently, guard the cached-handle map with a mutex. Simpler: keep one upload worker task and serialize requests.
7. **Benchmark.** Compare handshake count and heap high-water mark before/after on a 10-minute backlog flush.

## Verification

- Instrument `upload_transport.cpp` to log handshake vs reused calls; on a 100-sample backlog, handshake count ≤ 2.
- Heap high-water mark after 24 h is within 5 % of start (was previously drifting).
- On backend restart mid-flush, recovery within one rotation cycle.

## Related

- H2 — bigger response buffer is part of the same `esp_http_client_config_t`.
- C2 — queue persistence means backlog flushes can be larger; this fix makes those flushes feasible.
