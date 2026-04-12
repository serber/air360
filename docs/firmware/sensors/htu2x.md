# HTU2X

Temperature and humidity sensor from the HTU2x family (HTU21D and compatible) by TE Connectivity. The driver uses the `si7021` component, which is protocol-compatible with HTU2X.

## Transport

- I2C, bus 0 (SDA=GPIO8, SCL=GPIO9)
- Address: `0x40` (fixed for the entire HTU2X family)
- Clock: 100 kHz, pull-ups enabled

## Initialization

1. Resolve bus pins via `context.i2c_bus_manager->resolvePins()`
2. Initialize the sensor descriptor via `si7021_init_desc()` (passes resolved port and GPIO pins)
3. Delay **1000 ms** — some HTU21D variants silently ignore the first I2C transaction after power-on
4. Reset the sensor via `si7021_reset()`

## Polling

Each poll cycle:
1. Read temperature via `si7021_measure_temperature()`
2. Read humidity via `si7021_measure_humidity()`
3. Validate that no NaN values are returned

Temperature and humidity are read in two separate calls (not simultaneous).

## Measurements

| Measurement | ValueKind | Unit |
|-------------|-----------|------|
| Temperature | `kTemperatureC` | °C |
| Humidity | `kHumidityPercent` | % |

## Notes

- The 1-second startup delay is hardcoded in the driver to compensate for HTU21D modules that drop the first I2C transaction after boot. This delay happens once during `SensorManager::applyConfig()`, not on every poll.
- Temperature and humidity are acquired by two sequential commands. Each command includes its own internal conversion delay inside the library.
- `si7021` and HTU2X are software-compatible and share the same I2C command set.
- If either read call returns an error or a NaN value, `initialized_` is reset.

## Recommended poll interval

Minimum 5 seconds.

## Component

`esp-idf-lib__si7021` (managed component)

## Source files

- `firmware/main/src/sensors/drivers/htu2x_sensor.cpp`
- `firmware/main/include/air360/sensors/drivers/htu2x_sensor.hpp`
