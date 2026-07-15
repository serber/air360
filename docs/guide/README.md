# Air360 Build Guide

An end-to-end guide to building, flashing, and configuring an Air360 device —
from bare components to a station uploading air quality data.

Follow the guides in order for a first build, or jump straight to a topic.

## Guides

### 1. [Hardware Assembly](assembly.md)

Building the physical device.

- [Two ways to build Air360](assembly.md#two-ways-to-build-air360) — shield vs. direct wiring
- [Build with the shield board](assembly.md#option-1-build-with-the-shield-board) — parts list and connectors
- [Wire directly to the ESP32-S3](assembly.md#option-2-wire-directly-to-the-esp32-s3) — full sensor pinout
- [Choose your sensors](assembly.md#choose-your-sensors) — what each sensor measures
- [Enclosure: Stevenson screen](assembly.md#enclosure-stevenson-screen) — 3D-printable outdoor housing
- [Powering the device](assembly.md#powering-the-device) — mains and solar options

### 2. [Firmware, Web Interface, and Backends](firmware-and-backends.md)

Getting the device online and sending data.

- [Flashing the device](firmware-and-backends.md#flashing-the-device) — flash `full.bin`, first boot, setup Wi-Fi
- [Device web interface](firmware-and-backends.md#device-web-interface) — Overview, Device, Sensors, Backends
- [Where your measurements go](firmware-and-backends.md#where-your-measurements-go) — Air360, Sensor.Community, openSenseMap, InfluxDB, Custom Upload
- [Sensor.Community compatibility](firmware-and-backends.md#sensorcommunity-compatibility) — per-sensor value support

### 3. [Firmware Features — Device Page](device-features.md)

A reference for every card on the Device settings page.

- [Identity](device-features.md#identity) — device name, mDNS hostname
- [Wi-Fi station](device-features.md#wi-fi-station) — credentials, scanner, setup AP fallback
- [Wi-Fi power save](device-features.md#wi-fi-power-save) — modem sleep for battery/solar builds
- [Time (SNTP)](device-features.md#time-sntp) — clock sync, required for uploads
- [Static IP](device-features.md#static-ip) — fixed address instead of DHCP
- [Mobile uplink](device-features.md#mobile-uplink) — cellular modem as primary uplink
- [BLE advertising](device-features.md#ble-advertising) — BTHome v2 for Home Assistant
- [Firmware update](device-features.md#firmware-update) — over-the-air update from the browser

### 4. [Firmware Features — Backends](backends.md)

How to set up each upload target, field by field.

- [Air360 API](backends.md#air360-api) — the project's own service and map
- [Sensor.Community](backends.md#sensorcommunity) — the global volunteer network
- [openSenseMap](backends.md#opensensemap) — open data platform with sensor mapping
- [InfluxDB](backends.md#influxdb) — your own time-series database
- [Custom Upload](backends.md#custom-upload) — any HTTP endpoint you control

## See also

- [Firmware user guide](../firmware/user-guide.md) — full operational reference for the device firmware
- [Firmware documentation index](../firmware/README.md) — implementation docs for contributors
