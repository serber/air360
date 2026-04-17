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

- UART1 (fixed port)
- RX: GPIO18, TX: GPIO17 (default, configurable via `Kconfig`)
- Baud rate: 9600 (default, configurable)
- RX buffer: 4096 bytes

All UART parameters are taken from the sensor record (`SensorRecord`), which is pre-populated with `Kconfig` defaults when the sensor is created.

## Initialization

1. Store the UART port configuration from the sensor record
2. Create a `TinyGPSPlus` parser instance via `std::make_unique<TinyGPSPlus>()`
3. Open the UART port via `UartPortManager::open()`
4. Flush the UART buffer via `UartPortManager::flush()`

## Polling

Each poll cycle:
1. Read up to 2048 bytes from the UART buffer in 256-byte chunks with a 100 ms timeout per chunk
2. Feed each byte to the parser via `TinyGPSPlus::encode()`
3. After reading completes, build the measurement via `rebuildMeasurement()`

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
- The 100 ms read timeout means a single poll can block for up to `(2048 / 256) × 100 ms = 800 ms` in the worst case when the buffer is full.
- Speed is reported in **knots**. To convert: `km/h = knots × 1.852`.

## Recommended poll interval

Minimum 5 seconds. GPS modules typically output NMEA sentences once per second — more frequent polling brings no benefit.

## Component

`cinderblocks__esp_tinygpsplusplus` (managed component)

## Source files

- `firmware/main/src/sensors/drivers/gps_nmea_sensor.cpp`
- `firmware/main/include/air360/sensors/drivers/gps_nmea_sensor.hpp`
