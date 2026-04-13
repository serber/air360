# Supported Sensors

| Sensor | Category | Transport | Pins |
|--------|----------|-----------|------|
| BME280 | Climate | I2C at 0x76 (alt 0x77) | SDA=GPIO8, SCL=GPIO9 |
| BME680 | Climate | I2C at 0x77 (alt 0x76) | SDA=GPIO8, SCL=GPIO9 |
| SHT4X | Temperature & Humidity | I2C at 0x44 | SDA=GPIO8, SCL=GPIO9 |
| HTU2X | Temperature & Humidity | I2C at 0x40 | SDA=GPIO8, SCL=GPIO9 |
| DHT11 | Temperature & Humidity | GPIO | GPIO4 / GPIO5 / GPIO6 |
| DHT22 | Temperature & Humidity | GPIO | GPIO4 / GPIO5 / GPIO6 |
| DS18B20 | Temperature | GPIO (1-Wire) | GPIO4 / GPIO5 / GPIO6 |
| SCD30 | CO2 | I2C at 0x61 | SDA=GPIO8, SCL=GPIO9 |
| VEML7700 | Light | I2C at 0x10 | SDA=GPIO8, SCL=GPIO9 |
| SPS30 | Particulate Matter | I2C at 0x69 | SDA=GPIO8, SCL=GPIO9 |
| GPS (NMEA) | Location | UART1 at 9600 baud | RX=GPIO18, TX=GPIO17 |
| ME3-NO2 | Gas | Analog (ADC) | GPIO4 / GPIO5 / GPIO6 |

I2C bus 0: SDA=GPIO8, SCL=GPIO9, 100 kHz. All GPIO pin assignments are configurable via `Kconfig`.

GPIO sensor slots (GPIO4/5/6): one sensor per slot. Shared across DHT11, DHT22, DS18B20, and ME3-NO2.

---

## Cellular Modem

The SIM7600E modem provides a cellular PPP uplink. It is not a sensor — it is managed by `CellularManager` independently of the sensor pipeline.

| Device | Role | Transport | Default Pins |
|--------|------|-----------|-------------|
| SIM7600E | Cellular uplink (PPP) | UART1 at 115200 baud | RX=GPIO18, TX=GPIO17, PWRKEY=GPIO12, SLEEP/DTR=GPIO21 |

> **Note:** The default modem UART (UART1, GPIO18/17) conflicts with the GPS default. GPS and cellular cannot be used simultaneously on the default pin assignment. Reconfigure one of them via `Kconfig` (`CONFIG_AIR360_CELLULAR_DEFAULT_RX_GPIO` / `CONFIG_AIR360_CELLULAR_DEFAULT_TX_GPIO` or `CONFIG_AIR360_GPS_DEFAULT_RX_GPIO` / `CONFIG_AIR360_GPS_DEFAULT_TX_GPIO`).

All modem GPIO assignments are configurable at runtime via the Device configuration page and stored in `CellularConfig` (NVS key `cellular_cfg`). The values above are the compiled-in defaults from `Kconfig.projbuild`.
