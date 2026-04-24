# M7 — `web_server.cpp` stack 10 KB with `std::string` rendering

- **Severity:** Medium
- **Area:** Memory / reliability
- **Files:**
  - `firmware/main/src/web_server.cpp` (httpd configuration, handlers)

## What is wrong

httpd worker tasks are configured with a 10 KB stack. Handlers render responses via `std::string +=` concatenation. Large pages (status page with many sensors, backends, and Wi-Fi scan results) can push both stack usage and heap growth to surprising levels.

## Why it matters

- Stack overflow is silent in FreeRTOS unless `CONFIG_FREERTOS_CHECK_STACKOVERFLOW` is on — verify this is enabled.
- Heap is consumed by every `std::string` realloc doubling; large pages allocate twice as much transient memory as the final size.
- If a handler recurses into another handler (unlikely but possible via redirects or include logic), stack pressure compounds.

## Consequences on real hardware

- Under worst-case page assembly + concurrent requests, stack or heap may be exhausted; the handler returns a truncated page or crashes.
- Difficult to reproduce — happens only with a specific content mix.

## Fix plan

1. **Instrument stack high-water mark per worker.** Log `uxTaskGetStackHighWaterMark()` when the watermark crosses thresholds (50 %, 70 %, 90 %).
2. **Pre-allocate response buffers.** `std::string::reserve(kExpectedMaxPageBytes)` at the top of each handler.
3. **Use streaming response APIs** (`httpd_resp_send_chunk`) for pages that can grow large. Render in chunks, free the chunk buffer after each send.
4. **Convert tight-bounded pages** to `snprintf` into a stack `char[1024]` buffer where possible.
5. **Ensure stack-overflow detection is on:**
   - `CONFIG_FREERTOS_CHECK_STACKOVERFLOW_CANARY=y`
   - A hook that logs task name and reboots cleanly.
6. **Review httpd worker count.** More workers = more stack budget. Rebalance if needed.
7. **Treat this together with H3** (split `web_server.cpp` + add escaping) — the same pass addresses both.

## Verification

- Status page with maximal sensor/backend configuration renders cleanly; high-water mark stays under 70 % of 10 KB.
- Stack-overflow canary hook is confirmed functional by a deliberate overflow test.
- Heap during concurrent requests (3 clients) stays within 20 % of idle.

## Related

- H3 (XSS + monolith split) — same files, same pass.
- H7 (STL on hot paths) — pre-allocation strategy is shared.
