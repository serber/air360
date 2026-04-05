# Sensor.Community Opportunity Roadmap

## Purpose

This document captures a full-pass review of the open `opendata-stuttgart/sensors-software` issue tracker and translates it into a practical Air360 roadmap.

The goal is not to clone the legacy airRohr firmware feature-for-feature. The goal is to identify the areas where Air360 can:

- preserve or improve real compatibility with the Sensor.Community ecosystem
- solve the same user problems in a stronger way
- earn credibility by addressing long-standing community pain points

## Scope And Method

This review was based on the full open issue tracker, not just the first page.

Snapshot used for this document:

- Repository: `opendata-stuttgart/sensors-software`
- Open issues reviewed: `209`
- Review date: `2026-04-06`

The shortlist below is intentionally filtered. Many open issues in the tracker are:

- `SDS011`-specific legacy problems
- `ESP8266`-specific build/runtime problems
- one-off support requests
- portal-side or data-platform issues that Air360 firmware cannot solve directly

This roadmap focuses only on issues that map well to Air360's current architecture and product direction.

## Selection Criteria

An issue was treated as a good Air360 candidate if it met one or more of these criteria:

- solves a repeated operational problem seen in the Sensor.Community ecosystem
- overlaps with Air360 features already in progress
- improves deployability in real installations
- improves reliability, diagnostics, or data quality
- can be delivered in Air360 without inheriting major legacy constraints

## Quick Wins

These are the highest-value near-term items. They align well with the current Air360 architecture and can create visible ecosystem value quickly.

### 1. Robust Time Sync And NTP Configuration

Relevant issues:

- [#1021 Keine Zeitsynchronisierung durch Fritzbox ntp](https://github.com/opendata-stuttgart/sensors-software/issues/1021)
- [#856 ESP32: setupNetworkTime() calls abort() when WiFi is not started](https://github.com/opendata-stuttgart/sensors-software/issues/856)

Why it matters:

- invalid time blocks uploads and creates `1970` behavior
- this is a real field problem, not just a developer problem
- Air360 has already seen similar startup and retry edge cases

What Air360 should do:

- allow configurable primary and fallback NTP servers
- show explicit time-sync health in UI and `/status`
- retry SNTP until time becomes valid without requiring reboot
- separate `uplink present` from `time valid`

Why this earns credibility:

- it addresses a long-standing operational issue directly tied to successful uploads
- it is easy for users to understand and validate

### 2. Sensor Calibration And Correction Layer

Relevant issues:

- [#1044 Temperature correction missing for some sensors](https://github.com/opendata-stuttgart/sensors-software/issues/1044)
- [#1033 BME280 temperature too high](https://github.com/opendata-stuttgart/sensors-software/issues/1033)
- [#687 Korrekturfaktor Temperatur](https://github.com/opendata-stuttgart/sensors-software/issues/687)
- [#691 Luftdruck Messung](https://github.com/opendata-stuttgart/sensors-software/issues/691)
- [#927 Temperature Correction Doesn't Seem To Work](https://github.com/opendata-stuttgart/sensors-software/issues/927)

Why it matters:

- raw temperature and pressure values are often not trustworthy without correction
- this is one of the most visible user-facing quality complaints
- Air360 already has a modern web UI where calibration can be exposed cleanly

What Air360 should do:

- add per-sensor correction values for temperature, humidity, and pressure
- apply the correction consistently across supported climate sensors
- show the configured correction in the sensor UI
- optionally distinguish raw versus corrected data in diagnostics

Why this earns credibility:

- it solves a repeated, high-visibility data-quality complaint
- it makes Air360 feel more mature than a simple port of old firmware ideas

### 3. Upload Robustness And Safer Failure Handling

Relevant issues:

- [#912 Crash and reboot when API reply is too big](https://github.com/opendata-stuttgart/sensors-software/issues/912)
- [#1055 Willkürliche Neustarts mit falsche Werte](https://github.com/opendata-stuttgart/sensors-software/issues/1055)
- [#976 Device hangs every 2 to 3 days](https://github.com/opendata-stuttgart/sensors-software/issues/976)

Why it matters:

- random reboot and hang behavior destroys user trust quickly
- these problems tend to surface only after real deployments, not lab testing
- Air360 already encountered memory-pressure and reboot issues around uploads

What Air360 should do:

- keep upload batching bounded
- harden large-response handling and error parsing
- expose richer restart diagnostics in UI and `/status`
- keep retry logic from turning transient faults into reboot loops

Why this earns credibility:

- stability improvements create immediate practical value
- strong runtime diagnostics reduce the support burden for public beta users

### 4. Better Offline Buffering And Queue Visibility

Relevant issues:

- [#112 Improve in case of WiFi down - cache some of the values](https://github.com/opendata-stuttgart/sensors-software/issues/112)
- [#940 Store the last 40 measurements in JSON file](https://github.com/opendata-stuttgart/sensors-software/issues/940)

Why it matters:

- intermittent uplink is normal in real installations
- Air360 already has a measurement queue, which is a strong foundation

What Air360 should do:

- continue improving backlog draining behavior
- expose queue depth clearly in the UI and `/status`
- add queue age diagnostics and backlog warnings
- consider optional persistent buffering later if RAM buffering is no longer enough

Why this earns credibility:

- it turns an internal implementation detail into a visible reliability feature
- it directly matches a recurring community request

## Mid-Term Features

These are strong candidates after the quick wins. They are more involved, but they extend Air360 in ways the Sensor.Community ecosystem repeatedly asked for.

### 5. Cellular Uplink For Off-Grid Deployments

Relevant issues:

- [#1054 GSM support](https://github.com/opendata-stuttgart/sensors-software/issues/1054)
- [#385 Support for mobile data transfer GSM / GPRS](https://github.com/opendata-stuttgart/sensors-software/issues/385)

Why it matters:

- this is one of the clearest unmet deployment needs
- it opens locations where Wi-Fi is not available
- it already matches Air360's current `SIM7600E` planning work

What Air360 should do:

- implement `SIM7600E` as an optional cellular uplink
- treat it as a first-class network path for time sync and backend upload
- preserve setup AP as the provisioning and recovery path

Why this earns credibility:

- it solves a long-standing ecosystem request in a modern ESP32-S3 firmware
- it clearly differentiates Air360 from the legacy ESP8266 baseline

### 6. Advanced Network Modes

Relevant issues:

- [#1009 Feature request: Static IP address](https://github.com/opendata-stuttgart/sensors-software/issues/1009)
- [#606 WPA Enterprise](https://github.com/opendata-stuttgart/sensors-software/issues/606)
- [#982 Wifi with Username/PW or Public with accepting the agreement first](https://github.com/opendata-stuttgart/sensors-software/issues/982)

Why it matters:

- schools, offices, labs, and managed networks often need more than basic WPA2-PSK
- static addressing is a common operational request

What Air360 should do:

- add optional static IPv4 configuration
- evaluate WPA2 Enterprise support as a distinct feature
- explicitly document that captive portals are not a standard unattended-device fit

Why this earns credibility:

- it expands the number of real environments where Air360 can be deployed reliably

### 7. Smarter Wi-Fi Recovery And Provisioning Behavior

Relevant issues:

- [#851 Add the function of finding a Wi-Fi network for some time](https://github.com/opendata-stuttgart/sensors-software/issues/851)
- [#841 Network re-connecting](https://github.com/opendata-stuttgart/sensors-software/issues/841)
- [#817 Hotspot mode timeout](https://github.com/opendata-stuttgart/sensors-software/issues/817)
- [#986 bei jedem reboot geht die WLAN Konfiguration verloren](https://github.com/opendata-stuttgart/sensors-software/issues/986)

Why it matters:

- many real field failures look like networking problems even when the sensor logic is fine
- provisioning behavior strongly affects first impressions of the product

What Air360 should do:

- keep station reconnects persistent and observable
- avoid trapping the device permanently in setup AP because of a temporary startup race
- keep Wi-Fi credentials durable across normal reboots
- make recovery states explicit in UI and `/status`

Why this earns credibility:

- it turns a class of recurring support issues into a product strength

## Strategic Bets

These are not the first items to do, but they could create strong ecosystem pull if implemented well.

### 8. Additional Backends And Integrations

Relevant issues:

- [#867 Support for Influx2](https://github.com/opendata-stuttgart/sensors-software/issues/867)
- [#709 Daten an InfluxDB senden](https://github.com/opendata-stuttgart/sensors-software/issues/709)
- [#405 How connect sensor to HomeAssistant?](https://github.com/opendata-stuttgart/sensors-software/issues/405)

Why it matters:

- many users want local or self-hosted observability, not only community uploads
- Air360 already has a backend abstraction layer

What Air360 should do:

- consider `InfluxDB 2.x` as the next strong backend candidate
- keep backend configuration consistent and minimal in the UI
- avoid turning the firmware into a generic sink-for-everything integration layer too early

Why this earns credibility:

- strong integrations make the device useful even outside Sensor.Community-only workflows

### 9. Ethernet And More Industrial Deployment Options

Relevant issues:

- [#728 Support of Ethernet (Olimex ESP32 POE ISO)](https://github.com/opendata-stuttgart/sensors-software/issues/728)

Why it matters:

- wired connectivity is attractive for stationary installations and noisy RF environments
- Air360 is already on ESP32-S3, where these expansions are more realistic than on old ESP8266 boards

What Air360 should do:

- treat Ethernet as a later alternate uplink, parallel to Wi-Fi and cellular
- build the connectivity layer in a way that keeps this future path open

Why this earns credibility:

- it positions Air360 as a more serious deployment platform, not just a hobby firmware

### 10. Identity Continuity Across Board Replacement

Relevant issues:

- [#145 Configurable ESP_ID](https://github.com/opendata-stuttgart/sensors-software/issues/145)
- [#922 ID not linked to specific nodMCU](https://github.com/opendata-stuttgart/sensors-software/issues/922)

Why it matters:

- replacing failed hardware should not force a user to lose continuity in the data history
- this is especially relevant for long-lived field installations

What Air360 should do:

- keep clarifying the distinction between hardware identity and ecosystem identity
- consider a controlled device identity migration flow in the future
- avoid casual identity editing that breaks trust or upload semantics

Why this earns credibility:

- it addresses a real lifecycle problem rather than a cosmetic feature gap

## Sensor Additions Worth Considering

New sensor support can help Air360, but it should not displace the higher-value reliability and uplink work above.

The strongest candidates from the tracker are:

- [#878 Add support for CO2 sensor Senseair S8](https://github.com/opendata-stuttgart/sensors-software/issues/878)
- [#375 Support for CO2 sensor MH-Z19](https://github.com/opendata-stuttgart/sensors-software/issues/375)
- [#949 Add support for MH-Z14 CO2 Module](https://github.com/opendata-stuttgart/sensors-software/issues/949)
- [#962 CCS811 Sensor Support](https://github.com/opendata-stuttgart/sensors-software/issues/962)
- [#681 Support for light intensity sensors](https://github.com/opendata-stuttgart/sensors-software/issues/681)
- [#822 Add Support For A Geiger Counter](https://github.com/opendata-stuttgart/sensors-software/issues/822)

Recommended approach:

- add new sensors only where they fit a clear category in the current Air360 sensor model
- prefer sensors that complement the existing roadmap, such as `CO2`, `light`, or `radiation`
- avoid reopening legacy `SDS011` complexity just to mirror old airRohr support

## What Not To Prioritize

The full tracker contains many issues that should not drive Air360 priorities:

- `SDS011`-specific problems
- `ESP8266` build failures and memory limits
- one-off board quirks for NodeMCU variants
- portal-side or map-side behavior that firmware cannot fix directly
- display-only feature requests with low operational value

These can be useful for general awareness, but they are not where Air360 gains the most credibility.

## Recommended Air360 Order

If the goal is to maximize practical value and ecosystem credibility, the recommended order is:

1. robust time sync and NTP configuration
2. calibration and correction layer
3. upload hardening and crash resilience
4. queue visibility and offline buffering improvements
5. `SIM7600E` cellular uplink
6. advanced networking such as static IP and selected enterprise support
7. additional backends such as `InfluxDB 2.x`
8. carefully chosen new sensor families such as `CO2`, `light`, and `radiation`

## Practical Conclusion

The strongest opportunities are not in reproducing every old airRohr feature.

The strongest opportunities are:

- making connectivity more robust
- making time and upload behavior trustworthy
- improving data quality through calibration
- supporting deployments where Wi-Fi is weak or absent

That is where Air360 can be both compatible with the Sensor.Community ecosystem and clearly better than the legacy baseline.
