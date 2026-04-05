# Planned Device Support

Planning inventory for hardware that Air360 may support later.

This file is not a record of implemented support. For the current runtime, use [`sensors.md`](sensors.md).

Status:
- `planned`
- `investigating`
- `in_progress`
- `implemented`
- `deferred`
- `dropped`

## Sensors

The sensor table below follows the same category semantics and ordering used by the current firmware UI where possible. Categories that do not yet exist in the implemented UI, such as `Light` and `Radiation`, are listed after the current runtime categories.

| Category | Device | Transport | Outputs | Priority | Status | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| `Climate` | BMP280 | i2c | `temperature`, `pressure` | medium | planned | Fallback when humidity is not needed. |
| `Climate` | BMP180 | i2c | `temperature`, `pressure` | low | planned | Older Bosch pressure sensor. |
| `Temperature / Humidity` | SHT3x | i2c | `temperature`, `humidity` | high | planned | Strong candidate for temp/humidity support. |
| `Temperature / Humidity` | HTU21D | i2c | `temperature`, `humidity` | medium | planned | Environmental sensor candidate. |
| `Temperature / Humidity` | DS18B20 | gpio / 1-wire | `temperature` | medium | planned | Simple external temperature probe. |
| `Air Quality` | CCS811 | i2c | `tvoc`, `eco2` | medium | planned | Same class of outputs as ENS160. |
| `Air Quality` | SCD30 | i2c | `co2_ppm`, `temperature`, `humidity` | high | planned | Important CO2-class sensor candidate. |
| `Particulate Matter` | Plantower PMS Series | uart | `pm1_0`, `pm2_5`, `pm10_0` | high | planned | PMSx003 / PMS7003-class modules. |
| `Light` | OPT3001 | i2c | `illuminance_lux` | medium | planned | Candidate ambient light sensor. |
| `Light` | VEML6030 | i2c | `illuminance_lux` | medium | planned | Candidate ambient light sensor. |
| `Light` | VEML7700 | i2c | `illuminance_lux` | medium | planned | Candidate ambient light sensor. |
| `Light` | VEML6070 | i2c | `uv_index` or raw UV | low | planned | UV-focused, not a general ALS replacement. |
| `Radiation` | Radiation SBM-19 | gpio / pulse | `cpm`, `radiation_level` | medium | planned | Geiger tube integration candidate. |
| `Radiation` | Radiation SBM-20 | gpio / pulse | `cpm`, `radiation_level` | medium | planned | Geiger tube integration candidate. |
| `Radiation` | Radiation Si22G | gpio / pulse | `cpm`, `radiation_level` | low | planned | Alternative radiation sensing candidate. |

## Peripherals

| Device | Category | Transport | Function | Priority | Status | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| Display support | platform feature | i2c / spi / gpio | local status UI | medium | planned | Separate workstream from sensor drivers. |

## Connectivity Modules

| Device | Category | Transport | Function | Priority | Status | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| SIM7600 | mobile uplink | uart | send data without Wi-Fi | high | planned | Preferred cellular module candidate. |

## Light Sensor Notes

- `OPT3001` or `VEML6030`: cleaner long-term choices.
- `VEML7700`: good if module availability matters.
- `BH1750`: easy to source, but weaker long-term choice.
