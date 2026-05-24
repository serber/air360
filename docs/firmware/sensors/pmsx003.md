# PMSX003

## Status

Implemented. Keep this document aligned with the current PMSX003 driver and registry defaults.

## Scope

This document covers the Air360 PMSX003 driver, including UART binding defaults, ESP32-S3 wiring, frame parsing, and particulate matter measurements.

## Source of truth in code

- `firmware/main/src/sensors/drivers/pmsx003_sensor.cpp`
- `firmware/main/include/air360/sensors/drivers/pmsx003_sensor.hpp`
- `firmware/main/src/sensors/sensor_registry.cpp`

## Read next

- [README.md](README.md)
- [supported-sensors.md](supported-sensors.md)
- [../transport-binding.md](../transport-binding.md)

Plantower PMS family particulate matter sensor. Air360 uses the `petrovgp/esp-pms` ESP-IDF component and reports PM1.0, PM2.5, PM10, plus particle counts by size bin.

## Transport

- UART at `9600` baud, 8 data bits, no parity, 1 stop bit
- Default binding: UART2, RX=`GPIO16`, TX=`GPIO15`
- UART1 is selectable through the descriptor: RX=`GPIO18`, TX=`GPIO17`
- The driver opens UART through `UartPortManager`, then passes the selected `uart_port_t` to `pms_init()`

## Hardware

Connect the module to a 5 V supply and cross UART TX/RX:

```text
PMSX003  ->  ESP32-S3 / Air360
VCC          5 V
GND          GND
TX           RX GPIO (default GPIO16 on UART2)
RX           TX GPIO (default GPIO15 on UART2)
SET          not connected by Air360
RESET        not connected by Air360
```

- The PMS module is powered from `5 V`.
- UART logic is TTL-level `3.3 V`.
- `SET` and `RESET` are optional control pins. Air360 leaves them unconfigured and uses UART/component state management only.
- UART1 alternate wiring is RX=`GPIO18`, TX=`GPIO17`.
- Keep the air inlet and outlet unobstructed; airflow restriction affects readings.

## Initialization

1. Store the selected `SensorRecord`.
2. Open the selected UART through `context.uart_port_manager->open()`.
3. Reset the singleton `petrovgp/esp-pms` component state with `pms_deinit()`.
4. Initialize the component with `PMS_TYPE_5003`, no `SET` GPIO, no `RESET` GPIO, and the selected UART port.
5. Flush stale UART input.
6. Mark the driver initialized.

The driver does not create a FreeRTOS task. It runs inside the existing sensor manager polling task. The component owns its internal wake/reset timing state.

## Polling

Each poll cycle:

1. Drain UART events and reset the parser if an overrun was reported.
2. Check `pms_get_state()`.
3. If the component is not `PMS_STATE_ACTIVE`, return `ESP_OK` with `PMSX003 is warming up.` and do not store a measurement.
4. Read available UART bytes with a 100 ms first-read timeout.
5. Parse 32-byte PMS frames:
   - header `0x42 0x4D`
   - length `0x00 0x1C`
   - payload and checksum handled by `pms_parse_data()`
6. Store the latest valid frame from the poll cycle.

If no bytes arrive, or bytes arrive without a valid frame, the poll returns `ESP_OK` with `last_error_` set and keeps the previous measurement.

## Measurements

| Measurement | ValueKind | Unit |
|-------------|-----------|------|
| PM1.0 atmospheric mass concentration | `kPm1_0UgM3` | ug/m3 |
| PM2.5 atmospheric mass concentration | `kPm2_5UgM3` | ug/m3 |
| PM10 atmospheric mass concentration | `kPm10_0UgM3` | ug/m3 |
| Particle count above 0.3 um | `kPc0_3Per0_1L` | #/0.1L |
| Particle count above 0.5 um | `kPc0_5Per0_1L` | #/0.1L |
| Particle count above 1.0 um | `kPc1_0Per0_1L` | #/0.1L |
| Particle count above 2.5 um | `kPc2_5Per0_1L` | #/0.1L |
| Particle count above 5.0 um | `kPc5_0Per0_1L` | #/0.1L |
| Particle count above 10 um | `kPc10Per0_1L` | #/0.1L |

PM values use the component's atmospheric fields: `PMS_FIELD_PM1_ATM`, `PMS_FIELD_PM2_5_ATM`, and `PMS_FIELD_PM10_ATM`.

## Notes

- Air360 registers this as `SensorType::kPmsx003 = 19`.
- The descriptor uses `PMS_TYPE_5003`, which covers the common 32-byte Plantower frame used by PMS5003/PMS7003/PMSA003-class modules.
- PMS3003 uses a shorter frame and is not covered by this descriptor.
- Sensor.Community receives PM1.0 as `P0`, PM2.5 as `P2`, and PM10 as `P1`; PMS particle-count bins are skipped there.
- The `petrovgp/esp-pms` component keeps global singleton state, so the firmware exposes PMSX003 as a single sensor in the Particulate Matter category.

## Recommended poll interval

30 seconds. PMS modules can stream faster, but Air360 defaults to 30 seconds to keep upload volume and UI churn lower.

## Component

`petrovgp/esp-pms` version `^1.0.0`.

## Source files

- `firmware/main/src/sensors/drivers/pmsx003_sensor.cpp`
- `firmware/main/include/air360/sensors/drivers/pmsx003_sensor.hpp`
