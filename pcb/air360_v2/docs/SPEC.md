# Air360 V2 — PCB specification

Board revision V2.2. Decisions and evidence are in [DESIGN-RECORD.md](DESIGN-RECORD.md).

## 1. Purpose

A single ESP32-S3 board that replaces the [Air360 shield](https://oshwlab.com/serber2006/air360) with an ESP32-S3 DevKit on top. It must run the existing Air360 firmware unchanged, power external sensors and a modem, and fit the shield's cables and mounting.

- Do not change GPIO assignments, connector functions or pin order to simplify routing. Settings through existing configuration interfaces are acceptable; firmware source changes are not.
- No ordering or purchasing without a separate instruction.

## 2. ESP32-S3 and DevKitC functions

- ESP32-S3-WROOM-1-N16R8 on the board itself, not a DevKit on top.
- DevKitC-equivalent power, decoupling, reset, BOOT, programming and debug, mapped function by function. No second USB connector or bridge without a reason in that mapping.
- Module antenna placement and keep-outs per Espressif. The modem antenna is separate.
- Pins taken by the module's internal flash and PSRAM are not exposed; document the restriction.

## 3. Power

| Source | Requirement |
|---|---|
| DC (J3, J4) | 2S battery only, Li-ion (8.4 V full) or LiFePO4 (7.3 V full), about 6–8.4 V. Buck to regulated +5V, 3V3 from +5V. Reverse polarity protection. 5 V at DC must not start the board or damage it. A 12 V adapter by mistake must not damage it; it is not an operating mode |
| USB-C (J1) | 5 V only from an ordinary charger, no PD/QC. Works without DC or battery. A computer port for programming is a separate, limited mode |
| Both | Defined behaviour with both connected, in any order. No backfeed into either source or into an unpowered converter |

- +5V target: 1–1.5 A, from field data: about 140 mA without a modem, 250–300 mA with one. A modem with high bursts may need its own supply.
- Tabulate continuous, peak and start-up loads, losses and the limits of connectors, shunt, fuses, tracks and vias. Do not rate the board by its converter rating.
- No battery charging or balancing on the board.

## 4. Protection against restart loops

When the battery runs down, the ESP32 starts, the load sags the supply, it resets, and the cycle repeats. Hardware must stop this:

1. Disconnect the battery-powered load when voltage falls during operation, and do not try to start at low voltage.
2. Restart only at `V_ON > V_OFF`, with margin for start-up sag, tolerances and recovery of an unloaded pack. Add delay or qualification so recovery after load removal cannot start another cycle.
3. Thresholds for 2S only. Chemistry is selected on the board (JP1), not inferred from voltage. A wrong setting must not over-discharge the pack or damage the board.
4. The monitor keeps working while the load is off; account for its own current.
5. A dead or absent battery must not block operation from USB. No parasitic powering of unpowered loads through signal lines.
6. This does not replace cell protection in the pack.

The firmware `power_gate` (deep sleep at boot, bypass after several attempts) does not meet this requirement. Keep compatibility with it and document its settings for this board.

## 5. INA219

- Measures DC/battery voltage before the buck, with a 0.1 Ω shunt and Kelvin connection, so the existing driver's calibration stays valid.
- Address must not collide with the supported sensors; the firmware supports `0x40`, `0x41`, `0x44`, `0x45`.
- On USB-only power the reading is about 0 V. Do not claim USB voltage or total current measurement.

## 6. Sensors and modem

External devices, supported by the firmware:

| Interface | Supported sensors |
|---|---|
| I²C | AHT30, BME280, BME680, BMP390, SCD30, SCD40, SCD41, SPS30, VEML7700, OPT3001, HTU2X, SHT3X, SHT4X, INA219, INA226 |
| UART | SDS011, PMSX003, MH-Z19B, GPS NMEA |
| GPIO / 1-Wire | PPD42NS, DHT11, DHT22, DS18B20 |
| ADC | ME3-NO2, with its required analog interface |

- Devices with the same fixed address or a shared UART cannot work together without extra provisions; document the limits.
- ESP32 GPIOs are not 5 V tolerant: provide level protection where a module can output 5 V.
- Modem: external carrier board (SIM7600 family and others supported by the firmware) with its own antenna. The board provides +5V with a defined current limit and the existing signal connector.

## 7. Pin assignments

### GPIO assignments

RX/TX are named from the ESP32's perspective.

| Function | GPIO |
|---|---|
| I²C SDA / SCL | 8 / 9 |
| Sensor UART2 RX / TX | 16 / 15 |
| Modem UART1 RX / TX | 18 / 17 |
| Modem PWRKEY / SLEEP(DTR) | 12 / 21 |
| GPIO1 / GPIO2 / GPIO3 external sensor signals | 4 / 5 / 6 |
| Native USB D− / D+ | 19 / 20 |
| RGB LED | 48 |
| BOOT | 0 |
| RESET | CHIP_PU / EN, not an arbitrary GPIO |

GPS and the modem share UART1 by default.

### Connector pin order

Electrical pin numbers, taken from the firmware and confirmed by the user on 2026-11-04. Physical order follows the shield.

| Connector | Pins in order |
|---|---|
| I2C1, I2C2, I2C3 | 1: +5V; 2: 3V3; 3: GND; 4: SCL; 5: SDA |
| SPS30 | 1: GND; 2: GND; 3: SCL; 4: SDA; 5: +5V |
| SCD30 | 1: GND; 2: +5V; 3: SDA; 4: SCL |
| UART2 | 1: +5V; 2: 3V3; 3: GND; 4: RX2 (GPIO16); 5: TX2 (GPIO15) |
| MODEM | 1: +5V; 2: GND; 3: ESP32 RX (GPIO18); 4: ESP32 TX (GPIO17); 5: PWRKEY (GPIO12); 6: SLEEP/DTR (GPIO21) |
| GPIO1 | 1: +5V; 2: 3V3; 3: GND; 4: GPIO4 |
| GPIO2 | 1: +5V; 2: 3V3; 3: GND; 4: GPIO5 |
| GPIO3 | 1: +5V; 2: 3V3; 3: GND; 4: GPIO6 |
| Debug J2 | 1: U0TXD; 2: U0RXD; 3: GND |

Connector types, positions and keying as on shield v1.3: 2.54 mm straight pin headers for MODEM, UART2, GPIO and I2C; JST ZH 1.5 mm vertical for SPS30 and SCD30.

## 8. Sources

- [Assembly and connections](../../../docs/guide/assembly.md).
- [Supported sensor matrix](../../../docs/firmware/sensors/supported-sensors.md).
- [Sensor documentation](../../../docs/firmware/sensors/README.md).
- [INA219 addresses and calibration](../../../docs/firmware/sensors/ina219.md).
- [Existing firmware power gate and limitations](../../../docs/firmware/power-gate.md).
- [Startup sequence](../../../docs/firmware/startup-pipeline.md).
- [Modem and GPIOs](../../../docs/firmware/sensors/sim7600e.md).
- [Modem management](../../../docs/firmware/cellular-manager.md).
- [Firmware configuration](../../../docs/firmware/configuration-reference.md).
- [Kconfig](../../../firmware/main/Kconfig.projbuild), [sdkconfig.defaults](../../../firmware/sdkconfig.defaults).
- [Sensor registry](../../../firmware/main/src/sensors/sensor_registry.cpp), [sensor types](../../../firmware/main/include/air360/sensors/sensor_types.hpp), [INA219 driver](../../../firmware/main/src/sensors/drivers/ina219_sensor.cpp).
- When documentation and firmware disagree, the firmware code is right.
