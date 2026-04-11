# Air360 Firmware Documentation

Documentation for the Air360 firmware — an ESP32-S3 air quality monitoring device built on ESP-IDF 6.x and FreeRTOS.

The `firmware/` directory is the source of truth for all implemented behaviour described here.

---

## Getting started

| Document | Description |
|----------|-------------|
| [user-guide.md](user-guide.md) | End-user guide: flashing, first-time setup, web UI walkthrough |
| [PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md) | Directory layout, key source files, third-party components |

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
| [time.md](time.md) | Uptime vs Unix time, SNTP sync sequence, time validity threshold, where time gates the system |

---

## Sensor subsystem

| Document | Description |
|----------|-------------|
| [sensors/supported-sensors.md](sensors/supported-sensors.md) | Supported sensors: category, transport, address/pin |
| [sensors/README.md](sensors/README.md) | Per-driver documentation index |
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

| Document | Status | Topic |
|----------|--------|-------|
| [adr/measurement-runtime-separation-adr.md](adr/measurement-runtime-separation-adr.md) | Implemented | Split between sensor lifecycle and measurement runtime ownership |
| [adr/live-sensor-reconfiguration-adr.md](adr/live-sensor-reconfiguration-adr.md) | Implemented | No-reboot sensor reconfiguration via `SensorManager::applyConfig()` |
| [adr/overview-health-status-adr.md](adr/overview-health-status-adr.md) | Implemented | Aggregated device health summary for Overview page and `/status` |
| [adr/configurable-sntp-server-adr.md](adr/configurable-sntp-server-adr.md) | Proposed | User-configurable SNTP server with runtime `Check SNTP` action |
| [adr/static-ip-configuration-adr.md](adr/static-ip-configuration-adr.md) | Proposed | Optional static IPv4 configuration for Wi-Fi station mode |
| [adr/sim7600e-mobile-uplink-adr.md](adr/sim7600e-mobile-uplink-adr.md) | Proposed | Cellular uplink via SIM7600E |

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

measurement-pipeline
  └─ time                    (why samples are blocked before SNTP)

web-ui
  ├─ configuration-reference (field constraints for /config and /sensors)
  └─ network-manager         (setup AP mode detection)

sensors/*
  └─ transport-binding       (I2C / UART / GPIO transport details)
```
