# Sensor Drivers

Documentation for each sensor driver implemented in `firmware/main/src/sensors/drivers/`.

Each file covers transport configuration, initialization sequence, polling logic, reported measurements, and implementation-specific notes.

## Index

| File | Sensor | Transport | Measurements |
|------|--------|-----------|--------------|
| [bme280.md](bme280.md) | BME280 | I2C `0x76` | Temperature, humidity, pressure |
| [bme680.md](bme680.md) | BME680 | I2C `0x77` | Temperature, humidity, pressure, gas resistance |
| [scd30.md](scd30.md) | SCD30 | I2C `0x61` | CO₂, temperature, humidity |
| [sps30.md](sps30.md) | SPS30 | I2C `0x69` | PM1.0–PM10.0 mass & number concentrations, typical particle size |
| [veml7700.md](veml7700.md) | VEML7700 | I2C `0x10` | Illuminance |
| [htu2x.md](htu2x.md) | HTU2X | I2C `0x40` | Temperature, humidity |
| [sht4x.md](sht4x.md) | SHT4X | I2C `0x44` | Temperature, humidity |
| [gps_nmea.md](gps_nmea.md) | GPS (NMEA) | UART1 | Latitude, longitude, altitude, satellites, speed, course, HDOP |
| [dht.md](dht.md) | DHT11 / DHT22 | GPIO | Temperature, humidity |
| [ds18b20.md](ds18b20.md) | DS18B20 | GPIO (1-Wire) | Temperature |
| [me3_no2.md](me3_no2.md) | ME3-NO2 | Analog (ADC) | Raw ADC, voltage |
