# Sensor.Community Issues And PR Review For Air360

## Purpose

This is a second-pass review of the open `opendata-stuttgart/sensors-software` tracker focused on one practical question:

Which user pains from the Sensor.Community ecosystem can be closed in the current Air360 firmware without forcing a legacy-style rewrite?

Unlike the earlier roadmap, this pass includes both:

- all open issues
- all open pull requests

and filters them against the current Air360 implementation in `firmware/` and the current firmware docs in [`../firmware/README.md`](../firmware/README.md).

## Snapshot Used

- Review date: `2026-04-14`
- Upstream repository: `opendata-stuttgart/sensors-software`
- Open issues reviewed: `209`
- Open pull requests reviewed: `15`

## What Air360 Already Covers

Some recurring upstream requests are already implemented in Air360 and should not drive new backlog unless the current implementation needs refinement:

- Static IPv4 configuration is already supported in [`../firmware/configuration-reference.md`](../firmware/configuration-reference.md). This already covers the pain behind [#657](https://github.com/opendata-stuttgart/sensors-software/issues/657) and [#1009](https://github.com/opendata-stuttgart/sensors-software/issues/1009).
- A configurable SNTP server and a runtime `Check SNTP` flow already exist in [`../firmware/network-manager.md`](../firmware/network-manager.md) and [`../firmware/web-ui.md`](../firmware/web-ui.md). This partially addresses [#1021](https://github.com/opendata-stuttgart/sensors-software/issues/1021).
- Cellular uplink is already part of the firmware and configuration model via `SIM7600E`, described in [`../firmware/configuration-reference.md`](../firmware/configuration-reference.md). That already moves past the older GSM requests such as [#1054](https://github.com/opendata-stuttgart/sensors-software/issues/1054) and [#385](https://github.com/opendata-stuttgart/sensors-software/issues/385).
- Air360 already has a backend abstraction layer, in-memory upload queue, and backlog drain logic in [`../firmware/measurement-pipeline.md`](../firmware/measurement-pipeline.md) and [`../firmware/upload-adapters.md`](../firmware/upload-adapters.md).

That means the best next tasks are not the old baseline features. The best next tasks are the gaps that remain visible even in a more modern ESP32-S3 firmware.

## Recommended Task List

The items below are ordered by practical value, implementation fit, and how directly they close recurring ecosystem pain.

### 1. Wi-Fi Self-Healing And Provisioning Recovery

Relevant upstream threads:

- [#851 Add the function of finding a Wi-Fi network for some time](https://github.com/opendata-stuttgart/sensors-software/issues/851)
- [#841 Network re-connecting](https://github.com/opendata-stuttgart/sensors-software/issues/841)
- [#817 Hotspot mode timeout](https://github.com/opendata-stuttgart/sensors-software/issues/817)
- [#982 Wifi with Username/PW or Public with accepting the agreement first](https://github.com/opendata-stuttgart/sensors-software/issues/982)
- [PR #1042 Patch/better wifi debug](https://github.com/opendata-stuttgart/sensors-software/pull/1042)
- [PR #947 Make WiFi maximum TX power configurable](https://github.com/opendata-stuttgart/sensors-software/pull/947)

Why this is a strong Air360 task now:

- Air360 already has a clear network state machine, setup AP fallback, Wi-Fi scan API, and status UI.
- The current documented behavior is still weak for unattended recovery: [`../firmware/network-manager.md`](../firmware/network-manager.md) explicitly says there is no automatic reconnect after a disconnect, and a failed station boot can leave the device in setup AP until reboot.

Recommended scope:

- automatic station reconnect with bounded backoff
- periodic station retry while setup AP is active
- optional setup AP timeout and retry cycle
- clearer user-facing Wi-Fi error reasons in `/status` and the overview page
- optional Wi-Fi TX power setting and debug verbosity for difficult installs

User pain this closes:

- router or modem boots slower than the sensor
- router restarts leave the sensor offline forever
- the device gets stuck in hotspot mode until manual power-cycling

### 2. Consistent Correction Layer For Climate Sensors

Relevant upstream threads:

- [#1044 Temperature correction missing for some sensors](https://github.com/opendata-stuttgart/sensors-software/issues/1044)
- [#1033 BME280 temperature too high](https://github.com/opendata-stuttgart/sensors-software/issues/1033)
- [#927 Temperature Correction Doesn't Seem To Work](https://github.com/opendata-stuttgart/sensors-software/issues/927)
- [#979 temp compensation limited? unable to apply with HTU21D](https://github.com/opendata-stuttgart/sensors-software/issues/979)
- [#687 Korrekturfaktor Temperatur](https://github.com/opendata-stuttgart/sensors-software/issues/687)
- [PR #1048 enable IIR filtering for BME280](https://github.com/opendata-stuttgart/sensors-software/pull/1048)

Why this is a strong Air360 task now:

- Air360 already supports several climate sensors: `BME280`, `BME680`, `HTU2X`, `SHT4X`, `SCD30`, `DHT11`, `DHT22`.
- The current sensor config model in [`../firmware/configuration-reference.md`](../firmware/configuration-reference.md) has no per-sensor correction fields.

Recommended scope:

- per-sensor offsets for temperature, humidity, and pressure where applicable
- consistent application across UI, `/status`, and uploads
- optional raw-versus-corrected display in diagnostics
- fold BME280 driver tuneables into the same pass, starting with configurable IIR filtering

User pain this closes:

- “temperature is always too high”
- one sensor family supports correction while another does not
- users cannot compensate enclosure self-heating cleanly

### 3. Pressure Normalization And Altitude Compensation

Relevant upstream threads:

- [#102 BME280 pressure shows wrong values](https://github.com/opendata-stuttgart/sensors-software/issues/102)
- [#71 Einheitendiskrepanz Luftdruck zwischen luftdaten und sensemap](https://github.com/opendata-stuttgart/sensors-software/issues/71)
- [#936 Wrong format of 'pressure value' of a BMP280 Sensor](https://github.com/opendata-stuttgart/sensors-software/issues/936)

Why this is a strong Air360 task now:

- Pressure confusion is a repeated ecosystem pain: users expect sea-level-normalized pressure, but the device often reports absolute station pressure.
- Air360 already has `GPS (NMEA)` support and can carry altitude in the measurement model.

Recommended scope:

- optional installation altitude field in the sensor or device configuration
- optional sea-level-normalized pressure alongside raw pressure
- clear UI labeling so users understand which value is being shown and uploaded
- later enhancement: auto-use GPS altitude when GPS is configured and valid

User pain this closes:

- “my BME280 pressure is wrong”
- mismatch between local weather maps and sensor readings

### 4. SCD30 Calibration And Compensation Controls

Relevant upstream threads:

- [#1058 How "SCD30 Auto Calibration" activate](https://github.com/opendata-stuttgart/sensors-software/issues/1058)

Why this is a strong Air360 task now:

- Air360 already has an `SCD30` driver and already publishes `CO2`, temperature, and humidity.
- The current SCD30 documentation in [`../firmware/sensors/scd30.md`](../firmware/sensors/scd30.md) states that altitude is hardcoded to `0 m`, and there is no user-facing calibration control.

Recommended scope:

- enable or disable automatic self-calibration
- expose altitude or ambient-pressure compensation
- optionally expose forced recalibration for controlled maintenance workflows
- surface warm-up and calibration state in UI diagnostics

User pain this closes:

- users can install an SCD30 but cannot configure its key calibration behavior
- high-altitude deployments remain poorly compensated

### 5. Backend Fault Isolation And HTTP Hardening

Relevant upstream threads:

- [#912 Crash and reboot when API reply is too big](https://github.com/opendata-stuttgart/sensors-software/issues/912)
- [#1034 No data upload to opensensemap since 3/16/2024](https://github.com/opendata-stuttgart/sensors-software/issues/1034)

Why this is a strong Air360 task now:

- Air360 already has multiple backends and an upload manager, but the current queue semantics are still all-or-nothing: [`../firmware/measurement-pipeline.md`](../firmware/measurement-pipeline.md) documents that if any enabled backend fails, the inflight samples are restored for all.
- That means one bad backend can still stall the whole delivery pipeline even if other backends are healthy.

Recommended scope:

- bound and safely classify oversized or malformed HTTP responses
- isolate backend failures so one broken destination does not block the rest
- distinguish TLS, DNS, timeout, HTTP, and payload parsing failures in runtime status
- consider temporary backend quarantine after repeated failures

User pain this closes:

- one misbehaving API causes global upload disruption
- remote outages turn into reboot loops or invisible stuck queues

### 6. Authenticated Backend Support With First Integrations: openSenseMap And InfluxDB 2

Relevant upstream threads:

- [#1034 No data upload to opensensemap since 3/16/2024](https://github.com/opendata-stuttgart/sensors-software/issues/1034)
- [PR #1062 support openSenseMap device API keys](https://github.com/opendata-stuttgart/sensors-software/pull/1062)
- [#867 Support for Influx2](https://github.com/opendata-stuttgart/sensors-software/issues/867)

Why this is a strong Air360 task now:

- Air360 already has a backend registry in [`../firmware/upload-adapters.md`](../firmware/upload-adapters.md) and even carries a reserved `bearer_token` field in backend config, but the current firmware does not use it.
- This is a clean extension of the existing architecture, not a new subsystem.

Recommended scope:

- make `bearer_token` and custom auth headers real, not reserved-only
- add a dedicated openSenseMap uploader with device API key support
- add an `InfluxDB 2.x` uploader using token auth
- keep TLS based on ESP-IDF certificate bundle and report certificate failures cleanly

User pain this closes:

- users want more than Sensor.Community-only uploads
- openSenseMap compatibility is brittle when their auth model changes
- self-hosted time-series ingestion still needs a modern token-based path

### 7. Local History And Data-Quality Diagnostics

Relevant upstream threads:

- [#940 Store the last 40 measurements in JSON file](https://github.com/opendata-stuttgart/sensors-software/issues/940)
- [#937 Feature requests for data quality determination](https://github.com/opendata-stuttgart/sensors-software/issues/937)

Why this is a strong Air360 task now:

- Air360 already has a local latest-value cache plus a bounded upload queue.
- The missing piece is visibility: users still cannot inspect a short local history or basic quality metadata without an external backend.

Recommended scope:

- add a `last N samples` endpoint and small UI view
- expose queue depth, oldest pending sample age, and last successful upload time per backend
- store per-sample uptime and configured poll interval in local diagnostics
- for PM sensors, consider optional min/max/range metadata when that can be collected cheaply

User pain this closes:

- Wi-Fi outages leave users blind even if measurements continue locally
- users cannot quickly judge whether their data stream is healthy or repetitive

### 8. Battery And Power Telemetry

Relevant upstream threads:

- [#995 Support for battery reporting](https://github.com/opendata-stuttgart/sensors-software/issues/995)
- [PR #1038 Battery monitor feature v2 - based on INA219 Current&Power Monitor](https://github.com/opendata-stuttgart/sensors-software/pull/1038)
- [PR #847 Battery monitor feature](https://github.com/opendata-stuttgart/sensors-software/pull/847)
- [#99 Extension: Solar Panel + Battery](https://github.com/opendata-stuttgart/sensors-software/issues/99)
- [#104 Request: deep sleep for ESP8266 plus battery sensing for solar operation](https://github.com/opendata-stuttgart/sensors-software/issues/104)

Why this is a strong Air360 task now:

- Air360 already has an ESP32-S3 platform, I2C transport management, analog support, and a runtime status page.
- Remote and cellular installations benefit immediately from visible power telemetry.

Recommended scope:

- start with `INA219` over I2C because it gives voltage, current, and power cleanly
- optionally add a simpler ADC-divider battery voltage mode later
- show battery voltage, current draw, power, and charge estimate in the overview page and `/status`
- upload power telemetry only to Air360 or other rich backends, not necessarily to Sensor.Community

User pain this closes:

- off-grid or UPS-powered nodes fail silently
- users cannot estimate remaining runtime or installation quality

### 9. Targeted Sensor Expansion That Fits The Current Air360 Model

Relevant upstream threads:

- [#1059 Feature request: add support for Honeywell IH-PMC-001](https://github.com/opendata-stuttgart/sensors-software/issues/1059)
- [PR #992 Support for CCS811 sensor](https://github.com/opendata-stuttgart/sensors-software/pull/992)
- [#878 Add support for CO2 sensor Senseair S8](https://github.com/opendata-stuttgart/sensors-software/issues/878)
- [#949 Add support for MH-Z14 CO2 Module](https://github.com/opendata-stuttgart/sensors-software/issues/949)

Recommended order:

- `Honeywell IH-PMC-001` first if it maps cleanly onto the existing PM value kinds
- `Senseair S8` or `MH-Z14` next because `CO2 ppm` already exists in the measurement model
- `CCS811` after that, because it likely needs new value kinds and uploader decisions

Why this is lower than the items above:

- new sensors are valuable, but reliability, corrections, and backend robustness will close more repeated user pain than adding one more driver

## Tasks Worth Explicitly Not Prioritizing

These appeared in the upstream tracker or PR list, but they are not the best use of effort for Air360 right now:

- ESP8266-only build and flash problems
- NodeMCU board quirks
- old `SDS011`-specific instability threads
- map-side or server-side availability complaints like [#1060](https://github.com/opendata-stuttgart/sensors-software/issues/1060)
- translation-only PRs and cosmetic UI tweaks
- “compile for ESP32” work such as [PR #1039](https://github.com/opendata-stuttgart/sensors-software/pull/1039), because Air360 already is an ESP32-S3 firmware

## Recommended Implementation Order

If the goal is to close the most user pain with the least architectural churn, the best order is:

1. Wi-Fi self-healing and provisioning recovery
2. correction layer for climate sensors
3. pressure normalization and SCD30 calibration controls
4. backend fault isolation and HTTP hardening
5. authenticated backends: openSenseMap and InfluxDB 2
6. local history and queue diagnostics
7. battery and power telemetry
8. targeted new sensors

## Practical Conclusion

The strongest Air360 opportunities are not “copy more legacy firmware features”.

The strongest opportunities are:

- make connectivity recover by itself
- make climate data more trustworthy
- make uploads resilient when one backend misbehaves
- expose enough local diagnostics that the device is debuggable without external infrastructure

Those are the areas where Air360 can realistically ship improvements and visibly solve the same pains that keep resurfacing in Sensor.Community.
