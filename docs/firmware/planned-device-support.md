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

| Device | Category | Transport | Outputs | Priority | Status | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| Plantower PMS Series | PM | uart | `pm1_0`, `pm2_5`, `pm10_0` | high | planned | PMSx003 / PMS7003-class modules. |
| DS18B20 | temperature | gpio / 1-wire | `temperature` | medium | planned | Simple external temperature probe. |
| OPT3001 | ambient light | i2c | `illuminance_lux` | medium | planned | Candidate ambient light sensor. |
| VEML6030 | ambient light | i2c | `illuminance_lux` | medium | planned | Candidate ambient light sensor. |
| VEML7700 | ambient light | i2c | `illuminance_lux` | medium | planned | Candidate ambient light sensor. |
| VEML6070 | UV / light | i2c | `uv_index` or raw UV | low | planned | UV-focused, not a general ALS replacement. |
| CCS811 | gas / IAQ | i2c | `tvoc`, `eco2` | medium | planned | Same class of outputs as ENS160. |
| SHT3x | environmental | i2c | `temperature`, `humidity` | high | planned | Strong candidate for temp/humidity support. |
| HTU21D | environmental | i2c | `temperature`, `humidity` | medium | planned | Environmental sensor candidate. |
| BMP280 | pressure | i2c | `temperature`, `pressure` | medium | planned | Fallback when humidity is not needed. |
| BMP180 | pressure | i2c | `temperature`, `pressure` | low | planned | Older Bosch pressure sensor. |
| SCD30 | CO2 / environmental | i2c | `co2_ppm`, `temperature`, `humidity` | high | planned | Important CO2-class sensor candidate. |

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
