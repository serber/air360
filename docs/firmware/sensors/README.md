# Supported Sensors

## Status

Implemented. Keep this index aligned with the sensor types and driver files currently present in `firmware/`.

## Scope

This document is the detailed sensor documentation index and hardware reference for the Air360 firmware.

## Source of truth in code

- `firmware/main/include/air360/sensors/sensor_types.hpp`
- `firmware/main/src/sensors/sensor_registry.cpp`
- `firmware/main/src/sensors/drivers/`

## Read next

- [supported-sensors.md](supported-sensors.md)
- [adding-new-sensor.md](adding-new-sensor.md)
- [../transport-binding.md](../transport-binding.md)

Reference index for every sensor driver implemented in `firmware/main/src/sensors/drivers/`.

Use [supported-sensors.md](supported-sensors.md) for the concise matrix and [adding-new-sensor.md](adding-new-sensor.md) for the implementation checklist.

## Sensor Index

| File | Sensor | Transport | Default binding | Measurements |
|------|--------|-----------|-----------------|--------------|
| [bme280.md](bme280.md) | BME280 | I2C | Bus 0, `0x76` (alt `0x77`), SDA=`GPIO8`, SCL=`GPIO9` | Temperature, humidity, pressure |
| [bme680.md](bme680.md) | BME680 | I2C | Bus 0, `0x77` (alt `0x76`), SDA=`GPIO8`, SCL=`GPIO9` | Temperature, humidity, pressure, gas resistance |
| [scd30.md](scd30.md) | SCD30 | I2C | Bus 0, `0x61`, SDA=`GPIO8`, SCL=`GPIO9` | CO2, temperature, humidity |
| [sps30.md](sps30.md) | SPS30 | I2C | Bus 0, `0x69`, SDA=`GPIO8`, SCL=`GPIO9` | PM1.0-PM10.0 mass and number concentrations, typical particle size |
| [sds011.md](sds011.md) | SDS011 | UART | Default UART2, RX=`GPIO16`, TX=`GPIO15`, `9600` baud; UART1 selectable | PM2.5 and PM10 mass concentrations |
| [veml7700.md](veml7700.md) | VEML7700 | I2C | Bus 0, `0x10`, SDA=`GPIO8`, SCL=`GPIO9` | Illuminance |
| [htu2x.md](htu2x.md) | HTU2X | I2C | Bus 0, `0x40`, SDA=`GPIO8`, SCL=`GPIO9` | Temperature, humidity |
| [sht3x.md](sht3x.md) | SHT3X | I2C | Bus 0, `0x44` (alt `0x45`), SDA=`GPIO8`, SCL=`GPIO9` | Temperature, humidity |
| [sht4x.md](sht4x.md) | SHT4X | I2C | Bus 0, `0x44`, SDA=`GPIO8`, SCL=`GPIO9` | Temperature, humidity |
| [gps_nmea.md](gps_nmea.md) | GPS (NMEA) | UART | Default UART1, RX=`GPIO18`, TX=`GPIO17`, `9600` baud; UART2 selectable | Latitude, longitude, altitude, satellites, speed, course, HDOP |
| [dht.md](dht.md) | DHT11 / DHT22 | GPIO | Descriptor allowed pins: `GPIO4`, `GPIO5`, `GPIO6` | Temperature, humidity |
| [ds18b20.md](ds18b20.md) | DS18B20 | GPIO / 1-Wire | Descriptor allowed pins: `GPIO4`, `GPIO5`, `GPIO6` | Temperature |
| [me3_no2.md](me3_no2.md) | ME3-NO2 | Analog (ADC) | Descriptor allowed pins: `GPIO4`, `GPIO5`, `GPIO6` | Raw ADC, voltage |
| [ina219.md](ina219.md) | INA219 | I2C | Bus 0, `0x40` (alt `0x41`, `0x44`, `0x45`), SDA=`GPIO8`, SCL=`GPIO9` | Bus voltage, current, power |
| [mhz19b.md](mhz19b.md) | MH-Z19B | UART | Default UART2, RX=`GPIO16`, TX=`GPIO15`, `9600` baud; UART1 selectable | CO2 |

I2C bus 0 is fixed to SDA=`GPIO8`, SCL=`GPIO9` at `100 kHz`.

GPIO/analog sensor pins are listed per sensor descriptor. The current DHT11, DHT22, DS18B20, and ME3-NO2 descriptors allow `GPIO4`, `GPIO5`, and `GPIO6`; only one sensor can occupy a pin at a time.

## Datasheet Notes

- The firmware tree is the source of truth for supported sensor types and transport bindings.
- The hardware notes below use public manufacturer sources where possible.
- Accuracy is kept in the manufacturer's native form. Some vendors use `%RH`, `% m.v.`, or `% output value`; others specify absolute error in `deg C`, `ppm`, `hPa`, or `m`.
- When a public manufacturer page or datasheet does not publish a clean service-life or maximum-current number, the table says `Not stated`.
- `HTU2X` and `SHT4X` are sensor families in Air360. Where the firmware accepts a family rather than one exact part number, the notes use the closest family reference part and call that out explicitly.
- `SHT3X` covers the digital I2C SHT30/SHT31/SHT35 family through the common `esp-idf-lib/sht3x` driver.

## Sensor Hardware Reference

### BME280

| Field | Value |
|-------|-------|
| Manufacturer | Bosch Sensortec |
| Air360 measurements | Temperature, humidity, pressure |
| Declared service life | Not stated in the public Bosch datasheet or product page |
| Operating temperature | `-40..85 deg C` (`0..65 deg C` full accuracy) |
| Supply voltage | `VDD 1.71..3.6 V`, `VDDIO 1.2..3.6 V` |
| Accuracy | Humidity `+-3 %RH`; pressure sensitivity error `+-0.25 %`; temperature `+-0.5 deg C` at `25 deg C` |
| Maximum current | `714 uA` during pressure measurement; `350 uA` during temperature measurement; `340 uA` during humidity measurement |
| Reference links | [Bosch datasheet](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bme280-ds002.pdf) |

### BME680

| Field | Value |
|-------|-------|
| Manufacturer | Bosch Sensortec |
| Air360 measurements | Temperature, humidity, pressure, gas resistance |
| Declared service life | Not stated as a numeric lifetime in the public Bosch datasheet; Bosch instead documents long-term stability and field qualification |
| Operating temperature | `-40..85 deg C` (`0..65 deg C` full accuracy for pressure) |
| Supply voltage | `VDD 1.71..3.6 V`, `VDDIO 1.2..3.6 V` |
| Accuracy | Humidity `+-3 %RH`; pressure `+-0.6 hPa` absolute / `+-0.12 hPa` relative; temperature `+-0.5 deg C` at `25 deg C`, `+-1.0 deg C` over `0..65 deg C`; gas sensor deviation `+-15 %` |
| Maximum current | Up to `18 mA` peak when the gas hotplate starts; up to `13 mA` during heater operation |
| Reference links | [Bosch datasheet](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bme680-ds001.pdf) |

### HTU2X

| Field | Value |
|-------|-------|
| Manufacturer | TE Connectivity / Measurement Specialties |
| Air360 measurements | Temperature, humidity |
| Reference part used here | HTU21D, because the firmware treats `HTU2X` as the HTU21D-compatible I2C family |
| Declared service life | Not stated in the public TE product pages used here |
| Operating temperature | `-40..125 deg C` |
| Supply voltage | Public TE product pages surface `3.8 V` peak supply |
| Accuracy | TE surfaces this family as a high-accuracy RH/T sensor; the public product-page extract available here does not expose a single percentage figure for the bare sensor |
| Maximum current | `0.014 mA` on the TE product page |
| Reference links | [TE product family page](https://www.te.com/en/product-CAT-HSC0004.html), [TE part page](https://www.te.com/en/product-HPP845E031R5.html) |

### SHT3X

| Field | Value |
|-------|-------|
| Manufacturer | Sensirion |
| Air360 measurements | Temperature, humidity |
| Supported family | `SHT30` / `SHT31` / `SHT35` digital I2C family via the common `SHT3X` driver |
| Declared service life | Not stated as a numeric lifetime in the public datasheet |
| Operating temperature | `-40..125 deg C` |
| Supply voltage | `2.15..5.5 V` |
| Accuracy | Humidity typically up to `+-1.5 %RH`; temperature typically up to `+-0.1 deg C` for SHT35-class parts. Exact tolerance depends on the selected SHT30/SHT31/SHT35 variant and operating range. |
| Maximum current | Public datasheet lists measurement-current behavior by mode rather than one Air360-wide max-current value; Air360 uses single-shot polling through the component. |
| Reference links | [Sensirion SHT3x-DIS datasheet](https://sensirion.com/media/documents/213E6A3B/63A5A569/Datasheet_SHT3x_DIS.pdf), [ESP component documentation](https://esp-idf-lib.github.io/sht3x/) |

### SHT4X

| Field | Value |
|-------|-------|
| Manufacturer | Sensirion |
| Air360 measurements | Temperature, humidity |
| Supported family | `SHT40` / `SHT41` / `SHT43` / `SHT45` family via the common `SHT4X` driver |
| Declared service life | Not stated as a numeric lifetime in the public datasheet |
| Operating temperature | `-40..125 deg C` |
| Supply voltage | `1.08..3.6 V` |
| Accuracy | Humidity `+-1.8 %RH` typical for `SHT40` / `SHT41` / `SHT43`, `+-1.0 %RH` typical for `SHT45`; temperature `+-0.2 deg C` typical for `SHT40` / `SHT41`, `+-0.1 deg C` typical for `SHT45` |
| Maximum current | `500 uA` max during measurement with heater off; heater modes can reach `100 mA`, but Air360 keeps the heater disabled |
| Reference links | [Sensirion product page](https://developer.sensirion.com/products-support/sht4x-humidity-and-temperature-sensor), [Sensirion datasheet](https://sensirion.com/media/documents/33FD6951/661CD142/HT_DS_Datasheet_SHT4x.pdf) |

### DHT11

| Field | Value |
|-------|-------|
| Manufacturer | Aosong / ASAIR |
| Air360 measurements | Temperature, humidity |
| Declared service life | Not stated; the current datasheet instead recommends use within one year after delivery for humidity-sensitivity handling |
| Operating temperature | `-20..60 deg C` |
| Supply voltage | `3.3..5.5 V` |
| Accuracy | Humidity `+-5 %RH`; temperature `+-2 deg C` |
| Maximum current | The current public datasheet exposes `1 mA` typical during measurement; a separate explicit max-current figure is not surfaced |
| Reference links | [ASAIR DHT11 datasheet](https://www.aosong.com/userfiles/files/media/DHT11%E6%B8%A9%E6%B9%BF%E5%BA%A6%E4%BC%A0%E6%84%9F%E5%99%A8%E8%AF%B4%E6%98%8E%E4%B9%A6%EF%BC%88%E4%B8%AD%EF%BC%89%20A0-1208.pdf) |

### DHT22

| Field | Value |
|-------|-------|
| Manufacturer | Aosong / ASAIR |
| Air360 measurements | Temperature, humidity |
| Reference part used here | AM2302 / DHT22 class sensor |
| Declared service life | Not stated as a numeric operating life in the public AM2302 manual |
| Operating temperature | `-40..80 deg C` |
| Supply voltage | `3.3..6.0 V` |
| Accuracy | The current AM2302 manual gives `+-2 %RH` as the typical humidity error and publishes humidity / temperature error envelopes rather than one single unified accuracy number for every condition |
| Maximum current | Not stated clearly in the searchable manufacturer material used here |
| Reference links | [ASAIR AM2302 technical manual](https://www.aosong.com/uploadfiles/2025/04/20250417105409216.pdf) |

### DS18B20

| Field | Value |
|-------|-------|
| Manufacturer | Analog Devices (original Maxim part family) |
| Air360 measurements | Temperature |
| Declared service life | Not stated in the public product page or datasheet |
| Operating temperature | `-55..125 deg C` |
| Supply voltage | `3.0..5.5 V`, or parasite-power operation on the 1-Wire bus |
| Accuracy | Temperature `+-0.5 deg C` from `-10..85 deg C` |
| Maximum current | Up to approximately `1.5 mA` during temperature conversion |
| Reference links | [Analog Devices product page](https://www.analog.com/en/products/ds18b20par.html), [Analog Devices datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/ds18b20.pdf) |

### SCD30

| Field | Value |
|-------|-------|
| Manufacturer | Sensirion |
| Air360 measurements | CO2, temperature, humidity |
| Declared service life | `15 years` |
| Operating temperature | CO2 sensor operating conditions `0..50 deg C`; integrated RH/T sensing range `-40..70 deg C` |
| Supply voltage | `3.3..5.5 V` |
| Accuracy | CO2 `+-(30 ppm + 3 %)`, humidity `+-3 %RH`, temperature `+-(0.4 deg C + 0.023 x (T - 25 deg C))` in `0..50 deg C` |
| Maximum current | `75 mA` during measurement (`19 mA` average at one measurement per `2 s`) |
| Reference links | [Sensirion SCD30 datasheet](https://sensirion.com/media/documents/4EAF6AF8/61652C3C/Sensirion_CO2_Sensors_SCD30_Datasheet.pdf) |

### VEML7700

| Field | Value |
|-------|-------|
| Manufacturer | Vishay Semiconductors |
| Air360 measurements | Illuminance |
| Declared service life | Not stated in the public product page or datasheet |
| Operating temperature | `-25..85 deg C` |
| Supply voltage | `2.5..3.6 V` |
| Accuracy | Vishay documents the part as a high-accuracy ALS and publishes ambient-light range / resolution, but the searchable public datasheet text used here does not expose one clean percentage-accuracy figure |
| Maximum current | `45 uA` in the most active measurement mode shown in the datasheet (`8 uA` in a lower-power mode, `0.5 uA` in shutdown) |
| Reference links | [Vishay product page](https://www.vishay.com/en/product/84286/tab/technical-questions/), [Vishay datasheet](https://www.vishay.com/docs/84286/veml7700.pdf) |

### SPS30

| Field | Value |
|-------|-------|
| Manufacturer | Sensirion |
| Air360 measurements | PM1.0, PM2.5, PM4, PM10 mass concentration; particle number concentration; typical particle size |
| Declared service life | `>10 years` at `24 h/day` operation |
| Operating temperature | `-10..60 deg C` absolute operating range; Sensirion recommends `10..40 deg C` for best performance |
| Supply voltage | `4.5..5.5 V` |
| Accuracy | PM1 / PM2.5 mass concentration `+-[5 ug/m3 + 5 % m.v.]` from `0..100 ug/m3`, then `+-10 % m.v.` from `100..1000 ug/m3`; PM4 / PM10 are specified less tightly (`+-25 ug/m3` or `+-25 % m.v.` depending on range) |
| Maximum current | `80 mA` max during the first `200 ms` fan start; `65 mA` max in measurement mode |
| Reference links | [Sensirion SPS30 datasheet](https://sensirion.com/media/documents/8600FF88/64A3B8D6/Sensirion_PM_Sensors_Datasheet_SPS30.pdf) |

### SDS011

| Field | Value |
|-------|-------|
| Manufacturer | Nova Fitness Co., Ltd. |
| Air360 measurements | PM2.5 and PM10 mass concentration |
| Declared service life | `8000 hours` under continuous operation |
| Operating temperature | `-10..50 deg C` |
| Supply voltage | `4.7..5.3 V` |
| Accuracy | Relative error `+/-15 %` and `+/-10 ug/m3` at `25 deg C`, `50 %RH` |
| Maximum current | `70 mA +/-10 mA` during operation; sleep current below `4 mA` for laser and fan sleep |
| Air360 mode | Wakes on init/poll, continuous work period, passive/query reporting |
| Reference links | [Nova Fitness datasheet mirror](https://microcontrollerslab.com/wp-content/uploads/2020/12/NonA-PM-SDS011-Dust-sensor-datasheet.pdf), [Nettigo product page](https://nettigo.eu/products/1085) |

### GPS (NMEA)

| Field | Value |
|-------|-------|
| Manufacturer | Module-dependent |
| Air360 measurements | Latitude, longitude, altitude, satellites, speed, course, HDOP |
| Declared service life | Module-dependent |
| Operating temperature | Module-dependent |
| Supply voltage | Module-dependent |
| Accuracy | Module-dependent |
| Maximum current | Module-dependent |
| Reference links | Air360 intentionally supports a generic NMEA-over-UART receiver rather than one fixed GNSS module. Add manufacturer references only after the hardware BOM selects a concrete GPS part number. |

### ME3-NO2

| Field | Value |
|-------|-------|
| Manufacturer | Zhengzhou Winsen Electronics Technology Co., Ltd. |
| Air360 measurements | Raw ADC value and calibrated voltage; the firmware does not yet convert this to NO2 concentration |
| Declared service life | `2 years` in air |
| Operating temperature | The public manual excerpt used here does not expose one clean operating-temperature band; it does give zero-drift data for `-20..40 deg C` and storage temperature `-20..50 deg C` |
| Supply / bias | `0 mV` bias, recommended `10 ohm` load resistance; this electrochemical cell is read through external analog front-end circuitry rather than powered like a digital sensor |
| Accuracy | Repeatability `<2 %` of output value; stability `<2 % / month` |
| Maximum current | Not stated; the public Winsen manual describes the sensor as low-consumption and specifies sensitivity in `uA/ppm` rather than supply current |
| Reference links | [Winsen product page](https://de.winsen-sensor.com/product/me3-no2.html?lang=tr), [Winsen manual](https://www.winsen-sensor.com/d/files/4-series-electrochemical-toxic-gas-sensor/me3-no2/me3-no2-0-20ppm.pdf) |

### INA219

| Field | Value |
|-------|-------|
| Manufacturer | Texas Instruments |
| Air360 measurements | Bus voltage, current, power |
| Declared service life | Not stated in the public datasheet |
| Operating temperature | `-40..125 deg C` |
| Supply voltage | `2.7..5.5 V` |
| Accuracy | Voltage `0.5 %` full-scale; current accuracy depends on shunt resistor tolerance |
| Maximum current | `1 mA` quiescent; shunt current limited by gain setting (±3.2 A with 100 mΩ shunt at `INA219_GAIN_0_125`) |
| Reference links | [TI product page](https://www.ti.com/product/INA219), [TI datasheet](https://www.ti.com/lit/ds/symlink/ina219.pdf) |

### MH-Z19B

| Field | Value |
|-------|-------|
| Manufacturer | Zhengzhou Winsen Electronics Technology Co., Ltd. |
| Air360 measurements | CO2 |
| Declared service life | `5 years` |
| Operating temperature | `0..50 deg C` |
| Supply voltage | `4.5..5.5 V` (Vin); UART logic levels `3.3 V` |
| Accuracy | `±(50 ppm + 5 % of reading)` |
| Maximum current | `150 mA` peak during heating; `60 mA` typical |
| Reference links | [Winsen product page](https://www.winsen-sensor.com/sensors/co2-sensor/mh-z19b.html), [Winsen datasheet](https://www.winsen-sensor.com/d/files/infrared-gas-sensor/mh-z19b-co2-ver1_0.pdf) |

## Peripheral Hardware

The SIM7600E modem is not a sensor. It is managed by `CellularManager` independently of the sensor pipeline.

| File | Device | Transport | Role |
|------|--------|-----------|------|
| [sim7600e.md](sim7600e.md) | SIM7600E | UART1 (RX=`GPIO18`, TX=`GPIO17`) | Cellular PPP uplink; built-in GNSS is not yet used by the firmware |

> The default modem UART conflicts with the default GPS UART. GPS and cellular cannot be used simultaneously on the stock `GPIO17` / `GPIO18` wiring unless the GPS sensor is moved to UART2 in the Sensor Configuration page or the modem UART is reconfigured.
