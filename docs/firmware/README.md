# Air360 Firmware Documentation

## Status

Implemented. Keep this index aligned with the current `firmware/` tree and the implementation docs under `docs/firmware/`.

## Scope

This is the top-level navigation map for firmware implementation documentation. It is written for maintainers and AI agents that need to find the right subsystem docs quickly.

## Source of truth in code

- `firmware/main/src/`
- `firmware/main/include/air360/`
- `firmware/main/Kconfig.projbuild`
- `firmware/sdkconfig.defaults`

## Read next

- [../../AGENTS.md](../../AGENTS.md)
- [../../firmware/AGENTS.md](../../firmware/AGENTS.md)
- [PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md)

Documentation for the Air360 firmware — an ESP32-S3 air quality monitoring device built on ESP-IDF 6.x and FreeRTOS.

The `firmware/` directory is the source of truth for all implemented behaviour described here.

---

## Getting started

| Document | Description |
|----------|-------------|
| [user-guide.md](user-guide.md) | End-user guide: flashing, first-time setup, web UI walkthrough |
| [PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md) | Directory layout, key source files, third-party components |
| [change-impact-map.md](change-impact-map.md) | What else to review when a firmware file or subsystem changes |

---

## Task routes

| I need to... | Read this first | Then read |
|--------------|-----------------|-----------|
| Understand startup and long-lived tasks | [startup-pipeline.md](startup-pipeline.md) | [ARCHITECTURE.md](ARCHITECTURE.md) |
| Work on NVS-backed config | [nvs.md](nvs.md) | [configuration-reference.md](configuration-reference.md) |
| Change Wi-Fi, setup AP, or SNTP | [network-manager.md](network-manager.md) | [time.md](time.md) |
| Change cellular modem behavior | [cellular-manager.md](cellular-manager.md) | [sensors/sim7600e.md](sensors/sim7600e.md) |
| Add or change a sensor driver | [sensors/adding-new-sensor.md](sensors/adding-new-sensor.md) | [sensors/supported-sensors.md](sensors/supported-sensors.md) + [transport-binding.md](transport-binding.md) |
| Change web routes or forms | [web-ui.md](web-ui.md) | [configuration-reference.md](configuration-reference.md) |
| Change upload behavior | [measurement-pipeline.md](measurement-pipeline.md) | [upload-adapters.md](upload-adapters.md) + [upload-transport.md](upload-transport.md) |
| Estimate doc fallout before editing | [change-impact-map.md](change-impact-map.md) | [doc-template.md](doc-template.md) |

---

## Architecture and boot

| Document | Description |
|----------|-------------|
| [ARCHITECTURE.md](ARCHITECTURE.md) | System overview: components, task model, data flow, GPIO allocation |
| [startup-pipeline.md](startup-pipeline.md) | 9-step boot sequence, long-lived tasks, failure modes |
| [nvs.md](nvs.md) | NVS storage: namespace, blob structs, validation and reset behaviour |

---

## Configuration

| Document | Description |
|----------|-------------|
| [configuration-reference.md](configuration-reference.md) | All configurable fields: defaults, ranges, validation rules |
| [web-ui.md](web-ui.md) | Embedded HTTP server: routes, pages, form actions, JavaScript behaviour |

---

## Networking and time

| Document | Description |
|----------|-------------|
| [network-manager.md](network-manager.md) | Wi-Fi station / setup AP modes, SNTP time sync, state transitions |
| [cellular-manager.md](cellular-manager.md) | SIM7600E modem lifecycle, PPP session, reconnect logic, hardware GPIO control |
| [time.md](time.md) | Uptime vs Unix time, SNTP sync sequence, time validity threshold, where time gates the system |

---

## Sensor subsystem

| Document | Description |
|----------|-------------|
| [sensors/README.md](sensors/README.md) | Supported sensors, hardware reference, and per-driver documentation index |
| [sensors/supported-sensors.md](sensors/supported-sensors.md) | Concise support matrix for all current sensor types |
| [sensors/adding-new-sensor.md](sensors/adding-new-sensor.md) | Implementation and doc checklist for adding a new driver |
| [transport-binding.md](transport-binding.md) | I2C bus manager and UART port manager used by all sensor drivers |

### Sensor drivers

| Document | Sensor | Transport | Measurements |
|----------|--------|-----------|--------------|
| [sensors/bme280.md](sensors/bme280.md) | BME280 | I2C `0x76` | Temperature, humidity, pressure |
| [sensors/bme680.md](sensors/bme680.md) | BME680 | I2C `0x77` | Temperature, humidity, pressure, gas resistance |
| [sensors/sps30.md](sensors/sps30.md) | SPS30 | I2C `0x69` | PM1.0–PM10.0, particle number concentrations, typical particle size |
| [sensors/scd30.md](sensors/scd30.md) | SCD30 | I2C `0x61` | CO₂, temperature, humidity |
| [sensors/veml7700.md](sensors/veml7700.md) | VEML7700 | I2C `0x10` | Illuminance |
| [sensors/htu2x.md](sensors/htu2x.md) | HTU2X | I2C `0x40` | Temperature, humidity |
| [sensors/sht4x.md](sensors/sht4x.md) | SHT4X | I2C `0x44` | Temperature, humidity |
| [sensors/gps_nmea.md](sensors/gps_nmea.md) | GPS (NMEA) | UART1 | Latitude, longitude, altitude, speed, satellites, HDOP |
| [sensors/dht.md](sensors/dht.md) | DHT11 / DHT22 | GPIO | Temperature, humidity |
| [sensors/ds18b20.md](sensors/ds18b20.md) | DS18B20 | GPIO (1-Wire) | Temperature |
| [sensors/me3_no2.md](sensors/me3_no2.md) | ME3-NO2 | Analog (ADC) | Raw ADC count, voltage |

---

## Measurement pipeline and upload

| Document | Description |
|----------|-------------|
| [measurement-pipeline.md](measurement-pipeline.md) | Sensor polling → queue → upload window → batch assembly → acknowledge/restore |
| [upload-adapters.md](upload-adapters.md) | Sensor.Community and Air360 API adapters: request format, grouping, response classification |
| [upload-transport.md](upload-transport.md) | HTTP execution layer: `esp_http_client` configuration, TLS, response struct |

---

## Architecture Decision Records

See [adr/README.md](adr/README.md) for the full index grouped by status.

---

## Documentation conventions

- Use [doc-template.md](doc-template.md) when creating or rewriting firmware implementation docs.
- Use [change-impact-map.md](change-impact-map.md) before or during refactors to see which docs usually need to move together.

---

## Document map

Key cross-document relationships:

```
startup-pipeline
  ├─ nvs                     (steps 4–6: config load)
  ├─ configuration-reference (field defaults and validation)
  ├─ network-manager         (step 7: Wi-Fi connection)
  └─ measurement-pipeline    (step 8: upload task spawn)

measurement-pipeline
  ├─ transport-binding       (sensor driver hardware access)
  └─ upload-adapters         (batch → HTTP requests)

upload-adapters
  └─ upload-transport        (HTTP execution)

network-manager
  └─ time                    (SNTP sync sequence, validity threshold)

cellular-manager
  └─ network-manager         (setCellularStatus uplink bearer sink)

measurement-pipeline
  └─ time                    (why samples are blocked before SNTP)

web-ui
  ├─ configuration-reference (field constraints for /config and /sensors)
  └─ network-manager         (setup AP mode detection)

sensors/*
  └─ transport-binding       (I2C / UART / GPIO transport details)
```
