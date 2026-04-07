# Firmware ADRs

This directory contains firmware architecture decision records for Air360.

These documents are firmware-specific planning and design notes. They are useful for understanding intended direction, but `firmware/` remains the source of truth for what is already implemented.

## Document Map

- [`measurement-runtime-separation-adr.md`](measurement-runtime-separation-adr.md)
  Planned split between sensor lifecycle management and measurement runtime ownership.
- [`live-sensor-reconfiguration-adr.md`](live-sensor-reconfiguration-adr.md)
  Planned no-reboot sensor apply behavior built on top of the measurement/runtime split.
- [`sim7600e-mobile-uplink-adr.md`](sim7600e-mobile-uplink-adr.md)
  Planned cellular uplink support using `SIM7600E`.
- [`configurable-sntp-server-adr.md`](configurable-sntp-server-adr.md)
  Planned device-level `SNTP server` override and runtime `Check SNTP` action in the `Device` page.
- [`static-ip-configuration-adr.md`](static-ip-configuration-adr.md)
  Planned optional static IPv4 configuration for Wi-Fi station mode from the `Device` page.
