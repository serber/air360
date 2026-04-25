# GPS (NMEA)

## Status

Implemented. Keep this document aligned with the current GPS NMEA driver and registry defaults.

## Scope

This document covers the Air360 GPS NMEA sensor path, including UART defaults, parsed output fields, and driver-level behavior.

## Source of truth in code

- `firmware/main/src/sensors/drivers/gps_nmea_sensor.cpp`
- `firmware/main/include/air360/sensors/drivers/gps_nmea_sensor.hpp`
- `firmware/main/src/sensors/sensor_registry.cpp`

## Read next

- [README.md](README.md)
- [supported-sensors.md](supported-sensors.md)
- [../transport-binding.md](../transport-binding.md)

UART GPS receiver with NMEA sentence parsing via the TinyGPSPlus library.

## Transport

- Default UART1: RX=GPIO18, TX=GPIO17
- UART2 selectable: RX=GPIO16, TX=GPIO15
- Baud rate: 9600 (default, configurable)
- RX buffer: `max(4096, max_bytes_per_poll + 256)` bytes, where `max_bytes_per_poll = ceil((baud / 10) * poll_interval_seconds) + 256`

UART port selection is taken from the sensor record (`SensorRecord`), which is pre-populated from the sensor descriptor when the sensor is created. The web UI lets the record select UART1 or UART2 and writes the matching RX/TX pins from the firmware UART map.

## Initialization

1. Store the UART port configuration from the sensor record
2. Reset the in-place `TinyGPSPlus` parser
3. Derive `max_bytes_per_poll` from `uart_baud_rate` and `poll_interval_ms`
4. Derive the blocking read timeout as `80 %` of `poll_interval_ms` (minimum `50 ms`)
5. Open the UART port via `UartPortManager::open()` with an event queue and an RX ring buffer sized above the derived per-poll budget
6. Flush the UART buffer via `UartPortManager::flush()`

## Polling

Each poll cycle:
1. Drain pending UART driver events and count `UART_FIFO_OVF` / `UART_BUFFER_FULL` overruns
2. Perform one blocking `uart_read_bytes()` call through `UartPortManager::read()` with the derived timeout to wait for the next NMEA burst
3. Continue reading in `256`-byte chunks with zero timeout while `uart_get_buffered_data_len()` reports queued RX data
4. Feed each byte to the parser via `TinyGPSPlus::encode()`
5. Drain UART events again after the read loop so late overruns are logged in the same poll cycle
6. After reading completes, build the measurement via `rebuildMeasurement()`

`rebuildMeasurement()` checks `isValid()` on each parser field and adds only confirmed values.

## Measurements

All fields are included only when valid GPS data is available:

| Measurement | ValueKind | Unit |
|-------------|-----------|------|
| Latitude | `kLatitudeDeg` | ° (decimal) |
| Longitude | `kLongitudeDeg` | ° (decimal) |
| Altitude | `kAltitudeM` | m |
| Satellites | `kSatellites` | count |
| Speed | `kSpeedKnots` | knots |
| Course | `kCourseDeg` | ° |
| HDOP | `kHdop` | dimensionless |

## Notes

- **No fix** — until the first GPS fix is acquired the driver returns `ESP_OK` with `"No GPS fix yet."` in the error field. This is expected behavior at startup and after loss of signal.
- Fields are added to the measurement independently. If the module has satellite data but no coordinates yet, `kSatellites` will be present while `kLatitudeDeg` and `kLongitudeDeg` will not.
- All fields collected in a single poll share the same timestamp (the moment the first valid field is found).
- The first read may block for up to `80 %` of the configured poll interval, which gives slow GPS UART links time to accumulate a full NMEA burst before the driver switches to drain-to-empty reads.
- UART overrun events are counted from the UART driver's event queue and logged via `ESP_LOGW` so field reports can distinguish real parser starvation from line noise or wiring issues.
- Speed is reported in **knots**. To convert: `km/h = knots × 1.852`.

## Recommended poll interval

Minimum 5 seconds. GPS modules typically output NMEA sentences once per second — more frequent polling brings no benefit.

## Component

`cinderblocks__esp_tinygpsplusplus` (managed component)

## Source files

- `firmware/main/src/sensors/drivers/gps_nmea_sensor.cpp`
- `firmware/main/include/air360/sensors/drivers/gps_nmea_sensor.hpp`
