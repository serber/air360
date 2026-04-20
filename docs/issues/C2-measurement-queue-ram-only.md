# C2 — Measurement queue is RAM-only, bounded at 256

- **Severity:** Critical
- **Area:** Data integrity / persistence
- **Files:**
  - `firmware/main/src/uploads/measurement_store.cpp`
  - `firmware/main/include/air360/uploads/measurement_store.hpp`
  - `firmware/main/src/uploads/upload_manager.cpp`

## What is wrong

`MeasurementStore` holds unacknowledged samples in RAM only:

- Capacity is `kMaxQueuedSamples = 256`.
- Per-sensor queued counts live in a `std::unordered_map<uint32_t, uint32_t>`.
- On overflow, the oldest sample is dropped and `dropped_sample_count_` is incremented.
- No writes to flash. No replay on boot.

## Why it matters

- Any reset, brownout, OTA, or crash drops every un-uploaded sample.
- 256 samples is very tight for a multi-sensor device during any cellular outage:
  - 6 sensors × 5 s cadence × 10 min outage ≈ 720 samples — 3× over capacity.
- The drop counter is diagnostic, not remedial — the data is gone.
- For an air-quality logger, silent data gaps are a compliance and trust problem, not a nice-to-have.

## Consequences on real hardware

- Users report "gaps" in dashboards you cannot reproduce on the bench.
- Every firmware update wipes the pending backlog.
- Every cellular flap longer than the queue depth silently loses data.

## Fix plan

1. **Add persistent queue storage.** Two options ranked:
   - **Preferred:** ring-buffer file on LittleFS (`/spiffs/queue.bin` or dedicated partition). Fixed-size records (~64 B per sample) indexed by sequence number. Head/tail cursors written atomically to a separate small file.
   - **Alternative:** NVS blob-page pool with write-ahead log semantics. Lower wear budget; use only if LittleFS partition is impossible.
2. **Schema decision.** Record layout:
   ```
   struct PersistedSample {
       uint32_t seq;          // monotonic
       uint32_t sensor_id;    // registry key
       uint64_t sample_time_ms;
       uint8_t  value_count;
       uint8_t  reserved[3];
       SensorValue values[kMaxValuesPerSample];
       uint32_t crc32;        // over everything above
   };
   ```
3. **Hybrid fast-path.** Keep the RAM deque for the hot path. Spill to persistent storage above a watermark (e.g. 128 samples) or on forced shutdown signal.
4. **Per-backend cursors** persist alongside the queue. Pruning is allowed only when all backends have acknowledged past a given sequence number.
5. **Bounded capacity in persistent storage too.** Decide overflow policy explicitly: drop-oldest with counter, or refuse-new with pushback. Document the choice.
6. **Replay on boot.** On `UploadManager::start`, load persistent cursors and feed pending samples back to adapters.
7. **Partition plan.** Update `partitions.csv` to reserve a dedicated data partition (e.g. 512 KB) — do not share with spiffs used for the web UI.
8. **Flash-wear budget.** At 6 sensors × 5 s cadence and a 64 B record, you write ~76 B/s steady-state. Over a 10-year lifetime that is ~24 GB — well within SPI flash endurance (~10⁵ cycles × partition size), but log the assumed budget in `nvs.md` / `measurement-pipeline.md`.

## Verification

- Host-side tests: fake LittleFS, inject samples, crash mid-write, restart, assert no sample is duplicated or lost across the crash boundary.
- Long-soak: device on cellular, forced flaps, 24 h, compare local-persisted counts against backend-received counts.
- OTA survival test: queue 100 samples, trigger OTA, verify backlog delivers post-boot.

## Related

- H1 (HTTP keep-alive) reduces pressure on the queue by making uploads cheaper.
- H5 (NVS backup blob) uses a similar "two-copy + validate" idea and may share helpers.
