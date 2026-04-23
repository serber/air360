# M9 — GPS poll budget / timeout combination can starve

- **Severity:** Medium
- **Area:** Sensor driver timing
- **Files:**
  - `firmware/main/src/sensors/drivers/gps_nmea_sensor.cpp` (`kGpsReadTimeoutTicks = 100 ms`, `kGpsMaxBytesPerPoll = 2048`)

## What is wrong

Each poll consumes up to 2 KB of UART data with a single 100 ms read timeout.

- 2048 B at 9600 baud = ~2.1 s of data.
- 2048 B at 38400 baud = ~530 ms.
- 2048 B at 115200 baud = ~180 ms.

If baud is on the low end (common for GPS modules), the read will either:
- Return less than 2 KB within 100 ms, OK.
- Block the full 100 ms and return short, causing the parser to be starved (lose a fix).
- At higher baud, quickly fill 2 KB but then the poll function returns and the UART receive buffer may overflow between polls if polling cadence is low.

## Why it matters

- GPS modules emit bursty data. 100 ms is long enough to miss a full burst cycle, too short to drain a long burst.
- 2048 B budget + no flow control makes worst-case behavior depend on baud + poll cadence, which are set elsewhere.

## Consequences on real hardware

- Intermittent "no fix" reports when a fix should be available.
- UART overruns logged but dismissed as "normal."

## Fix plan

1. **Match budget to cadence and baud.** Calculate:
   ```
   max_bytes_per_poll = baud_rate_bytes_per_sec * poll_interval_seconds + margin
   ```
   Store the result in a constant derived from the sensor config, not a magic 2048.
2. **Increase the UART driver RX ring-buffer size** (`uart_driver_install(..., rx_buffer_size, ...)`) to > max_bytes_per_poll. A 4 KB buffer is cheap insurance.
3. **Use `uart_read_bytes` with a longer timeout** if poll cadence can accommodate it — say, 80 % of the poll interval. This avoids the 100 ms ceiling.
4. **Drain-to-empty idiom:** read until the UART RX buffer is empty (`uart_get_buffered_data_len`) rather than a fixed byte count. This avoids overruns during bursts.
5. **Log UART overrun events** from the driver's event queue (requires `uart_driver_install` with an event queue).

## Verification

- Bench with the configured GPS module: no fix dropouts over 1 h in clear view.
- UART overrun count stays at zero.

## Related

- None direct; independent.
