# ME3-NO2

## Status

Implemented. Keep this document aligned with the current ME3-NO2 driver and registry defaults.

## Scope

This document covers the Air360 ME3-NO2 analog sensor path, including ADC binding, output fields, and current limitations.

## Source of truth in code

- `firmware/main/src/sensors/drivers/me3_no2_sensor.cpp`
- `firmware/main/include/air360/sensors/drivers/me3_no2_sensor.hpp`
- `firmware/main/src/sensors/sensor_registry.cpp`

## Read next

- [README.md](README.md)
- [supported-sensors.md](supported-sensors.md)
- [adding-new-sensor.md](adding-new-sensor.md)

Analog electrochemical nitrogen dioxide (NO₂) sensor from the ME3 series. Connected via ADC.

## Transport

- Analog input (ADC)
- Pin configured from the sensor record (`record.analog_gpio_pin`)
- Valid pins: GPIO4, GPIO5, GPIO6
- ADC unit: `ADC_UNIT_1` (resolved automatically from the GPIO number)
- Attenuation: `ADC_ATTEN_DB_12` (input range 0–3.9 V)
- Bit width: `ADC_BITWIDTH_DEFAULT`

## Initialization

1. Verify that `analog_gpio_pin >= 0`
2. Map the GPIO pin to an ADC unit and channel via `adc_oneshot_io_to_channel()`
3. Create the ADC unit via `adc_oneshot_new_unit()`
4. Configure the ADC channel via `adc_oneshot_config_channel()`:
   - Attenuation: 12 dB
   - Bit width: default
5. Attempt to create a calibration scheme:
   - First try curve fitting via `adc_cali_create_scheme_curve_fitting()`
   - Fall back to line fitting via `adc_cali_create_scheme_line_fitting()`
   - If neither is available: `calibration_enabled_` remains `false`

Calibration is optional — the driver works without it and returns raw ADC values only.

## Polling

Each poll cycle:
1. Read the raw ADC value via `adc_oneshot_read()`
2. Record the raw value as `kAdcRaw`
3. If calibration is available: convert to millivolts via `adc_cali_raw_to_voltage()` and record as `kVoltageMv`

## Measurements

| Measurement | ValueKind | Unit | Condition |
|-------------|-----------|------|-----------|
| Raw ADC reading | `kAdcRaw` | — | Always |
| Calibrated voltage | `kVoltageMv` | mV | Only when calibration is available |

## Notes

- **The driver reports raw ADC or voltage, not NO₂ concentration.** Converting the output to ppm requires knowledge of the individual sensor's sensitivity (specified in the datasheet, typically in nA/ppm) and the signal conditioning circuit (load resistor value). This calculation is not implemented in the firmware.
- ADC calibration on ESP32-S3 supports two methods: curve fitting (more accurate, requires eFuse calibration burned during chip manufacturing) and line fitting (universal fallback). If neither is available, only `kAdcRaw` is returned.
- Attenuation of 12 dB corresponds to an input range of ~0–3.9 V, suitable for most analog circuits powered at 3.3 V.

## Recommended poll interval

Minimum 5 seconds. Electrochemical sensors have a slow response — more frequent reads do not improve accuracy.

## Component

`esp_adc` (built into ESP-IDF)

## Source files

- `firmware/main/src/sensors/drivers/me3_no2_sensor.cpp`
- `firmware/main/include/air360/sensors/drivers/me3_no2_sensor.hpp`
