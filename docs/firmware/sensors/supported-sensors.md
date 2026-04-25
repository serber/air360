# Supported Sensors Matrix

## Status

Implemented. This matrix should match the sensor types and transport bindings currently available in `firmware/`.

## Scope

This is the concise support matrix for the current Air360 firmware. It is intended as the fastest way to answer “is sensor X supported, through which transport, and where is its detailed documentation?”

## Source of truth in code

- `firmware/main/include/air360/sensors/sensor_types.hpp`
- `firmware/main/src/sensors/sensor_registry.cpp`
- `firmware/main/src/sensors/drivers/`

## Read next

- [README.md](README.md)
- [adding-new-sensor.md](adding-new-sensor.md)
- [../transport-binding.md](../transport-binding.md)

## Current support matrix

| Sensor type | Transport | Default binding | Allowed binding values | Detail doc |
|-------------|-----------|-----------------|------------------------|------------|
| `BME280` | I2C | Bus 0, address `0x76` | I2C `0x76`, `0x77` | [bme280.md](bme280.md) |
| `BME680` | I2C | Bus 0, address `0x77` | I2C `0x76`, `0x77` | [bme680.md](bme680.md) |
| `SCD30` | I2C | Bus 0, address `0x61` | I2C `0x61` | [scd30.md](scd30.md) |
| `SPS30` | I2C | Bus 0, address `0x69` | I2C `0x69` | [sps30.md](sps30.md) |
| `VEML7700` | I2C | Bus 0, address `0x10` | I2C `0x10` | [veml7700.md](veml7700.md) |
| `HTU2X` | I2C | Bus 0, address `0x40` | I2C `0x40` | [htu2x.md](htu2x.md) |
| `SHT4X` | I2C | Bus 0, address `0x44` | I2C `0x44` | [sht4x.md](sht4x.md) |
| `GPS NMEA` | UART | UART1, RX=`GPIO18`, TX=`GPIO17`, `9600` baud | UART1 or UART2 | [gps_nmea.md](gps_nmea.md) |
| `DHT11` | GPIO | First allowed pin, currently GPIO4 | GPIO4/5/6 | [dht.md](dht.md) |
| `DHT22` | GPIO | First allowed pin, currently GPIO4 | GPIO4/5/6 | [dht.md](dht.md) |
| `DS18B20` | GPIO / 1-Wire | First allowed pin, currently GPIO4 | GPIO4/5/6 | [ds18b20.md](ds18b20.md) |
| `ME3-NO2` | Analog / ADC | First allowed pin, currently GPIO4 | GPIO4/5/6 | [me3_no2.md](me3_no2.md) |
| `INA219` | I2C | Bus 0, address `0x40` | I2C `0x40`, `0x41`, `0x44`, `0x45` | [ina219.md](ina219.md) |
| `MH-Z19B` | UART | UART2, RX=`GPIO16`, TX=`GPIO15`, `9600` baud | UART1 or UART2 | [mhz19b.md](mhz19b.md) |

## Peripheral note

`SIM7600E` is documented alongside sensors because it shares hardware and configuration context, but it is not registered through the sensor pipeline. See [sim7600e.md](sim7600e.md) and [../cellular-manager.md](../cellular-manager.md).
