# GPS (NMEA) Sensor

## Overview

The GPS driver receives NMEA sentences from a GPS module over UART, parses them using the [TinyGPS++](https://components.espressif.com/components/cinderblocks/esp_tinygpsplusplus) ESP-IDF component, and exposes the extracted values as a standard `SensorMeasurement`.

---

## Hardware wiring

The GPS module connects to **UART1** on the ESP32-S3:

| ESP32-S3 GPIO | Signal | Connected to |
|---|---|---|
| **GPIO 17** (U1TXD) | TX | GPS module RX |
| **GPIO 18** (U1RXD) | RX | GPS module TX |

Default baud rate: **9600**.

> The UART port and GPIO pins are fixed at compile time (see [Configuration](#configuration)). The validator in `sensor_registry.cpp` rejects any sensor record that specifies different values — changing them requires a firmware rebuild.

---

## Measured values

| `SensorValueKind` | Unit | Source field |
|---|---|---|
| `kLatitudeDeg` | degrees | `location.lat()` |
| `kLongitudeDeg` | degrees | `location.lng()` |
| `kAltitudeM` | metres | `altitude.meters()` |
| `kSatellites` | count | `satellites.value()` |
| `kSpeedKnots` | knots | `speed.knots()` |
| `kCourseDeg` | degrees | `course.deg()` |
| `kHdop` | — | `hdop.hdop()` |

Only fields reported as valid by TinyGPS++ (`isValid()`) are included in the measurement. A measurement with no valid fields sets `last_error_` to `"No GPS fix yet."`.

---

## Source files

| File | Purpose |
|---|---|
| `main/include/air360/sensors/drivers/gps_nmea_sensor.hpp` | Class declaration, forward declaration of `TinyGPSPlus` |
| `main/src/sensors/drivers/gps_nmea_sensor.cpp` | Driver implementation |
| `main/src/sensors/sensor_registry.cpp` | Default config, validation (`validateGpsNmeaRecord`) |

---

## Implementation notes

**Initialization** (`init()`): opens the configured UART port via `UartPortManager`, then flushes any stale data.

**Polling** (`poll()`): reads up to 2048 bytes per call in 256-byte chunks with a 100 ms timeout on the first read and a non-blocking read for subsequent chunks. Each byte is forwarded to `TinyGPSPlus::encode()`. After all bytes are consumed, `rebuildMeasurement()` extracts the latest valid fields.

**Thread safety**: the driver holds no shared state beyond its own member variables; access is serialised by the `SensorManager` polling task.

---

## Configuration

Compile-time defaults are set in `sdkconfig.defaults` and declared in `main/Kconfig.projbuild`:

| `sdkconfig.defaults` key | Kconfig option | Value | Range |
|---|---|---|---|
| `CONFIG_AIR360_GPS_DEFAULT_UART_PORT` | `AIR360_GPS_DEFAULT_UART_PORT` | `1` | 1–2 |
| `CONFIG_AIR360_GPS_DEFAULT_RX_GPIO` | `AIR360_GPS_DEFAULT_RX_GPIO` | `18` | 0–48 |
| `CONFIG_AIR360_GPS_DEFAULT_TX_GPIO` | `AIR360_GPS_DEFAULT_TX_GPIO` | `17` | 0–48 |
| `CONFIG_AIR360_GPS_DEFAULT_BAUD_RATE` | `AIR360_GPS_DEFAULT_BAUD_RATE` | `9600` | 1200–115200 |

To change any of these values, update `sdkconfig.defaults` and rebuild. The validator enforces that the runtime sensor record matches the compiled values, so a mismatch produces an error at sensor configuration time rather than silently using wrong pins.

---

## ESP-IDF component

TinyGPS++ is provided by the managed component `cinderblocks/esp_tinygpsplusplus` (v1.1.1), declared in `main/idf_component.yml`. The component is downloaded automatically by `idf.py build` on first use.

Include in consumer code:

```cpp
#include <TinyGPSPlus.h>
```

CMakeLists.txt dependency:

```cmake
REQUIRES cinderblocks__esp_tinygpsplusplus
```
