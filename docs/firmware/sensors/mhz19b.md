# MH-Z19B

## Status

Implemented. Keep this document aligned with the current MH-Z19B driver and registry defaults.

## Scope

This document covers the Air360 MH-Z19B driver, including UART binding defaults, warmup behavior, and CO2 measurement reporting.

## Source of truth in code

- `firmware/main/src/sensors/drivers/mhz19b_sensor.cpp`
- `firmware/main/include/air360/sensors/drivers/mhz19b_sensor.hpp`
- `firmware/main/src/sensors/sensor_registry.cpp`

## Read next

- [README.md](README.md)
- [supported-sensors.md](supported-sensors.md)
- [../transport-binding.md](../transport-binding.md)

Infrared CO2 sensor from Zhengzhou Winsen Electronics Technology. Communicates via UART using a proprietary binary protocol. Does not use I2C or ADC.

## Transport

- UART (9600 baud, 8N1)
- Default binding: UART2, RX=`GPIO16`, TX=`GPIO15`; UART1 is selectable through the sensor descriptor
- The `esp-idf-lib/mhz19b` component manages UART initialization internally; the driver calls `mhz19b_init()` directly and does not go through `UartPortManager`
- Baud rate is fixed at 9600 by both the sensor and the component library

## Hardware

The MH-Z19B is a standalone NDIR module. Connect directly to the ESP32 UART pins:

```
MH-Z19B  →  ESP32
Vin (5 V)   5 V
GND         GND
TXD         RX GPIO (default GPIO16)
RXD         TX GPIO (default GPIO15)
```

- The sensor requires a **5 V supply** on its Vin pin. Its UART logic levels are 3.3 V and are safe to connect directly to ESP32 GPIOs.
- PWM output pin (if present on the module) is unused by this driver.

## Initialization

1. Call `mhz19b_init(&device_, uart_port, tx_gpio, rx_gpio)` — the library installs the UART driver on the given port.
2. Set `initialized_ = true`.

The driver ignores `SensorDriverContext` entirely; no I2C bus or port manager interaction occurs.

## Warmup

The MH-Z19B requires approximately 3 minutes to stabilize after power-on. During warmup:

- `poll()` calls `mhz19b_is_warming_up(&device_, false)` — returns true if less than 3 minutes have elapsed since init.
- The driver sets `last_error_` to `"Warming up (3 min)."` and returns `ESP_OK`.
- No measurement is stored. The web UI will display the warmup message as an informational state rather than an error.

## Polling

Each poll cycle after warmup:

1. Check `mhz19b_is_ready()` — if false, skip (measurement not yet ready).
2. Call `mhz19b_read_co2(&device_, &co2)` — returns a 16-bit CO2 value in ppm.
3. Store as `SensorValueKind::kCo2Ppm`.

Default poll interval: 10 seconds. The sensor updates its reading approximately every 5 seconds; polling every 10 s avoids reading the same value twice.

## Measurements

| Measurement | ValueKind   | Unit |
|-------------|-------------|------|
| CO2         | `kCo2Ppm`  | ppm  |

## Notes

- If `mhz19b_read_co2()` returns an error, `initialized_` is set to `false` and the next poll cycle re-initializes the device.
- The descriptor assigns UART2 by default (`uart_port_id = 2`) to avoid conflict with GPS and the cellular modem on UART1. The web UI can move the sensor to UART1 when that port is free.
- The MH-Z19B measuring range is `0–5000 ppm`. Indoor air is typically `400–1000 ppm`; values above `1000 ppm` indicate poor ventilation.

## Recommended poll interval

10 seconds. The sensor produces a new reading approximately every 5 seconds; 10 s is a safe default that avoids duplicate readings.

## Component

`esp-idf-lib__mhz19b` v1.2.0 (managed component via `idf_component.yml`)

## Source files

- `firmware/main/src/sensors/drivers/mhz19b_sensor.cpp`
- `firmware/main/include/air360/sensors/drivers/mhz19b_sensor.hpp`
