# airrohr-firmware Server Communication Contract

## 1. Scope

### What was analyzed

This document is based on repository evidence from the current `airrohr-firmware` implementation, primarily:

- `airrohr-firmware/airrohr-firmware.ino`
- `airrohr-firmware/ext_def.h`
- `airrohr-firmware/defines.h`
- `airrohr-firmware/airrohr-cfg.h`
- `airrohr-firmware/utils.cpp`
- `airrohr-firmware/utils.h`
- `airrohr-firmware/html-content.h`
- `docs/modules/airrohr-firmware.md`

### In scope

- Outbound data uploads implemented by the firmware
- Target hosts, ports, protocols, paths, and request methods
- Headers explicitly set by the firmware
- Payload construction and field naming
- Send timing, gating, and retry behavior
- Response handling
- Runtime and compile-time settings that affect server communication
- Other firmware-initiated network communication that can affect behavior, especially NTP and OTA download

### Out of scope

- Server-side code and validation rules not present in this repository
- The second-stage OTA loader implementation in `airrohr-update-loader/`
- The local device web UI contract except where it changes outbound server communication

## 2. Communication Overview

### Confirmed behavior from code

- The firmware is client-initiated. It opens outbound connections and performs request/response exchanges. There is no long-lived bidirectional application protocol for backend uploads.
- Telemetry uploads are HTTP `POST` requests created in `sendData()` in `airrohr-firmware/airrohr-firmware.ino`.
- The main telemetry fan-out is:
  - Sensor.Community: one JSON `POST` per enabled sensor type, with `X-PIN`
  - Optional secondary targets: Madavi, OpenSenseMap, Feinstaub App, aircms, custom API, custom InfluxDB, CSV serial output
- OTA update checks use HTTPS `GET` requests in `fwDownloadStream()` to `firmware.sensor.community`.
- Time sync uses NTP via `configTime()` with `0.pool.ntp.org` and `1.pool.ntp.org`.

### High-level directionality

- Telemetry upload is effectively unidirectional from device to server. The response body is not parsed for configuration or command-and-control.
- OTA download is bidirectional request/response and the response body is consumed.

### Protocols used

| Purpose | Protocol |
| --- | --- |
| Telemetry upload | HTTP or HTTPS |
| OTA metadata/binary download | HTTPS |
| Time sync | NTP over UDP |
| Local configuration UI | HTTP on device-local server |

## 3. Server Targets

### Telemetry and update targets

| Target | Purpose | Host | Port | Protocol | Path / construction | Definition | Fixed / configurable |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Sensor.Community | Main public sensor upload | `api.sensor.community` | `80` or `443` | HTTP or HTTPS | `/v1/push-sensor-data/` | `HOST_SENSORCOMMUNITY`, `URL_SENSORCOMMUNITY` in `ext_def.h`; port chosen in `createLoggerConfigs()` | Fixed host/path, runtime SSL toggle |
| Madavi | Secondary archive upload | `api-rrd.madavi.de` | `80` or `443` | HTTP or HTTPS | `/data.php` | `HOST_MADAVI`, `URL_MADAVI` in `ext_def.h`; port chosen in `createLoggerConfigs()` | Fixed host/path, runtime SSL toggle |
| OpenSenseMap | OpenSenseMap upload | `ingress.opensensemap.org` | `443` | HTTPS-intended | `/boxes/{senseboxid}/data?luftdaten=1` | `HOST_SENSEMAP`, `URL_SENSEMAP` in `ext_def.h`; `tmpl()` in `utils.cpp` | Fixed host/path template, runtime `senseboxid` |
| Feinstaub App | Secondary upload | `server.chillibits.com` | `80` | HTTP | `/data.php` | `HOST_FSAPP`, `URL_FSAPP` in `ext_def.h` | Fixed |
| aircms | Secondary upload | `doiot.ru` | `80` | HTTP | `/php/sensors.php?h=<digest>` | `HOST_AIRCMS`, `URL_AIRCMS` in `ext_def.h`; digest appended in `sendDataToOptionalApis()` | Fixed host/path prefix |
| Custom API | User-defined upload | runtime config | runtime config | HTTP or HTTPS-intended | runtime config | `cfg::host_custom`, `cfg::url_custom`, `cfg::port_custom`, `cfg::ssl_custom` | Runtime configurable |
| Custom InfluxDB | User-defined upload | runtime config | runtime config | HTTP or HTTPS-intended | runtime config | `cfg::host_influx`, `cfg::url_influx`, `cfg::port_influx`, `cfg::ssl_influx` | Runtime configurable |
| OTA update server | Firmware download | `firmware.sensor.community` | `443` | HTTPS | `/<airrohr path>/update/latest_<lang>.bin`, `.md5`, loader files | `FW_DOWNLOAD_HOST`, `FW_DOWNLOAD_PORT`, `FW_2ND_LOADER_URL` in `ext_def.h`; URLs built in `twoStageOTAUpdate()` | Fixed host, path compile-time and runtime controlled |
| NTP server 1 | Time sync | `0.pool.ntp.org` | Not explicit in repository | NTP | Not explicit in repository | `NTP_SERVER_1` in `ext_def.h`; used by `setupNetworkTime()` | Fixed |
| NTP server 2 | Time sync | `1.pool.ntp.org` | Not explicit in repository | NTP | Not explicit in repository | `NTP_SERVER_2` in `ext_def.h`; used by `setupNetworkTime()` | Fixed |

### Platform caveat

Confirmed from `createLoggerConfigs()` and `getNewLoggerWiFiClient()`:

- On ESP8266, a non-null `loggerConfigs[logger].session` causes `WiFiClientSecure` to be used.
- On ESP32, `new_session()` returns `nullptr`, so the current code path does not allocate secure sessions for loggers. As written, logger requests still use `WiFiClient` even when the configured port is `443`.
- In this file, `esp_mac_id` is initialized only inside the ESP8266-specific setup path. The repository does not show an ESP32 assignment for `esp_mac_id` before it is used in `User-Agent` and `X-MAC-ID`.

## 4. Request Contract

### 4.1 Shared HTTP sender behavior

Source of truth:

- `sendData()` in `airrohr-firmware/airrohr-firmware.ino`
- `getNewLoggerWiFiClient()` in `airrohr-firmware/airrohr-firmware.ino`
- `configureCACertTrustAnchor()` in `utils.cpp`

Confirmed behavior:

- Method: `POST`
- Timeout: `20 * 1000` ms
- User-Agent: `SOFTWARE_VERSION + '/' + esp_chipid + '/' + esp_mac_id`
- Reuse: disabled by `http.setReuse(false)`
- Explicit headers always added for successful `http.begin(...)`:
  - `Content-Type`
  - `X-Sensor: <SENSOR_BASENAME><esp_chipid>`
  - `X-MAC-ID: <SENSOR_BASENAME><esp_mac_id>`
- `X-PIN` is added only when the caller passes a non-zero `pin`
- HTTP Basic Authorization is set only for:
  - `LoggerCustom` when `cfg::user_custom` or `cfg::pwd_custom` is non-empty
  - `LoggerInflux` when `cfg::user_influx` or `cfg::pwd_influx` is non-empty

Identifier formatting confirmed in `setup()`:

- On ESP8266, `esp_chipid` is `String(ESP.getChipId())`.
- On ESP8266, `esp_mac_id` is derived from `WiFi.macAddress()`, with `:` removed and the result lowercased.
- On ESP32, `esp_chipid` is derived from `ESP.getEfuseMac()`.

Header details not explicitly set by repository code:

- `Host`: not explicitly added by firmware code; it is implicit via `HTTPClient::begin(...)`
- Any other automatic library headers: Not clear from repository contents.

Version-string nuance:

- The JSON payload field `software_version` comes from `SOFTWARE_VERSION_STR` embedded into `data_first_part`.
- The `User-Agent` string comes from the runtime `SOFTWARE_VERSION` object.
- On ESP8266, `SOFTWARE_VERSION` may gain the `-STF` suffix if `ESP.checkFlashConfig()` fails in `setup()`, but `software_version` in JSON remains the compile-time `SOFTWARE_VERSION_STR`.

### 4.2 Sensor.Community request

Purpose:

- Upload one sensor-type-specific JSON payload to Sensor.Community.

Trigger:

- Inside the `if (send_now)` block in `loop()`
- One request per enabled sensor type with non-empty payload fragment
- Implemented by `sendSensorCommunity()`

Transport:

- Host: `api.sensor.community`
- Path: `/v1/push-sensor-data/`
- Port: `80` by default, `443` if `cfg::ssl_dusti` is true
- Protocol: HTTP by default, HTTPS-intended if `cfg::ssl_dusti` is true

Headers:

- `Content-Type: application/json`
- `X-Sensor: <SENSOR_BASENAME><esp_chipid>`
- `X-MAC-ID: <SENSOR_BASENAME><esp_mac_id>`
- `X-PIN: <sensor pin>`
- `User-Agent: <SOFTWARE_VERSION>/<esp_chipid>/<esp_mac_id>`

Payload format:

- JSON object
- Top-level keys:
  - `software_version`
  - `sensordatavalues`
- `software_version` comes from the compile-time constant `SOFTWARE_VERSION_STR` embedded in `data_first_part`
- `sensordatavalues` is an array of objects of the form:
  - `{"value_type":"<name>","value":"<string>"}`

Field naming rule:

- `sendSensorCommunity()` starts with the same fragment format as aggregate JSON
- It then removes a sensor-specific prefix string using `String::replace(replace_str, emptyString)`
- This means Sensor.Community sees unprefixed field names for most sensor families

Per-sensor `X-PIN` and transmitted `value_type` names:

| Sensor family | Pin macro | `X-PIN` | Source fragment names | Names after prefix stripping |
| --- | --- | --- | --- | --- |
| SDS011 | `SDS_API_PIN` | `1` | `SDS_P1`, `SDS_P2` | `P1`, `P2` |
| SDS021 | `SDS_API_PIN` | `1` | `SDS_P1`, `SDS_P2` | `P1`, `P2` |
| PMS1003 | `PMS_API_PIN` | `1` | `PMS_P0`, `PMS_P1`, `PMS_P2` | `P0`, `P1`, `P2` |
| PMS3003 | `PMS_API_PIN` | `1` | `PMS_P0`, `PMS_P1`, `PMS_P2` | `P0`, `P1`, `P2` |
| PMS5003 | `PMS_API_PIN` | `1` | `PMS_P0`, `PMS_P1`, `PMS_P2` | `P0`, `P1`, `P2` |
| PMS6003 | `PMS_API_PIN` | `1` | `PMS_P0`, `PMS_P1`, `PMS_P2` | `P0`, `P1`, `P2` |
| PMS7003 | `PMS_API_PIN` | `1` | `PMS_P0`, `PMS_P1`, `PMS_P2` | `P0`, `P1`, `P2` |
| PMSx003 family (generic code path) | `PMS_API_PIN` | `1` | `PMS_P0`, `PMS_P1`, `PMS_P2` | `P0`, `P1`, `P2` |
| Honeywell HPM | `HPM_API_PIN` | `1` | `HPM_P1`, `HPM_P2` | `P1`, `P2` |
| HM3301 | `PMS_API_PIN` | `1` | not recovered | not recovered |
| NextPM | `NPM_API_PIN` | `1` | `NPM_P0`, `NPM_P1`, `NPM_P2`, `NPM_N1`, `NPM_N10`, `NPM_N25` | `P0`, `P1`, `P2`, `N1`, `N10`, `N25` |
| IPS-7100 | `IPS_API_PIN` | `1` | `IPS_P0`, `IPS_P1`, `IPS_P2`, `IPS_P01`, `IPS_P03`, `IPS_P05`, `IPS_P5`, `IPS_N1`, `IPS_N10`, `IPS_N25`, `IPS_N01`, `IPS_N03`, `IPS_N05`, `IPS_N5` | `P0`, `P1`, `P2`, `P01`, `P03`, `P05`, `P5`, `N1`, `N10`, `N25`, `N01`, `N03`, `N05`, `N5` |
| SPS30 | `SPS30_API_PIN` | `1` | `SPS30_P0`, `SPS30_P2`, `SPS30_P4`, `SPS30_P1`, `SPS30_N05`, `SPS30_N1`, `SPS30_N25`, `SPS30_N4`, `SPS30_N10`, `SPS30_TS` | `P0`, `P2`, `P4`, `P1`, `N05`, `N1`, `N25`, `N4`, `N10`, `TS` |
| BMP180 | `BMP_API_PIN` | `3` | `BMP_pressure`, `BMP_temperature` | `pressure`, `temperature` |
| BMP280 | `BMP280_API_PIN` | `3` | `BMP280_pressure`, `BMP280_temperature` | `pressure`, `temperature` |
| PPD42NS | `PPD_API_PIN` | `5` | `durP1`, `ratioP1`, `P1`, `durP2`, `ratioP2`, `P2` | unchanged |
| DHT22 | `DHT_API_PIN` | `7` | `temperature`, `humidity` | unchanged |
| SHT10 / SHT11 / SHT15 | `DHT_API_PIN` | `7` | `temperature`, `humidity` | unchanged |
| HTU21D | `HTU21D_API_PIN` | `7` | `HTU21D_temperature`, `HTU21D_humidity` | `temperature`, `humidity` |
| SHT30 / SHT31 / SHT35 / SHT85 | `SHT3X_API_PIN` | `7` | `SHT3X_temperature`, `SHT3X_humidity` | `temperature`, `humidity` |
| SHT3X | `SHT3X_API_PIN` | `7` | `SHT3X_temperature`, `SHT3X_humidity` | `temperature`, `humidity` |
| GPS | `GPS_API_PIN` | `9` | `GPS_lat`, `GPS_lon`, `GPS_height`, `GPS_timestamp` | `lat`, `lon`, `height`, `timestamp` |
| BME280 | `BME280_API_PIN` | `11` | `BME280_temperature`, `BME280_pressure`, `BME280_humidity` | `temperature`, `pressure`, `humidity` |
| DS18S20 | `DS18B20_API_PIN` | `13` | `DS18B20_temperature` | `temperature` |
| DS18B20 | `DS18B20_API_PIN` | `13` | `DS18B20_temperature` | `temperature` |
| DNMS | `DNMS_API_PIN` | `15` | `DNMS_noise_LAeq`, `DNMS_noise_LA_min`, `DNMS_noise_LA_max` | `noise_LAeq`, `noise_LA_min`, `noise_LA_max` |
| SEN5x | `SEN5X_API_PIN` | `16` | not recovered | not recovered |
| SCD30 | `SCD30_API_PIN` | `17` | `SCD30_temperature`, `SCD30_humidity`, `SCD30_co2_ppm` | `temperature`, `humidity`, `co2_ppm` |
| NO2-A43F | `SCD30_API_PIN` | `17` | not recovered | not recovered |
| Radiation SBM-19 | `RADIATION_API_PIN` | `19` | not recovered | not recovered |
| Radiation SBM-20 | `RADIATION_API_PIN` | `19` | not recovered | not recovered |
| Radiation Si22G | `RADIATION_API_PIN` | `19` | not recovered | not recovered |

Normalized request specification:

- Protocol: HTTP or HTTPS-intended
- Host: `api.sensor.community`
- Port: `80` or `443`
- Method: `POST`
- Path: `/v1/push-sensor-data/`
- Headers:
  - `Content-Type: application/json`
  - `X-Sensor: <platform-prefix><chipid>`
  - `X-MAC-ID: <platform-prefix><macid>`
  - `X-PIN: <pin>`
  - `User-Agent: <version>/<chipid>/<macid>`
- Body schema:
  - object
  - `software_version: string`
  - `sensordatavalues: array<object{value_type:string,value:string}>`
- Example payload:

```json
{"software_version": "NRZ-2024-135", "sensordatavalues":[{"value_type":"P1","value":"12.34"},{"value_type":"P2","value":"5.67"}]}
```

- Trigger: each send interval, once per enabled sensor family with non-empty payload
- Success criteria: HTTP status `200` through `208`
- Source of truth in code: `sendSensorCommunity()`, `sendData()`, `data_first_part`, sensor field builders in `airrohr-firmware.ino`

### 4.3 Aggregate JSON requests: Madavi, OpenSenseMap, Feinstaub App

Purpose:

- Upload one combined JSON document containing all enabled sensor fragments plus metadata.

Trigger:

- Called from `sendDataToOptionalApis(data)` after the aggregate `data` document is fully built in `loop()`

Targets using this exact aggregate JSON body:

- Madavi
- OpenSenseMap
- Feinstaub App

Transport and endpoints:

| Target | Host | Port | Path | Notes |
| --- | --- | --- | --- | --- |
| Madavi | `api-rrd.madavi.de` | `80` or `443` | `/data.php` | SSL toggle via `cfg::ssl_madavi` |
| OpenSenseMap | `ingress.opensensemap.org` | `443` | `/boxes/<senseboxid>/data?luftdaten=1` | `senseboxid` inserted by `tmpl()` |
| Feinstaub App | `server.chillibits.com` | `80` | `/data.php` | No auth |

Headers:

- `Content-Type: application/json`
- `X-Sensor`
- `X-MAC-ID`
- No `X-PIN`

Payload format:

- JSON object
- Top-level keys:
  - `software_version`
  - `sensordatavalues`
- `sensordatavalues` contains:
  - all enabled sensor family fragments, with their original aggregate names preserved
  - metadata entries added unconditionally in the `send_now` block:
    - `samples`
    - `min_micro`
    - `max_micro`
    - `interval`
    - `signal`

Normalized request specification:

- Protocol: HTTP or HTTPS-intended
- Host: target-specific
- Port: target-specific
- Method: `POST`
- Path: target-specific
- Headers:
  - `Content-Type: application/json`
  - `X-Sensor: <platform-prefix><chipid>`
  - `X-MAC-ID: <platform-prefix><macid>`
  - `User-Agent: <version>/<chipid>/<macid>`
- Body schema:
  - object
  - `software_version: string`
  - `sensordatavalues: array<object{value_type:string,value:string}>`
- Example payload:

```json
{"software_version": "NRZ-2024-135", "sensordatavalues":[{"value_type":"SDS_P1","value":"12.34"},{"value_type":"SDS_P2","value":"5.67"},{"value_type":"temperature","value":"21.50"},{"value_type":"humidity","value":"48.20"},{"value_type":"samples","value":"580"},{"value_type":"min_micro","value":"228"},{"value_type":"max_micro","value":"9912"},{"value_type":"interval","value":"145000"},{"value_type":"signal","value":"-67"}]}
```

- Trigger: each send interval when the corresponding target is enabled
- Success criteria: HTTP status `200` through `208`
- Source of truth in code: aggregate build in `loop()`, `sendDataToOptionalApis()`, `sendData()`

### 4.4 OpenSenseMap path construction

Source of truth:

- `URL_SENSEMAP` in `ext_def.h`
- `tmpl()` in `utils.cpp`
- `sendDataToOptionalApis()` in `airrohr-firmware.ino`

Confirmed behavior:

- Path template: `/boxes/{v}/data?luftdaten=1`
- `{v}` is replaced with `cfg::senseboxid`
- Sending occurs only if:
  - `cfg::send2sensemap` is true
  - `cfg::senseboxid[0] != '\0'`

### 4.5 aircms request

Purpose:

- Upload one aggregate payload wrapped inside an `airrohr=` parameter and authenticated by a digest in the URL.

Source of truth:

- `sendDataToOptionalApis()` in `airrohr-firmware.ino`
- `sha1Hex()` and `hmac1()` in `utils.cpp`

Transport:

- Host: `doiot.ru`
- Port: `80`
- Protocol: HTTP
- Path prefix: `/php/sensors.php?h=`
- Method: `POST`

Headers:

- `Content-Type: text/plain`
- `X-Sensor`
- `X-MAC-ID`
- `User-Agent`

Body construction:

- `ts = millis() / 1000`
- `token = WiFi.macAddress()`
- Body:
  - `L=<esp_chipid>&t=<ts>&airrohr=<aggregate-json>`

Digest construction:

- URL suffix digest is:
  - `hmac1(sha1Hex(token), aircms_data + token)`
- `hmac1(secret, s)` does not implement standard HMAC; the exact code is:
  - `sha1Hex(secret + sha1Hex(s))`
- Therefore the final digest equals:
  - `sha1Hex(sha1Hex(token) + sha1Hex(aircms_data + token))`

Normalized request specification:

- Protocol: HTTP
- Host: `doiot.ru`
- Port: `80`
- Method: `POST`
- Path: `/php/sensors.php?h=<sha1Hex(sha1Hex(token) + sha1Hex(body + token))>`
- Headers:
  - `Content-Type: text/plain`
  - `X-Sensor: <platform-prefix><chipid>`
  - `X-MAC-ID: <platform-prefix><macid>`
  - `User-Agent: <version>/<chipid>/<macid>`
- Body schema:
  - plain text
  - `L=<chipid>&t=<seconds-since-boot>&airrohr=<aggregate-json>`
- Example payload:

```text
L=12345678&t=9123&airrohr={"software_version": "NRZ-2024-135", "sensordatavalues":[{"value_type":"SDS_P1","value":"12.34"},{"value_type":"SDS_P2","value":"5.67"},{"value_type":"interval","value":"145000"},{"value_type":"signal","value":"-67"}]}
```

- Trigger: each send interval when `cfg::send2aircms` is true
- Success criteria: HTTP status `200` through `208`
- Source of truth in code: `sendDataToOptionalApis()`, `sha1Hex()`, `hmac1()`, `sendData()`

### 4.6 Custom API request

Purpose:

- Upload a user-defined JSON document to a user-defined host/path.

Source of truth:

- `sendDataToOptionalApis()` in `airrohr-firmware.ino`
- runtime config keys in `airrohr-cfg.h`

Transport:

- Host: `cfg::host_custom`
- Port: `cfg::port_custom`
- Path: `cfg::url_custom`
- Protocol: HTTP by default; HTTPS-intended when `cfg::ssl_custom` is true or `cfg::port_custom == 443`

Headers:

- `Content-Type: application/json`
- `X-Sensor`
- `X-MAC-ID`
- `User-Agent`
- Optional HTTP Basic Authorization

Payload construction:

- Start from aggregate JSON `data`
- Remove the first `{`
- Prefix `{"esp8266id": "<esp_chipid>", `
- Result is a JSON object with an additional top-level `esp8266id` field

Important confirmed detail:

- The field name is literally `esp8266id`, even when the platform prefix may be `esp32-`.

Normalized request specification:

- Protocol: HTTP or HTTPS-intended
- Host: runtime-configured
- Port: runtime-configured
- Method: `POST`
- Path: runtime-configured
- Headers:
  - `Content-Type: application/json`
  - `X-Sensor: <platform-prefix><chipid>`
  - `X-MAC-ID: <platform-prefix><macid>`
  - `User-Agent: <version>/<chipid>/<macid>`
  - optional `Authorization: Basic ...`
- Body schema:
  - object
  - `esp8266id: string`
  - `software_version: string`
  - `sensordatavalues: array<object{value_type:string,value:string}>`
- Example payload:

```json
{"esp8266id": "12345678", "software_version": "NRZ-2024-135", "sensordatavalues":[{"value_type":"SDS_P1","value":"12.34"},{"value_type":"SDS_P2","value":"5.67"},{"value_type":"interval","value":"145000"},{"value_type":"signal","value":"-67"}]}
```

- Trigger: each send interval when `cfg::send2custom` is true
- Success criteria: HTTP status `200` through `208`
- Source of truth in code: `sendDataToOptionalApis()`, `sendData()`, `cfg::*custom`

### 4.7 InfluxDB request

Purpose:

- Upload aggregate readings transformed from the JSON array into one Influx line protocol record.

Source of truth:

- `create_influxdb_string_from_data()` in `airrohr-firmware.ino`
- `isNumeric()` in `utils.cpp`

Transport:

- Host: `cfg::host_influx`
- Port: `cfg::port_influx`
- Path: `cfg::url_influx`
- Protocol: HTTP by default; HTTPS-intended only when `cfg::ssl_influx` is true

Headers:

- `Content-Type: application/x-www-form-urlencoded`
- `X-Sensor`
- `X-MAC-ID`
- `User-Agent`
- Optional HTTP Basic Authorization

Payload construction:

- Measurement name: `cfg::measurement_name_influx`
- Tag set: `node=<SENSOR_BASENAME><esp_chipid>`
- Field set: one field per `sensordatavalues` entry from the aggregate JSON
- Numeric detection:
  - values matching `isNumeric()` are written without quotes
  - other values are wrapped in double quotes
- A trailing comma is removed
- A trailing newline is appended
- No timestamp is appended

Important formatting caveats:

- Field names are not escaped for Influx line protocol.
- The measurement name is not escaped.
- String field values are quoted, but other line-protocol escaping behavior is not visible in repository code.

Normalized request specification:

- Protocol: HTTP or HTTPS-intended
- Host: runtime-configured
- Port: runtime-configured
- Method: `POST`
- Path: runtime-configured
- Headers:
  - `Content-Type: application/x-www-form-urlencoded`
  - `X-Sensor: <platform-prefix><chipid>`
  - `X-MAC-ID: <platform-prefix><macid>`
  - `User-Agent: <version>/<chipid>/<macid>`
  - optional `Authorization: Basic ...`
- Body schema:
  - single line protocol record plus trailing newline
  - `<measurement>,node=<platform-prefix><chipid> <field>=<value>,<field>=<value>...`
- Example payload:

```text
feinstaub,node=esp8266-12345678 SDS_P1=12.34,SDS_P2=5.67,temperature=21.50,humidity=48.20,samples=580,interval=145000,signal=-67
```

- Trigger: each send interval when `cfg::send2influx` is true
- Success criteria: HTTP status `200` through `208`
- Source of truth in code: `create_influxdb_string_from_data()`, `isNumeric()`, `sendData()`

### 4.8 OTA download requests

Purpose:

- Download metadata and binaries for auto-update.

Source of truth:

- `fwDownloadStream()` and `twoStageOTAUpdate()` in `airrohr-firmware.ino`

Confirmed behavior:

- Method: `GET`
- Host: `firmware.sensor.community`
- Port: `443`
- Timeout: `60 * 1000` ms
- User-Agent format:
  - `<SOFTWARE_VERSION> <esp_chipid>/<esp_mac_id> <PM-sensor-version-date> <cfg::current_lang> <CURRENT_LANG> [BETA]`
- URLs fetched:
  - `/<airrohr path>/update/latest_<lang>.bin.md5`
  - `/<airrohr path>/update/latest_<lang>.bin`
  - `/<loader path>.md5`
  - `/<loader path>`

This traffic is implemented in the current firmware, but it is not part of the sensor-data upload contract.

## 5. Payload Schema

### 5.1 Shared JSON object schema

Confirmed JSON envelope from `data_first_part` and `add_Value2Json()`:

```text
object {
  software_version: string,
  sensordatavalues: array<
    object {
      value_type: string,
      value: string
    }
  >
}
```

Confirmed schema details:

- `software_version` is always a JSON string.
- Every sensor reading is represented as a string in the `value` field, even when semantically numeric.
- The JSON is assembled manually as strings, not by serializing a JSON object model.

### 5.2 Aggregate JSON field inventory

Possible `value_type` entries in aggregate JSON, taken from `add_Value2Json(...)` calls in `airrohr-firmware.ino`:

- `temperature`
- `humidity`
- `HTU21D_temperature`
- `HTU21D_humidity`
- `BMP_pressure`
- `BMP_temperature`
- `SHT3X_temperature`
- `SHT3X_humidity`
- `SCD30_temperature`
- `SCD30_humidity`
- `SCD30_co2_ppm`
- `BME280_temperature`
- `BME280_pressure`
- `BME280_humidity`
- `BMP280_pressure`
- `BMP280_temperature`
- `DS18B20_temperature`
- `SDS_P1`
- `SDS_P2`
- `PMS_P0`
- `PMS_P1`
- `PMS_P2`
- `HPM_P1`
- `HPM_P2`
- `NPM_P0`
- `NPM_P1`
- `NPM_P2`
- `NPM_N1`
- `NPM_N10`
- `NPM_N25`
- `IPS_P0`
- `IPS_P1`
- `IPS_P2`
- `IPS_P01`
- `IPS_P03`
- `IPS_P05`
- `IPS_P5`
- `IPS_N1`
- `IPS_N10`
- `IPS_N25`
- `IPS_N01`
- `IPS_N03`
- `IPS_N05`
- `IPS_N5`
- `durP1`
- `ratioP1`
- `P1`
- `durP2`
- `ratioP2`
- `P2`
- `SPS30_P0`
- `SPS30_P2`
- `SPS30_P4`
- `SPS30_P1`
- `SPS30_N05`
- `SPS30_N1`
- `SPS30_N25`
- `SPS30_N4`
- `SPS30_N10`
- `SPS30_TS`
- `DNMS_noise_LAeq`
- `DNMS_noise_LA_min`
- `DNMS_noise_LA_max`
- `GPS_lat`
- `GPS_lon`
- `GPS_height`
- `GPS_timestamp`
- `samples`
- `min_micro`
- `max_micro`
- `interval`
- `signal`

### 5.3 Value formatting and data type notes

Confirmed from repository code:

- Most sensor floats are serialized via `String(value)` in `add_Value2Json(...)`.
- GPS latitude and longitude are serialized with explicit precision via `String(last_value_GPS_lat, 6)` and `String(last_value_GPS_lon, 6)`.
- GPS timestamp format is explicitly built with:
  - `%04d-%02d-%02dT%02d:%02d:%02d.%03d`
- Metadata fields are serialized from integer-like values converted with `String(...)`.

Precision for non-GPS float fields:

- The exact decimal precision produced by `String(value)` is determined by the Arduino core implementation, not by repo-local code. Not clear from repository contents.

### 5.4 Mapping from internal values to transmitted fields

Selected exact mappings confirmed in code:

| Internal source | Transmitted field(s) | Notes |
| --- | --- | --- |
| `last_value_DHT_T`, `last_value_DHT_H` | `temperature`, `humidity` | DHT only |
| `last_value_HTU21D_T`, `last_value_HTU21D_H` | `HTU21D_temperature`, `HTU21D_humidity` | aggregate names |
| `last_value_BMP_P`, `last_value_BMP_T` | `BMP_pressure`, `BMP_temperature` | |
| `last_value_BMX280_T`, `last_value_BMX280_P`, `last_value_BME280_H` | `BME280_*` or `BMP280_*` | depends on `bmx280.sensorID()` |
| averaged SDS accumulators | `SDS_P1`, `SDS_P2` | average after dropping min and max if more than two samples |
| PMS values | `PMS_P0`, `PMS_P1`, `PMS_P2` | |
| HPM values | `HPM_P1`, `HPM_P2` | |
| NextPM values | `NPM_P0`, `NPM_P1`, `NPM_P2`, `NPM_N1`, `NPM_N10`, `NPM_N25` | |
| IPS values | `IPS_*` family | |
| PPD low pulse occupancy derived values | `durP1`, `ratioP1`, `P1`, `durP2`, `ratioP2`, `P2` | concentration uses the cubic formula in `fetchSensorPPD()` |
| SPS30 accumulated averages | `SPS30_*` family | |
| DNMS values | `DNMS_noise_LAeq`, `DNMS_noise_LA_min`, `DNMS_noise_LA_max` | |
| GPS state | `GPS_lat`, `GPS_lon`, `GPS_height`, `GPS_timestamp` | lat/lon use 6 decimals |
| runtime counters | `samples`, `min_micro`, `max_micro`, `interval`, `signal` | always added to aggregate JSON |

## 6. Transmission Rules

### 6.1 Main send scheduling

Source of truth:

- `loop()` in `airrohr-firmware.ino`
- timing constants in `defines.h`

Confirmed behavior:

- `send_now = msSince(starttime) > cfg::sending_intervall_ms`
- Default `cfg::sending_intervall_ms` is `145000` ms
- After each send cycle, `starttime = millis()` is reset
- This means the interval is measured from the end of the previous send cycle

### 6.2 Initial NTP gate

Confirmed behavior:

- Before sending, the firmware checks:
  - `!sntp_time_set`
  - `send_now`
  - uptime less than about `65` seconds
- If all are true:
  - it logs `NTP sync not finished yet, skipping send`
  - sets `send_now = false`
  - resets `starttime = act_milli`

Platform note:

- `sntp_time_set` is incremented via `settimeofday_cb(...)` only inside `#if defined(ESP8266)`.
- ESP32-specific NTP completion behavior is not implemented the same way in this file.

### 6.3 Sensor prerequisites and conditional sending

Confirmed behavior:

- Sensor.Community requests are only sent when:
  - `cfg::send2dusti` is true
  - the per-sensor fragment string is non-empty
- OpenSenseMap requests require both:
  - `cfg::send2sensemap`
  - non-empty `cfg::senseboxid`
- Aggregate optional APIs are sent only when their target-specific enable flag is true
- Most sensor fragments are appended only after a successful read or successful averaging window
- If a sensor read fails, most sensor functions omit those fields from the JSON fragment instead of sending sentinel values

Important exception:

- GPS fields are added on every send cycle when `cfg::gps_read` is true, using the current stored GPS state. If valid GPS data has not been obtained, the stored values can remain sentinel or default values.

### 6.4 Sampling, buffering, and batching

Confirmed behavior:

- Sensor.Community is not batch-combined across sensor families. It receives one request per enabled sensor family.
- Madavi, OpenSenseMap, Feinstaub App, aircms, custom API, and Influx each get one request per send cycle.
- There is no persistent queue of unsent payloads.
- There is no offline replay.
- There is no multi-interval batching.

### 6.5 Retries and fallback

Confirmed behavior:

- `sendData()` performs a single `POST`.
- There is no immediate retry loop for failed HTTP uploads.
- Failure results in another attempt only on the next send interval.
- If Wi-Fi is disconnected after sending, the firmware calls:
  - `WiFi.reconnect()`
  - `waitForWifiToConnect(20)`

### 6.6 HTTP timeout behavior

Confirmed behavior:

- Telemetry `POST` timeout: `20` seconds
- OTA `GET` timeout: `60` seconds

## 7. Response Handling

### 7.1 Telemetry upload responses

Source of truth:

- `sendData()` in `airrohr-firmware.ino`

Confirmed behavior:

- Success is defined as HTTP status code from `200` through `208` inclusive.
- On success:
  - logs `Succeeded - <host>`
- On failure:
  - logs `Request failed with error: <result>`
  - if `result >= HTTP_CODE_BAD_REQUEST`, logs `http.getString()`
- The response body is not parsed for control data.
- The response does not alter configuration or next-request shape.

Error accounting:

- `loggerConfigs[logger].errors++` happens only when:
  - `send_success` is false
  - `result != 0`
- A failure to initialize the request via `http.begin(...)` logs `Failed connecting to <host>` but does not increment the per-logger error counter because `result` remains `0`.

### 7.2 OTA responses

Confirmed behavior:

- OTA `GET` expects `HTTP_CODE_OK` before writing the response to a stream.
- `.md5` responses are trimmed and compared against local MD5 values.
- Firmware binary download is locally validated by:
  - minimum size
  - 16-byte alignment
  - MD5 equality

## 8. Configuration Surface

### 8.1 Compile-time defaults

Primary source:

- `ext_def.h`

Communication-related compile-time defaults:

| Name | Purpose | Default |
| --- | --- | --- |
| `SEND2SENSORCOMMUNITY` | enable Sensor.Community upload | `1` |
| `SSL_SENSORCOMMUNITY` | Sensor.Community HTTPS toggle default | `0` |
| `SEND2MADAVI` | enable Madavi upload | `1` |
| `SSL_MADAVI` | Madavi HTTPS toggle default | `0` |
| `SEND2SENSEMAP` | enable OpenSenseMap upload | `0` |
| `SEND2FSAPP` | enable Feinstaub App upload | `0` |
| `SSL_FSAPP` | Feinstaub App SSL default | `0` |
| `SEND2AIRCMS` | enable aircms upload | `0` |
| `SEND2INFLUX` | enable custom Influx upload | `0` |
| `SEND2CUSTOM` | enable custom API upload | `0` |
| `SEND2CSV` | enable CSV serial output | `0` |
| `HOST_SENSORCOMMUNITY` / `URL_SENSORCOMMUNITY` | fixed Sensor.Community target | `api.sensor.community` / `/v1/push-sensor-data/` |
| `HOST_MADAVI` / `URL_MADAVI` | fixed Madavi target | `api-rrd.madavi.de` / `/data.php` |
| `HOST_SENSEMAP` / `URL_SENSEMAP` | fixed OpenSenseMap target | `ingress.opensensemap.org` / `/boxes/{v}/data?luftdaten=1` |
| `HOST_FSAPP` / `URL_FSAPP` | fixed Feinstaub App target | `server.chillibits.com` / `/data.php` |
| `HOST_AIRCMS` / `URL_AIRCMS` | fixed aircms target | `doiot.ru` / `/php/sensors.php?h=` |
| `HOST_CUSTOM` / `URL_CUSTOM` / `PORT_CUSTOM` | custom API defaults | `192.168.234.1` / `/data.php` / `80` |
| `HOST_INFLUX` / `URL_INFLUX` / `PORT_INFLUX` | Influx defaults | `influx.server` / `/write?db=sensorcommunity` / `8086` |
| `MEASUREMENT_NAME_INFLUX` | Influx measurement name default | `feinstaub` |

### 8.2 Runtime config keys persisted in `/config.json`

Primary source:

- `airrohr-cfg.h`
- `readConfig()` and `writeConfig()` in `airrohr-firmware.ino`

Communication-related persisted keys:

- `send2dusti`
- `ssl_dusti`
- `send2madavi`
- `ssl_madavi`
- `send2sensemap`
- `send2fsapp`
- `send2aircms`
- `send2csv`
- `sending_intervall_ms`
- `senseboxid`
- `send2custom`
- `host_custom`
- `url_custom`
- `port_custom`
- `user_custom`
- `pwd_custom`
- `ssl_custom`
- `send2influx`
- `host_influx`
- `url_influx`
- `port_influx`
- `user_influx`
- `pwd_influx`
- `measurement_name_influx`
- `ssl_influx`
- `auto_update`
- `use_beta`

Runtime type and UI behavior:

- `sending_intervall_ms` is stored in `/config.json` in milliseconds.
- In the web form, `Config_Type_Time` values are displayed in seconds and converted back to milliseconds on `POST`.
- Integer-like values use `String(...).toInt()` on form submission.
- Strings are copied with fixed maximum lengths from `defines.h`.

### 8.3 Validation and rewrites

Confirmed behavior in `readConfig()`:

- `cfg::sending_intervall_ms` is clamped to at least `READINGTIME_SDS_MS`
- Placeholder OpenSenseMap box ID `00112233445566778899aabb` is cleared and disables `send2sensemap`
- Empty `measurement_name_influx` is reset to the compile-time default
- If `host_influx == "api.luftdaten.info"`, Influx upload is disabled and the host is cleared

### 8.4 TLS selection rules

Confirmed behavior:

- Sensor.Community:
  - HTTPS-intended only when `cfg::ssl_dusti` is true
- Madavi:
  - HTTPS-intended only when `cfg::ssl_madavi` is true
- OpenSenseMap:
  - always port `443`, always secure-client-intended on ESP8266
- Custom API:
  - secure-client-intended when `cfg::ssl_custom` is true or `cfg::port_custom == 443`
- Influx:
  - secure-client-intended only when `cfg::ssl_influx` is true

ESP8266 trust behavior:

- Sensor.Community, Madavi, and OpenSenseMap use `configureCACertTrustAnchor(...)`
- If device time is earlier than the firmware build year, `configureCACertTrustAnchor(...)` falls back to `setInsecure()`
- Custom and Influx secure mode explicitly use `setInsecure()` on ESP8266

Unused or partially used settings:

- `SSL_FSAPP` exists in `ext_def.h` but is not wired into `createLoggerConfigs()` or runtime config. The current Feinstaub App upload path is fixed to port `80`.
- `SEND2MQTT` exists, but telemetry MQTT sending is not implemented. The comment above the placeholder says `rejected (see issue #33)`.

## 9. Compatibility-Critical Requirements

### Definitely required

These behaviors are directly implemented and should be preserved to match the current firmware contract:

- Sensor.Community uploads use `POST` to `/v1/push-sensor-data/` on `api.sensor.community`.
- Sensor.Community requests include:
  - `Content-Type: application/json`
  - `X-Sensor`
  - `X-MAC-ID`
  - `X-PIN`
  - `User-Agent`
- Sensor.Community bodies use:
  - top-level `software_version`
  - top-level `sensordatavalues`
  - array elements shaped as `{"value_type":"...","value":"..."}`
- Sensor.Community sends one request per sensor family, not one combined payload.
- Aggregate JSON uploads to Madavi/OpenSenseMap/Feinstaub App use the same `software_version` plus `sensordatavalues` envelope and preserve the aggregate field names.
- JSON `value` entries are strings, not JSON numbers.
- `X-Sensor` is `<SENSOR_BASENAME><esp_chipid>`.
- `X-MAC-ID` is `<SENSOR_BASENAME><esp_mac_id>`.
- `X-PIN` values must match the pin mapping in `ext_def.h`.
- Custom API payloads prepend a top-level `esp8266id` field.
- Influx uploads transform the aggregate JSON into one line protocol record with tag `node=<SENSOR_BASENAME><esp_chipid>`.

### Probably required

These are always sent by the current firmware and may be backend-sensitive, but server-side necessity is not provable from this repository alone:

- The presence of the top-level `software_version` field in JSON uploads
- The exact `value_type` names listed in this document
- The metadata entries in aggregate JSON:
  - `samples`
  - `min_micro`
  - `max_micro`
  - `interval`
  - `signal`
- The `User-Agent` structure `<version>/<chipid>/<macid>` for telemetry `POST`
- The OpenSenseMap query suffix `?luftdaten=1`
- The aircms digest formula and `L=...&t=...&airrohr=...` body layout

### Unclear from repository contents

- Whether Sensor.Community requires `X-MAC-ID` for acceptance, deduplication, or only for diagnostics. Not clear from repository contents.
- Whether servers validate a specific `software_version` string format or only record it. Not clear from repository contents.
- Whether Madavi, OpenSenseMap, Feinstaub App, or custom-compatible backends require all aggregate metadata entries. Not clear from repository contents.
- Whether the exact float string precision produced by `String(value)` is server-sensitive. Not clear from repository contents.
- Whether any of the fixed backends depend on headers automatically inserted by the Arduino `HTTPClient` library beyond the headers explicitly set by this firmware. Not clear from repository contents.

## 10. Implementation Map

### Core request construction

- `airrohr-firmware/airrohr-firmware.ino`
  - `sendData()`: generic HTTP `POST`, headers, auth, success/failure rules
  - `sendSensorCommunity()`: per-sensor Sensor.Community body assembly and prefix stripping
  - `sendDataToOptionalApis()`: fan-out to Madavi, OpenSenseMap, Feinstaub App, aircms, Influx, custom API, CSV
  - `create_influxdb_string_from_data()`: aggregate JSON to Influx line protocol
  - `fwDownloadStream()`: OTA `GET`
  - `twoStageOTAUpdate()`: OTA URL construction and download flow

### Constants and defaults

- `airrohr-firmware/ext_def.h`
  - fixed hosts, paths, ports
  - sensor pin mappings
  - enable/disable compile-time defaults
- `airrohr-firmware/defines.h`
  - `SENSOR_BASENAME`
  - timing constants
  - config field length limits

### Serialization helpers

- `airrohr-firmware/utils.cpp`
  - `add_Value2Json()`: exact JSON fragment shape
  - `tmpl()`: OpenSenseMap path substitution
  - `sha1Hex()`, `hmac1()`: aircms digest construction
  - `isNumeric()`: Influx numeric quoting decision
  - `configureCACertTrustAnchor()`: ESP8266 TLS trust behavior

### Scheduling and send orchestration

- `airrohr-firmware/airrohr-firmware.ino`
  - `loop()`: send interval trigger, per-sensor send order, metadata addition, Wi-Fi reconnect, OTA retry timing
  - sensor readers such as `fetchSensorSDS()`, `fetchSensorDHT()`, `fetchSensorBMX280()`, `fetchSensorGPS()`: field production rules

### Runtime configuration

- `airrohr-firmware/airrohr-cfg.h`
  - persisted config key names and types
- `airrohr-firmware/airrohr-firmware.ino`
  - `readConfig()`
  - `writeConfig()`
  - `webserver_config_send_body_post()`
  - `add_form_input()`

## 11. Open Questions / Unclear Areas

- The exact server-side validation logic for Sensor.Community, Madavi, OpenSenseMap, Feinstaub App, and aircms. Not clear from repository contents.
- Whether any backend requires all current headers or only a subset. Not clear from repository contents.
- Whether `software_version` is informational only or part of compatibility checks. Not clear from repository contents.
- The exact float precision emitted by `String(value)` across all supported frameworks and targets. Not clear from repository contents.
- Whether the current ESP32 HTTPS-related logger code path is intentional or an incomplete implementation. Not clear from repository contents.
- Whether any backend depends on the order of `sensordatavalues` entries. Not clear from repository contents.
- Whether OTA server responses contain additional semantics beyond the downloaded bytes and MD5 values consumed here. Not clear from repository contents.
- The exact NTP transport details beyond the hostnames passed to `configTime()`. Not clear from repository contents.

## 12. Suggested Next Step for Replacement Firmware

The safest replacement strategy, based strictly on this repository, is:

1. Reproduce Sensor.Community first.
   Preserve the endpoint, `POST` method, JSON envelope, `X-PIN` mapping, `X-Sensor`, `X-MAC-ID`, and the exact `value_type` names after prefix stripping.

2. Reproduce the aggregate JSON builder second.
   Preserve the same `software_version` plus `sensordatavalues` envelope and the same aggregate field names, then reuse that body for Madavi, OpenSenseMap, and Feinstaub App.

3. Treat `value` as a string field everywhere in JSON.
   Do not change aggregate JSON numbers into native JSON numbers unless live-server validation proves that to be safe.

4. Keep the request split model.
   Sensor.Community is per-sensor-family; the secondary backends are per-interval aggregate uploads.

5. Preserve custom and Influx quirks exactly if compatibility with existing deployments matters.
   That includes top-level `esp8266id` for custom JSON and `application/x-www-form-urlencoded` for Influx uploads.

6. Validate risky areas against live targets before release.
   The highest-risk areas are header sensitivity, float formatting precision, aircms digest handling, and any server interpretation of `software_version`, `X-MAC-ID`, or metadata entries.

## Do Not Change in a Compatible Reimplementation

- Sensor.Community host, path, method, per-sensor request split, and `X-PIN` mapping
- JSON envelope keys: `software_version` and `sensordatavalues`
- Per-reading object keys: `value_type` and `value`
- String encoding of JSON `value` fields
- `X-Sensor` and `X-MAC-ID` header names and value construction
- Aggregate field names used for Madavi/OpenSenseMap/Feinstaub App/custom/Influx
- Prefix stripping behavior for Sensor.Community field names
- Custom API top-level field name `esp8266id`
- Influx tag name `node` and the use of aggregate `value_type` names as field names
- OpenSenseMap path shape `/boxes/<senseboxid>/data?luftdaten=1`
- aircms URL/body construction and digest formula
