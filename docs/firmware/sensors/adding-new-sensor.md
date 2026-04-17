# Adding A New Sensor

## Status

Implemented workflow. Keep this checklist aligned with the current sensor registry, runtime model, and documentation set.

## Scope

This guide explains the minimum implementation and documentation work needed to add a new sensor driver to the Air360 firmware.

## Source of truth in code

- `firmware/main/include/air360/sensors/sensor_types.hpp`
- `firmware/main/src/sensors/sensor_registry.cpp`
- `firmware/main/src/sensors/sensor_manager.cpp`
- `firmware/main/src/sensors/drivers/`
- `firmware/main/src/web_server.cpp`

## Read next

- [supported-sensors.md](supported-sensors.md)
- [README.md](README.md)
- [../transport-binding.md](../transport-binding.md)

## Typical touch points

Most new sensor work touches these areas:

1. `firmware/main/include/air360/sensors/sensor_types.hpp`
2. `firmware/main/include/air360/sensors/drivers/<sensor>_sensor.hpp`
3. `firmware/main/src/sensors/drivers/<sensor>_sensor.cpp`
4. `firmware/main/src/sensors/sensor_registry.cpp`
5. `firmware/main/src/sensors/sensor_manager.cpp` only if lifecycle hooks or shared runtime behavior change
6. `firmware/main/src/web_server.cpp` if the UI needs new hints, labels, categories, or validation paths
7. `docs/firmware/sensors/<sensor>.md`
8. `docs/firmware/sensors/README.md`
9. `docs/firmware/sensors/supported-sensors.md`

## Implementation checklist

1. Add a new `SensorType` enum value in `sensor_types.hpp`.
2. Define the driver interface in `main/include/air360/sensors/drivers/`.
3. Implement the driver in `main/src/sensors/drivers/`.
4. Register creation, defaults, and validation in `sensor_registry.cpp`.
5. Confirm the transport model fits an existing Air360 path:
   - I2C via `I2cBusManager`
   - UART via `UartPortManager`
   - GPIO / ADC via the shared board slots
6. If the transport does not fit the current model, update `transport-binding.md` and document the new constraint before merging.
7. Verify how readings map into the existing measurement model and upload adapters.
8. Add or update UI hints if the user needs to choose address, pin, UART, or variant-specific fields.

## Documentation checklist

1. Create or update the per-driver doc in `docs/firmware/sensors/`.
2. Add the driver to the index in [README.md](README.md).
3. Add the driver to the concise matrix in [supported-sensors.md](supported-sensors.md).
4. Update [../configuration-reference.md](../configuration-reference.md) if new editable fields are introduced.
5. Update [../measurement-pipeline.md](../measurement-pipeline.md) if the measurement semantics differ from current assumptions.

## Best reference drivers

Use these existing drivers as starting points:

- `bme280_sensor.cpp`: straightforward I2C climate sensor
- `gps_nmea_sensor.cpp`: UART-backed sensor with parsed structured output
- `dht_sensor.cpp`: GPIO-backed digital sensor
- `me3_no2_sensor.cpp`: analog / ADC-backed sensor
- `sps30_sensor.cpp`: more complex I2C sensor with warm-up and multiple output fields

## Design constraints

- Prefer fitting new hardware into the existing measurement model instead of inventing special-case output semantics.
- Keep transport ownership inside `SensorDriverContext`; do not hard-code board pins in the driver.
- If a driver needs warm-up, retries, or unusual lifecycle behavior, document it in the driver page and review the matching ADRs before extending shared runtime code.
