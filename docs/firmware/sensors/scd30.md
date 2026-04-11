# SCD30

Optical CO₂ sensor (NDIR) with temperature and humidity from Sensirion.

## Transport

- I2C, bus 0 (SDA=GPIO8, SCL=GPIO9)
- Address: `0x61` (fixed)

## Initialization

1. Initialize the `i2cdev` subsystem
2. Initialize the sensor descriptor via `scd30_init_desc()`
3. Verify device presence via `i2c_dev_check_present()`
4. Calculate the measurement interval from `poll_interval_ms`:
   ```
   interval_s = (poll_interval_ms + 999) / 1000
   interval_s = clamp(interval_s, 3, 1799)
   ```
5. Apply the interval via `scd30_set_measurement_interval()`
6. Start continuous measurement via `scd30_trigger_continuous_measurement(altitude=0)`

After initialization `last_error_` is set to `"Waiting for first SCD30 sample."` — this is expected, not a fault condition.

## Polling

Each poll cycle:
1. Query data-ready status via `scd30_get_data_ready_status()`
2. If not ready: return `ESP_OK` with `"Waiting for new SCD30 sample."` in the error field — the measurement is skipped but the driver stays initialized
3. If ready: read CO₂, temperature, and humidity via `scd30_read_measurement()`
4. Validate that no NaN values are returned

## Measurements

| Measurement | ValueKind | Unit |
|-------------|-----------|------|
| CO₂ | `kCo2Ppm` | ppm |
| Temperature | `kTemperatureC` | °C |
| Humidity | `kHumidityPercent` | % |

## Notes

- The sensor operates in **continuous measurement mode** — it generates readings autonomously at the configured interval. The driver polls a data-ready flag and reads only when fresh data is available. At short `poll_interval_ms` values some polls will return no data.
- The measurement interval is derived from `poll_interval_ms` at initialization time. Changing the poll interval through the web UI requires a sensor manager restart (Apply button).
- Altitude is hardcoded to 0 m. The SCD30 can use altitude for pressure compensation — for accurate CO₂ readings at significant elevation this would require a code change.
- The first few readings after power-on may be unstable. NDIR sensors require a warm-up period.
- If reading the data-ready flag or the measurement itself returns an error, `initialized_` is reset.

## Recommended poll interval

Minimum 5 seconds (registry limit). The SCD30 supports intervals from 2 to 1800 seconds — the driver ensures a value of 3–1799 s after conversion.

## Component

`esp-idf-lib__scd30` (managed component)

## Source files

- `firmware/main/src/sensors/drivers/scd30_sensor.cpp`
- `firmware/main/include/air360/sensors/drivers/scd30_sensor.hpp`
