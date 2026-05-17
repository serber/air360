# PPD42NS

## Status

Implemented. Keep this document aligned with the current PPD42NS GPIO driver and registry defaults.

## Scope

This document covers the Air360 PPD42NS driver, GPIO binding, pulse-counting model, and ESP32-S3 wiring.

## Source of truth in code

- `firmware/main/src/sensors/drivers/ppd42ns_sensor.cpp`
- `firmware/main/include/air360/sensors/drivers/ppd42ns_sensor.hpp`
- `firmware/main/src/sensors/sensor_registry.cpp`

## Read next

- [README.md](README.md)
- [supported-sensors.md](supported-sensors.md)
- [../transport-binding.md](../transport-binding.md)

Shinyei PPD42NS optical dust sensor with a negative-logic digital pulse output. The driver measures low pulse occupancy (LPO) from the `P1` output and reports the PPD42NS concentration estimate in particles per `0.01 cf`.

## Transport

- GPIO input on the selected Air360 sensor pin
- Default binding: first allowed GPIO, currently `GPIO4`
- Selectable pins: `GPIO4`, `GPIO5`, or `GPIO6`
- The driver installs a GPIO interrupt handler for both edges and does not create a FreeRTOS task

## Hardware

Connect the sensor to the ESP32-S3 as follows:

```text
PPD42NS / Grove dust sensor      ESP32-S3 / Air360
5 V / red wire / CN1 pin 3       5 V supply
GND / black wire / CN1 pin 1     GND
P1 / yellow wire / CN1 pin 4     GPIO4 by default, or GPIO5/GPIO6 if selected
P2 / CN1 pin 2                   Not used
T1 threshold / CN1 pin 5         Leave open unless the hardware design tunes P2
```

- PPD42NS is powered from `5 V` and draws about `90 mA`.
- Share ground between the sensor and ESP32-S3.
- The PPD42NS digital output is pulled up to the sensor logic level and the public datasheet lists HIGH above `4.0 V`; ESP32-S3 GPIO is not 5 V tolerant. Use a level shifter or resistor divider before connecting `P1` to `GPIO4`/`GPIO5`/`GPIO6`.
- Air360 currently reads `P1` only. `P2` is left unconnected by the firmware.

## Initialization

1. Store the selected `SensorRecord`.
2. Configure `record.analog_gpio_pin` as input with no internal pull-up or pull-down.
3. Install the shared GPIO ISR service if it is not already installed.
4. Attach a per-instance interrupt handler for both edges.
5. Reset the low-pulse accumulator and start the 60 second warmup timer.

## Polling

The ISR records LOW pulse duration continuously. Each poll cycle:

1. Returns `ESP_OK` without a measurement during the 60 second sensor warmup.
2. Waits until at least a 30 second accumulation window is available.
3. Converts `low_us / window_us` to low pulse occupancy percent.
4. Converts LPO ratio to concentration with the common PPD42NS cubic fit.
5. Stores the latest concentration and LPO percent values.

The driver never blocks for the 30 second sample window. It accumulates pulse timing in the GPIO ISR and snapshots it from the sensor manager task.

## Measurements

| Measurement | ValueKind | Unit |
|-------------|-----------|------|
| Dust concentration estimate | `kDustConcentrationPcs001Cf` | pcs/0.01cf |
| Low pulse occupancy | `kLowPulseOccupancyPercent` | % |

## Notes

- The PPD42NS output is a dust-count estimate, not a calibrated PM2.5 or PM10 mass concentration in `ug/m3`.
- The firmware uses `P1`, which the datasheet defines for particles around `1 um` or larger.
- Readings are noisy and depend on airflow, orientation, and enclosure design. Keep the sensor opening unobstructed.
- GPIO ISR work is limited to timestamp accumulation under a short critical section.

## Recommended poll interval

30 seconds. This matches the datasheet output-characteristic window and Air360's minimum sensor poll interval.

## Component

None. The driver uses ESP-IDF GPIO and timer APIs.

## Source files

- `firmware/main/src/sensors/drivers/ppd42ns_sensor.cpp`
- `firmware/main/include/air360/sensors/drivers/ppd42ns_sensor.hpp`
