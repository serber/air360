# Supported Sensors

| Sensor | Category | Transport | Address / Pin |
|--------|----------|-----------|---------------|
| BME280 | Climate | I2C bus 0 | `0x76` (alt `0x77`) |
| BME680 | Climate | I2C bus 0 | `0x77` (alt `0x76`) |
| SHT4X | Temperature & Humidity | I2C bus 0 | `0x44` |
| HTU2X | Temperature & Humidity | I2C bus 0 | `0x40` |
| DHT11 | Temperature & Humidity | GPIO | GPIO4 / GPIO5 / GPIO6 |
| DHT22 | Temperature & Humidity | GPIO | GPIO4 / GPIO5 / GPIO6 |
| DS18B20 | Temperature | GPIO (1-Wire) | GPIO4 / GPIO5 / GPIO6 |
| SCD30 | CO2 | I2C bus 0 | `0x61` |
| VEML7700 | Light | I2C bus 0 | `0x10` |
| SPS30 | Particulate Matter | I2C bus 0 | `0x69` |
| GPS (NMEA) | Location | UART1 | RX GPIO18, TX GPIO17 |
| ME3-NO2 | Gas | Analog (ADC) | GPIO4 / GPIO5 / GPIO6 |

I2C bus 0: SDA = GPIO8, SCL = GPIO9. All GPIO pin assignments are configurable via `Kconfig`.
