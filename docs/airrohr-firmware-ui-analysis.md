# airrohr-firmware ESP32 UI and Configuration Interface Analysis

## Evidence Legend

- Confirmed: directly supported by repository code, constants, or embedded assets.
- Inferred: derived from surrounding implementation or library conventions, but not spelled out directly in this repository.
- Unclear: `Not clear from repository contents.`

# 1. Scope

## What was analyzed

This analysis is limited to the device-hosted configuration and status interface implemented by `airrohr-firmware`, with emphasis on the ESP32 code path and the web UI exposed by the running device.

## Primary files and directories examined

- `airrohr-firmware/airrohr-firmware.ino`
- `airrohr-firmware/html-content.h`
- `airrohr-firmware/airrohr-cfg.h`
- `airrohr-firmware/ext_def.h`
- `airrohr-firmware/defines.h`
- `airrohr-firmware/utils.cpp`
- `airrohr-firmware/utils.h`
- `airrohr-firmware/intl.h`
- `airrohr-firmware/intl_*.h`
- `airrohr-firmware/airrohr-logo-common.h`
- `airrohr-firmware/platformio.ini`

## In scope

- Wi-Fi onboarding and fallback into configuration mode
- SoftAP behavior
- DNS redirection / captive-portal behavior
- Web server startup and route registration
- HTML/CSS/JS generation for the device UI
- GET/POST handling for configuration and related pages
- Configuration parsing and persistence
- Runtime effects of saved settings, especially on ESP32
- Security/access-control behavior of the device UI

## Out of scope

- Sensor readout internals except where config settings directly affect them
- Cloud/backend API protocol details except where UI fields map to them
- The separate `airrohr-update-loader` project
- OLED/LCD display rendering internals except where the web UI configures those features

# 2. UI Exposure Model

## Boot and availability model

- Confirmed: `setup()` calls `init_config()`, then `connectWifi()`, then `setup_webserver()` in `airrohr-firmware.ino`.
- Confirmed: `connectWifi()` first attempts station mode with saved `cfg::wlanssid` / `cfg::wlanpwd`.
- Confirmed: if station connection is not established after `waitForWifiToConnect(40)`, `connectWifi()` calls `wifiConfig()`.

## Access point mode

- Confirmed: `wifiConfig()` switches to `WiFi.mode(WIFI_AP)`.
- Confirmed: it configures the AP interface to `192.168.4.1/24` via `WiFi.softAPConfig(...)`.
- Confirmed: it starts a soft AP with SSID `cfg::fs_ssid` and password `cfg::fs_pwd` via `WiFi.softAP(cfg::fs_ssid, cfg::fs_pwd, selectChannelForAp())`.
- Confirmed: if `cfg::fs_ssid` is empty, startup code generates a default SSID as `airRohr-<chipid>` using `SSID_BASENAME` from `defines.h`.
- Confirmed: default AP password comes from `FS_PWD` in `ext_def.h`, which is `"airrohrcfg"`.
- Confirmed: AP channel is chosen by `selectChannelForAp()`, which compares observed RSSI on channels 1, 6, and 11 and picks the least occupied of those three.

## Captive-portal style behavior

- Confirmed: `wifiConfig()` starts `DNSServer dnsServer; dnsServer.start(53, "*", apIP);`, so all DNS queries are answered with `192.168.4.1`.
- Confirmed: `/generate_204` and `/fwlink` are registered directly to `webserver_config`.
- Confirmed: `webserver_not_found()` redirects all unknown paths to `http://192.168.4.1/config` while the device is not connected to Wi-Fi.
- Confirmed: `webserver_not_found()` special-cases URIs containing `success.html` or `detect.html` and returns an HTML page with `window.location = "http://192.168.4.1/config";`.

## Local web interface

- Confirmed: the device always uses an on-device HTTP server on port 80.
- Confirmed: on ESP32 the server type is `WebServer server(80);`.
- Confirmed: during AP-based configuration, `setup_webserver()` is called inside `wifiConfig()`.
- Confirmed: after normal startup, `setup_webserver()` is called again after `connectWifi()`.
- Confirmed: the server is serviced in the main loop with `server.handleClient();`.

## Discovery and entry points

- Confirmed: during AP setup mode, the user can reach the UI at `http://192.168.4.1/config`.
- Confirmed: requesting `/` while not connected to Wi-Fi triggers a redirect to that URL.
- Confirmed: once connected in station mode, the web server remains available on the station IP returned by `WiFi.localIP()`.
- Confirmed: after successful station connection, `MDNS.begin(cfg::fs_ssid)` is called and the HTTP service is advertised with TXT key `PATH=/config`.
- Inferred: the mDNS hostname is intended to match `cfg::fs_ssid`, likely using normal mDNS host resolution semantics.
- Unclear: `Not clear from repository contents.` which exact browser URL users are expected to type for mDNS access on different client platforms.

## Conditions under which the UI is available

- Confirmed: in AP onboarding mode, the config UI is available only while `wifiConfig()` is running.
- Confirmed: AP config mode times out when `millis() - last_page_load >= cfg::time_for_wifi_config + 500`.
- Confirmed: `last_page_load` is refreshed by `start_html_page()` and by `webserver_not_found()`.
- Confirmed: in normal station mode, the HTTP server remains active after startup.
- Confirmed: if AP config mode times out, the code leaves AP mode, tries station mode again, and does not re-enter AP mode a second time in the same `connectWifi()` call.

# 3. UI Architecture

## Server-side rendering model

- Confirmed: the UI is server-rendered HTML assembled in C++ strings.
- Confirmed: there is no filesystem-hosted frontend bundle and no SPA framework.
- Confirmed: most HTML comes from `html-content.h` constants plus dynamic string assembly in `airrohr-firmware.ino`.
- Confirmed: the config page is rendered by `webserver_config_send_body_get()`.

## Web server implementation

- Confirmed: ESP32 uses the Arduino `WebServer` library.
- Confirmed: route registration happens in `setup_webserver()`.
- Confirmed: pages are sent with chunked/paginated output using `server.setContentLength(CONTENT_LENGTH_UNKNOWN)` and repeated `server.sendContent(...)`.

## Embedded assets

- Confirmed: CSS is embedded in `WEB_PAGE_STATIC_CSS` in `html-content.h`.
- Confirmed: the logo and favicon come from `airrohr-logo-common.h`.
- Confirmed: assets are served by `webserver_static()` and `webserver_favicon()`.
- Confirmed: the CSS/logo route path is `"/" INTL_LANG "_s1"`, for example `/EN_s1` or `/DE_s1`, based on compile-time language macros.

## JavaScript behavior

- Confirmed: the config page injects inline JS from `WEB_CONFIG_SCRIPT`.
- Confirmed: `setSSID(ssid)` copies a clicked SSID from the Wi-Fi list into the `wlanssid` field and focuses `wlanpwd`.
- Confirmed: `load_wifi_list()` performs `GET /wifi` and inserts the response into `<div id='wifilist'>`.
- Confirmed: another inline script disables `current_lang` and `use_beta` controls unless `auto_update` is checked.
- Confirmed: the debug page uses `fetch('/serial')` every 3 seconds to append log output.
- Confirmed: the iOS captive-portal response uses inline JS to redirect to `http://192.168.4.1/config`.

## CSS/layout system

- Confirmed: styling is minimal and embedded, using plain CSS classes such as `.b`, `.tabs`, `.tab`, `.panels`, `.panel`, `.wifi`.
- Confirmed: the config page tabs are implemented with four hidden radio inputs (`r1`..`r4`) plus CSS selectors that show `#panel1`..`#panel4`.
- Confirmed: no external fonts, icon packs, or client-side component libraries are present.

# 4. Navigation Map

## High-level navigation

- Confirmed: in station mode, `/` is the menu/landing page.
- Confirmed: in AP config mode, `/` redirects to `/config` on `192.168.4.1`.
- Confirmed: all HTML pages use the shared page header/footer, and the footer always includes a back-to-home link to `/`.
- Confirmed: the header logo also links to `/`.

## Menu from `/`

- Confirmed: `WEB_ROOT_PAGE_CONTENT` links to:
  - `/values`
  - `/status`
  - `https://maps.sensor.community/`
  - `/config`
  - `/removeConfig`
  - `/reset`
  - `/debug`

## Configuration page internal navigation

- Confirmed: `/config` contains four tabs:
  - `WiFi Settings`
  - `More settings`
  - `Sensors`
  - `APIs`
- Confirmed: all tabs belong to one `<form method='POST' action='/config'>`.
- Confirmed: the page ends with one submit button labeled `Save configuration and restart`.

## Conditional or mode-specific navigation

- Confirmed: the Wi-Fi scan list placeholder `<div id='wifilist'>` is only present when `wificonfig_loop` is true.
- Confirmed: AP SSID/AP password fields (`fs_ssid`, `fs_pwd`) are only shown when `wificonfig_loop` is false.
- Confirmed: `/generate_204` and `/fwlink` are alternative entry points into the config page.
- Confirmed: `/wifi` is an AJAX fragment endpoint, not a full page.

## Route table

| Route | Registration | Main purpose | Mode notes |
| --- | --- | --- | --- |
| `/` | `server.on("/", webserver_root)` | Main menu | Redirects to AP config if not connected |
| `/config` | `server.on("/config", webserver_config)` | Main configuration UI | GET renders, non-GET path handled as save/restart flow |
| `/wifi` | `server.on("/wifi", webserver_wifi)` | Wi-Fi network list fragment | Mainly used during AP config mode |
| `/values` | `server.on("/values", webserver_values)` | Current sensor values page | Redirects to AP config if not connected |
| `/status` | `server.on("/status", webserver_status)` | Device status page | Redirects to AP config if not connected |
| `/generate_204` | `server.on("/generate_204", webserver_config)` | Captive-portal entry point | AP-oriented |
| `/fwlink` | `server.on("/fwlink", webserver_config)` | Captive-portal entry point | AP-oriented |
| `/debug` | `server.on("/debug", webserver_debug_level)` | Debug log page and log level controls | Available in station mode |
| `/serial` | `server.on("/serial", webserver_serial)` | Plain-text log stream | Polled by `/debug` |
| `/removeConfig` | `server.on("/removeConfig", webserver_removeConfig)` | Confirm and delete config file | No reboot built into delete action |
| `/reset` | `server.on("/reset", webserver_reset)` | Confirm and reboot | POST restarts immediately |
| `/data.json` | `server.on("/data.json", webserver_data_json)` | Machine-readable sensor data | Not a human UI page |
| `/metrics` | `server.on("/metrics", webserver_metrics_endpoint)` | Metrics text endpoint | Not a human UI page |
| `/favicon.ico` | `server.on("/favicon.ico", webserver_favicon)` | Favicon/logo | Shared PNG |
| `/<INTL_LANG>_s1` | `server.on(F(STATIC_PREFIX), webserver_static)` | CSS/logo asset multiplexer | Uses query `r=css` or `r=logo` |

# 5. Route and Page Inventory

## Human-facing routes

| Route | Handler | Registered methods | Purpose | HTML / response source | User actions | Side effects |
| --- | --- | --- | --- | --- | --- | --- |
| `/` | `webserver_root()` | Registered without explicit method filter | Main menu | `WEB_ROOT_PAGE_CONTENT` | Navigate to pages | None |
| `/config` | `webserver_config()` | Registered without explicit method filter; handler branches on `server.method()` | Main configuration page | `webserver_config_send_body_get()`, `WEB_CONFIG_SCRIPT`, `form_*` helpers | Edit all config fields, save | POST updates config variables, writes `/config.json`, restarts on success |
| `/wifi` | `webserver_wifi()` | Registered without explicit method filter | Wi-Fi list fragment | Built in `webserver_wifi()` using `wlan_ssid_to_table_row()` | Click SSID to populate form | None |
| `/values` | `webserver_values()` | Registered without explicit method filter | Current data page | Built in `webserver_values()` | Read sensor values | None |
| `/status` | `webserver_status()` | Registered without explicit method filter | Device status page | Built in `webserver_status()` | Read status/errors | None |
| `/debug` | `webserver_debug_level()` | Registered without explicit method filter | Debug log viewer and debug level controls | Built in `webserver_debug_level()` | Change debug level via `?lvl=` links | Updates `cfg::debug` in RAM only |
| `/removeConfig` | `webserver_removeConfig()` | Registered without explicit method filter; handler branches on `server.method()` | Confirm and delete config file | `WEB_REMOVE_CONFIG_CONTENT` for GET | Confirm delete | POST deletes `/config.json` and `/config.json.old` if present |
| `/reset` | `webserver_reset()` | Registered without explicit method filter; handler branches on `server.method()` | Confirm and restart | `WEB_RESET_CONTENT` for GET | Confirm restart | POST calls `sensor_restart()` |

## Supporting routes

| Route | Handler | Response | Notes |
| --- | --- | --- | --- |
| `/serial` | `webserver_serial()` | Plain text from `Debug.popLines()` | Used by `/debug` polling script |
| `/data.json` | `webserver_data_json()` | JSON sensor snapshot with `age` | Exposed without page chrome |
| `/metrics` | `webserver_metrics_endpoint()` | Plain text metrics | Exposed without page chrome |
| `/favicon.ico` | `webserver_favicon()` | Embedded PNG | Same logo asset |
| `/<INTL_LANG>_s1?r=css` | `webserver_static()` | Embedded CSS | Compile-time route name |
| `/<INTL_LANG>_s1?r=logo` | `webserver_static()` | Embedded PNG | Shared logo |
| any unknown path in AP mode | `webserver_not_found()` | Redirect or iOS redirect HTML | Part of captive-portal behavior |

## Notes on route behavior

- Confirmed: the repository does not register separate GET-only or POST-only versions of routes.
- Confirmed: `/config`, `/removeConfig`, and `/reset` inspect `server.method()` internally.
- Unclear: `Not clear from repository contents.` how the underlying `WebServer` library resolves duplicate form parameters for checkbox fields with the same `name`.

# 6. Settings Model

## General settings model

- Confirmed: persisted settings are centralized in `namespace cfg` in `airrohr-firmware.ino`.
- Confirmed: key names, types, lengths, and storage pointers are defined in `configShape[]` in `airrohr-cfg.h`.
- Confirmed: input `name=` values match the persisted JSON keys.
- Confirmed: changing those keys would break compatibility with the existing `config.json` schema.

## Tab 1: WiFi Settings

| UI label | Key | Control | Default / initial value | Save-time handling | Confirmed runtime effect |
| --- | --- | --- | --- | --- | --- |
| Network name | `wlanssid` | text | `WLANSSID` in `ext_def.h`, default `"Freifunk-disabled"` | copied with `strncpy`, max 34 chars shown | used by `WiFi.begin(...)` in station mode |
| Password | `wlanpwd` | password | `WLANPWD` in `ext_def.h`, default empty | only overwritten if submitted value is non-empty | used by `WiFi.begin(...)` in station mode |
| Authentication | `www_basicauth_enabled` | checkbox | `WWW_BASICAUTH_ENABLED`, default `0` | parsed as bool | gates selected web pages via `webserver_request_auth()` outside config-loop mode |
| User | `www_username` | text | `WWW_USERNAME`, default `"admin"` | copied with `strncpy` | used by `server.authenticate(...)` when BasicAuth is enabled |
| Password | `www_password` | password | `WWW_PASSWORD`, default empty | only overwritten if non-empty | used by `server.authenticate(...)` when BasicAuth is enabled |
| Network name | `fs_ssid` | text | compile-time `FS_SSID`; if empty, generated as `airRohr-<chipid>` | copied with `strncpy` | used as SoftAP SSID; also used as station hostname and mDNS name |
| Password | `fs_pwd` | password | `FS_PWD`, default `"airrohrcfg"` | only overwritten if non-empty | used as SoftAP password |

Notes:

- Confirmed: `fs_ssid` and `fs_pwd` are hidden while `wificonfig_loop` is true.
- Confirmed: password inputs are intentionally rendered blank on GET and preserved unless a non-empty replacement is posted.

## Tab 2: More settings

| UI label | Key | Control | Default / initial value | Save-time handling | Confirmed runtime effect on ESP32 |
| --- | --- | --- | --- | --- | --- |
| OLED SSD1306 | `has_display` | checkbox | `HAS_DISPLAY`, default `0` | bool | affects display initialization and screen rotation logic |
| OLED SH1106 | `has_sh1106` | checkbox | `HAS_SH1106`, default `0` | bool | affects display initialization |
| OLED display flip | `has_flipped_display` | checkbox | `HAS_FLIPPED_DISPLAY`, default `0` | bool | affects display orientation logic |
| LCD 1602 (0x27) | `has_lcd1602_27` | checkbox | `HAS_LCD1602_27`, default `0` | bool | affects LCD init |
| LCD 1602 (0x3F) | `has_lcd1602` | checkbox | `HAS_LCD1602`, default `0` | bool | affects LCD init |
| LCD 2004 (0x27) | `has_lcd2004_27` | checkbox | `HAS_LCD2004_27`, default `0` | bool | affects LCD init |
| LCD 2004 (0x3F) | `has_lcd2004` | checkbox | `HAS_LCD2004`, default `0` | bool | affects LCD init |
| Display Wifi info | `display_wifi_info` | checkbox | `DISPLAY_WIFI_INFO`, default `1` | bool | controls whether Wi-Fi info screens are included on attached displays |
| Display device info | `display_device_info` | checkbox | `DISPLAY_DEVICE_INFO`, default `1` | bool | controls whether device info screens are included on attached displays |
| IP address | `static_ip` | text | empty | copied with `strncpy` | stored, but ESP32 code path does not apply it in `connectWifi()` |
| Subnet | `static_subnet` | text | empty | copied with `strncpy` | stored, but ESP32 code path does not apply it in `connectWifi()` |
| Gateway | `static_gateway` | text | empty | copied with `strncpy` | stored, but ESP32 code path does not apply it in `connectWifi()` |
| DNS server | `static_dns` | text | empty | copied with `strncpy` | stored, but ESP32 code path does not apply it in `connectWifi()` |
| Auto update firmware | `auto_update` | checkbox | `AUTO_UPDATE`, default `1` | bool | stored, but `twoStageOTAUpdate()` has no ESP32 implementation body |
| Load beta firmware | `use_beta` | checkbox | `USE_BETA`, default `0` | bool | stored, but no ESP32 OTA body uses it |
| Language | `current_lang` | select | compile-time `CURRENT_LANG` | copied with `strncpy` | stored, but no ESP32 UI rendering path uses it |
| Power saving | `powersave` | checkbox | default-initialized to `false` unless overridden by config | bool | on ESP32 it affects loop delay behavior; ESP8266 has additional Wi-Fi sleep handling not present for ESP32 |
| Debug level | `debug` | number | `DEBUG`, default `3` | `toInt()` | changes debug verbosity |
| Measuring interval (sec) | `sending_intervall_ms` | number | `145000 ms` shown as `145` | posted seconds are multiplied by 1000 | affects sampling / send interval; minimum forced to `READINGTIME_SDS_MS` when config is read |
| Duration router mode | `time_for_wifi_config` | number | `600000 ms` shown as `600` | posted seconds are multiplied by 1000 | controls AP config-mode timeout |

Notes:

- Confirmed: the static-IP explanatory text says all four fields must be completed.
- Confirmed: actual static-IP application is inside an `#if defined(ESP8266)` block in `connectWifi()`.
- Confirmed: a client-side script disables `current_lang` and `use_beta` when `auto_update` is unchecked.
- Confirmed: the compile-time page language still comes from `INTL_LANG` / `intl_*.h`, not from `cfg::current_lang`.

## Tab 3: Sensors

| UI label | Key | Control | Default | Save-time handling | Confirmed runtime effect |
| --- | --- | --- | --- | --- | --- |
| SDS011 | `sds_read` | checkbox | `1` | bool | enables SDS011 read/send/status paths |
| Honeywell PM | `hpm_read` | checkbox | `0` | bool | enables HPM read/send/status paths |
| Sensirion SPS30 | `sps30_read` | checkbox | `0` | bool | enables SPS30 read/send/status paths |
| DHT22 | `dht_read` | checkbox | `0` | bool | enables DHT read/send/status paths |
| HTU21D | `htu21d_read` | checkbox | `0` | bool | enables HTU21D read/send/status paths |
| BME280/BMP280 | `bmx280_read` | checkbox | `1` | bool | enables BME/BMP280 read/send/status paths |
| SHT3X | `sht3x_read` | checkbox | `0` | bool | enables SHT3X read/send/status paths |
| SCD30 | `scd30_read` | checkbox | `0` | bool | enables SCD30 read/send/status paths |
| DNMS | `dnms_read` | checkbox | `0` | bool | enables DNMS read/send/status paths |
| correction in dB(A) | `dnms_correction` | text | `"0.0"` | copied with `strncpy` | used by DNMS correction logic |
| Correction in °C | `temp_correction` | text | `"0.0"` | copied with `strncpy` | used as temperature offset on several sensors |
| Above sea level (m) | `height_above_sealevel` | text | `"0"` | copied with `strncpy` | used in sea-level pressure calculation |
| DS18B20 | `ds18b20_read` | checkbox | `0` | bool | enables DS18B20 read/send/status paths |
| PMS(1,3,5,6,7)003 | `pms_read` | checkbox | `0` | bool | enables PMS read/send/status paths |
| Tera Sensor Next PM | `npm_read` | checkbox | `0` | bool | selects NPM serial path and read/send/status behavior |
| Next PM fulltime | `npm_fulltime` | checkbox | `0` | bool | changes NPM warmup/continuous-running behavior |
| Piera Systems IPS-7100 | `ips_read` | checkbox | `0` | bool | selects IPS serial path and read/send/status behavior |
| BMP180 | `bmp_read` | checkbox | `0` | bool | enables BMP180 read/send/status paths |
| GPS (NEO 6M) | `gps_read` | checkbox | `0` | bool | enables GPS serial init and read/send/status behavior |

Hidden but related:

- Confirmed: `Config_ppd_read` / key `ppd_read` exists in `configShape[]`.
- Confirmed: `webserver_config_send_body_get()` does not render a PPD42NS checkbox.
- Confirmed: `ppd_read` can still be loaded from and written to `config.json` if already present in memory.

## Tab 4: APIs

| UI label | Key | Control | Default | Save-time handling | Confirmed runtime effect |
| --- | --- | --- | --- | --- | --- |
| Sensor.Community | `send2dusti` | checkbox | `1` | bool | enables main Sensor.Community upload |
| HTTPS | `ssl_dusti` | checkbox | `0` | bool | changes Sensor.Community destination port/session choice |
| Madavi.de | `send2madavi` | checkbox | `1` | bool | enables Madavi upload |
| HTTPS | `ssl_madavi` | checkbox | `0` | bool | changes Madavi destination port/session choice |
| CSV | `send2csv` | checkbox | `0` | bool | enables CSV logging/output path |
| Feinstaub-App | `send2fsapp` | checkbox | `0` | bool | enables FS app upload |
| aircms.online | `send2aircms` | checkbox | `0` | bool | enables aircms upload |
| OpenSenseMap.org | `send2sensemap` | checkbox | `0` | bool | enables senseBox upload when `senseboxid` is non-empty |
| senseBox ID | `senseboxid` | text | empty | copied with `strncpy` | inserted into OpenSenseMap upload path |
| Send data to custom API | `send2custom` | checkbox | `0` | bool | enables custom API upload |
| HTTPS | `ssl_custom` | checkbox | `0` | bool | causes TLS session setup for custom API when true or when port is 443 |
| Server | `host_custom` | text | `"192.168.234.1"` | copied with `strncpy` | destination host for custom API |
| Path | `url_custom` | text | `"/data.php"` | copied with `strncpy` | destination path for custom API |
| Port | `port_custom` | number | `80` | `toInt()` | destination port for custom API |
| User | `user_custom` | text | empty | copied with `strncpy` | included in custom API auth payload logic |
| Password | `pwd_custom` | password | empty | only overwritten if non-empty | included in custom API auth payload logic |
| Send to InfluxDB | `send2influx` | checkbox | `0` | bool | enables InfluxDB write path |
| HTTPS | `ssl_influx` | checkbox | `0` | bool | causes TLS session setup for InfluxDB |
| Server | `host_influx` | text | `"influx.server"` | copied with `strncpy` | destination host for InfluxDB |
| Path | `url_influx` | text | `"/write?db=sensorcommunity"` | copied with `strncpy` | destination path for InfluxDB |
| Port | `port_influx` | number | `8086` | `toInt()` | destination port for InfluxDB |
| User | `user_influx` | text | empty | copied with `strncpy` | included in InfluxDB auth logic |
| Password | `pwd_influx` | password | empty | only overwritten if non-empty | included in InfluxDB auth logic |
| Measurement | `measurement_name_influx` | text | `"feinstaub"` | copied with `strncpy` | used as InfluxDB measurement name |

## Validation and dependencies summary

- Confirmed: booleans are compared to `"1"` in the POST handler.
- Confirmed: integer/time fields use `String::toInt()`.
- Confirmed: time fields are entered in seconds in the UI and stored in milliseconds.
- Confirmed: string fields are truncated to their configured buffer length.
- Confirmed: password fields preserve the previous stored value when the submitted field is blank.
- Confirmed: `sending_intervall_ms` is forced to at least `READINGTIME_SDS_MS` when a config file is read.
- Confirmed: static-IP settings are only applied when all four strings parse as IP addresses, and only in the ESP8266 branch.
- Confirmed: OpenSenseMap upload also depends on `senseboxid[0] != '\0'`.
- Confirmed: NPM-specific behavior only matters when `npm_read` is enabled.
- Confirmed: the UI does not enforce most cross-field dependencies beyond the OTA checkbox script.

# 7. Form Submission and Persistence

## Form submission model

- Confirmed: the main config form uses `method='POST' action='/config'`.
- Confirmed: `webserver_config()` handles both render and save.
- Confirmed: the remove-config page uses `method='POST' action='/removeConfig'`.
- Confirmed: the reset page intends to use a POST form to `/reset`.

## Request parsing

- Confirmed: `webserver_config_send_body_post()` iterates over every `configShape[]` entry.
- Confirmed: for each key, it checks `server.hasArg(key)` and reads `server.arg(key)`.
- Confirmed: `Config_Type_UInt` uses `toInt()`.
- Confirmed: `Config_Type_Time` uses `toInt() * 1000`.
- Confirmed: `Config_Type_String` copies the submitted string and truncates to the configured max length.
- Confirmed: `Config_Type_Password` only updates the stored string if a non-empty value was submitted.

## Persistence backend

- Confirmed: persistence uses SPIFFS, not NVS/Preferences/EEPROM.
- Confirmed: current config is stored in `/config.json`.
- Confirmed: before writing a new config, the code renames `/config.json` to `/config.json.old`.
- Confirmed: `readConfig()` falls back to `/config.json.old` if `/config.json` cannot be opened or parsed.
- Confirmed: `writeConfig()` serializes every `configShape[]` entry plus `SOFTWARE_VERSION`.

## Save/apply/restart behavior

- Confirmed: after POST `/config`, the response body says `Sensor is rebooting.`.
- Confirmed: after sending the page, `webserver_config()` calls `writeConfig()` and then `sensor_restart()` if the write succeeded.
- Confirmed: config changes are therefore applied by reboot rather than by in-place reconfiguration.
- Confirmed: `sensor_restart()` ends SPIFFS, closes the active PM serial port, and calls `ESP.restart()`.

## Error and success handling

- Confirmed: there is no field-level validation message on the config page.
- Confirmed: there is no explicit UI error page when `writeConfig()` fails.
- Confirmed: `removeConfig` displays success/failure messages depending on file deletion outcome.
- Confirmed: `reset` POST does not display a success page; it calls `sensor_restart()` directly.

# 8. Wi-Fi and Onboarding Flow

## First boot / no usable Wi-Fi

- Confirmed: with no working station credentials, `connectWifi()` eventually calls `wifiConfig()`.
- Confirmed: `wifiConfig()` first disconnects station mode and performs one Wi-Fi scan.
- Confirmed: the scan results are cached in `wifiInfo[]` and reused by `/wifi`; there is no rescan endpoint.
- Confirmed: the device then starts SoftAP mode and the config web server.

## End-user flow in setup mode

1. Confirmed: the user connects to the AP named `cfg::fs_ssid`.
2. Confirmed: if `cfg::fs_ssid` was not previously configured, that defaults to `airRohr-<chipid>`.
3. Confirmed: the AP password defaults to `airrohrcfg`.
4. Confirmed: DNS wildcarding and the redirect handlers steer browsers to `http://192.168.4.1/config`.
5. Confirmed: the config page shows Wi-Fi credentials first and includes a clickable list of scanned SSIDs loaded from `/wifi`.
6. Confirmed: clicking an SSID fills `wlanssid` and focuses the Wi-Fi password field.
7. Confirmed: submitting the form writes config and restarts the device.
8. Confirmed: after reboot, startup tries station mode again using the saved credentials.

## Wi-Fi list behavior

- Confirmed: `/wifi` sorts networks by RSSI descending.
- Confirmed: it deduplicates identical SSIDs by name.
- Confirmed: on ESP8266 it filters out hidden SSIDs.
- Confirmed: on ESP32 there is no hidden-SSID filter in this repository's code path.
- Confirmed: each row shows SSID, a lock indicator for encrypted networks, and a computed signal quality percentage.

## Fallback if configuration mode times out

- Confirmed: AP mode runs until `time_for_wifi_config`.
- Confirmed: on timeout, `wifiConfig()` stops the DNS server, disconnects the AP, switches back to station mode, and calls `WiFi.begin(...)` using the current `wlanssid` / `wlanpwd`.
- Confirmed: if the device still does not connect after the later `waitForWifiToConnect(20)` in `connectWifi()`, there is no second AP fallback within that same startup path.

## Returning to configuration later

- Confirmed: once the device is on the normal network, the config page remains available at `/config` on the station IP.
- Confirmed: the server also starts mDNS with service path `/config`.
- Confirmed: deleting the config via `/removeConfig` and then rebooting will cause the next boot to fall back to AP onboarding unless compile-time station defaults happen to work.
- Unclear: `Not clear from repository contents.` whether any hardware button or non-web trigger exists to force config mode.

# 9. Captive Portal / Discovery Behavior

## Confirmed captive/discovery mechanisms

- Confirmed: wildcard DNS via `DNSServer` during AP config mode.
- Confirmed: absolute redirect target is always `http://192.168.4.1/config`.
- Confirmed: `/generate_204` is mapped to the config handler.
- Confirmed: `/fwlink` is mapped to the config handler.
- Confirmed: URIs containing `success.html` or `detect.html` return an HTML+JS redirect page.
- Confirmed: `/` redirects to AP config when `WiFi.status() != WL_CONNECTED`.

## Discovery after station join

- Confirmed: station hostname is set from `cfg::fs_ssid` on ESP32 using `WiFi.setHostname(cfg::fs_ssid)`.
- Confirmed: mDNS service is started with the same name and advertises HTTP path `/config`.
- Inferred: clients that support mDNS are intended to find the device by that hostname.
- Unclear: `Not clear from repository contents.` which client OS/browser combinations were expected to auto-discover the mDNS name.

## Device IP assumptions

- Confirmed: AP-mode redirects hard-code `192.168.4.1`.
- Confirmed: station-mode pages display `WiFi.localIP()` in the header/status or on attached displays.

# 10. UI Assets and Presentation

## HTML structure source

- Confirmed: shared header markup is in `WEB_PAGE_HEADER`, `WEB_PAGE_HEADER_HEAD`, and `WEB_PAGE_HEADER_BODY`.
- Confirmed: shared footer markup is in `WEB_PAGE_FOOTER`.
- Confirmed: page content is composed procedurally in each handler.
- Confirmed: forms use simple `<table>` layouts and plain inputs.

## CSS

- Confirmed: all CSS is embedded in `WEB_PAGE_STATIC_CSS`.
- Confirmed: the stylesheet is served from `STATIC_PREFIX + "?r=css"`.
- Confirmed: the config UI uses CSS-only tabs implemented with hidden radio inputs.

## JavaScript

- Confirmed: all JS is inline.
- Confirmed: there are no standalone JS files.
- Confirmed: JS is limited to Wi-Fi list loading, SSID autofill, OTA-option disabling, debug log polling, and AP redirect handling.

## Icons/images/fonts

- Confirmed: the logo/favicon is a PNG embedded from `airrohr-logo-common.h`.
- Confirmed: the Wi-Fi list uses a literal lock glyph string in `webserver_wifi()`.
- Confirmed: no custom fonts are embedded or loaded.

## Localization resources

- Confirmed: UI strings come from compile-time `intl_*.h` headers selected by `intl.h`.
- Confirmed: `platformio.ini` selects languages through build flags like `-DINTL_DE`, `-DINTL_EN`, etc.
- Confirmed: the runtime-saved `current_lang` value does not change which `intl_*.h` header is compiled into the firmware.

## Richness level

- Confirmed: this is a minimal server-rendered HTML UI, not a rich frontend application.

# 11. Security and Access Control

## Authentication

- Confirmed: optional HTTP Basic Auth is implemented by `webserver_request_auth()`.
- Confirmed: Basic Auth uses `cfg::www_username` and `cfg::www_password`.
- Confirmed: Basic Auth is only applied when `cfg::www_basicauth_enabled` is true and `wificonfig_loop` is false.
- Confirmed: the AP onboarding config flow bypasses Basic Auth even if the setting is enabled.

## Which routes are protected

- Confirmed: `/`, `/config`, `/debug`, `/removeConfig`, and `/reset` explicitly call `webserver_request_auth()`.
- Confirmed: `/values`, `/status`, `/serial`, `/wifi`, `/data.json`, `/metrics`, static assets, and favicon do not call `webserver_request_auth()`.

## Input handling and sanitization

- Confirmed: string inputs are length-limited by buffer size and null-terminated.
- Confirmed: single quotes are HTML-escaped when rendering text values back into input fields.
- Confirmed: there is no CSRF token or origin checking.
- Confirmed: there is no server-side validation for IP formats, hostname formats, numeric ranges beyond integer parsing, or auth password strength.

## Sensitive settings handling

- Confirmed: password fields are not echoed back into the HTML form.
- Confirmed: blank password submissions preserve the existing stored password.

## Reset and delete exposure

- Confirmed: deleting the configuration does not require any hardware confirmation.
- Confirmed: reboot is exposed as a web route.

# 12. Compatibility-Critical UI Behavior

## Definitely required

- AP onboarding should use SoftAP mode with local UI on `192.168.4.1`.
- AP onboarding should include wildcard DNS plus redirect behavior to `/config`.
- `cfg::fs_ssid` should serve double duty as AP SSID and normal-network hostname/mDNS name.
- The config UI should keep the main route names `/`, `/config`, `/wifi`, `/values`, `/status`, `/removeConfig`, `/reset`, `/debug`.
- Input `name=` parameters should remain aligned with the JSON keys from `airrohr-cfg.h` such as `wlanssid`, `wlanpwd`, `fs_ssid`, `send2dusti`, `measurement_name_influx`.
- POST `/config` should persist settings and restart the device.
- Password fields should preserve their previous stored value when left blank on save.
- During AP onboarding, Basic Auth should not block access.
- `/wifi` should provide clickable SSID rows that populate the Wi-Fi SSID field.

## Probably required

- `/generate_204` and `/fwlink` compatibility routes should be preserved if captive-portal behavior matters.
- `success.html` / `detect.html` redirect handling should be preserved if matching mobile captive-portal behavior matters.
- The four-tab layout and field grouping should be preserved if the goal is user-experience compatibility rather than only data compatibility.
- The compile-time static asset route shape `/<INTL_LANG>_s1?r=css|logo` is probably worth preserving if exact markup compatibility matters.
- Leaving `/values` and `/status` unauthenticated is part of the current behavior, even if it is arguably a security weakness.

## ESP32-specific compatibility caveats

- Confirmed: the UI exposes `static_ip`, `static_subnet`, `static_gateway`, and `static_dns`, but the ESP32 runtime path in this repository does not apply them.
- Confirmed: the UI exposes `auto_update`, `use_beta`, and `current_lang`, but `twoStageOTAUpdate()` is implemented only inside `#if defined(ESP8266)`.
- Confirmed: if you are reproducing observed ESP32 behavior exactly, preserving these fields as stored-but-effectively-inert settings is closer to current repository behavior than inventing ESP32 behavior for them.

## Unclear from repository contents

- `Not clear from repository contents.` whether exact mDNS client-side naming conventions are compatibility-critical for deployed users.
- `Not clear from repository contents.` whether browsers rely on the malformed reset form markup still submitting to `/reset`.
- `Not clear from repository contents.` how the underlying `WebServer` library resolves duplicate checkbox parameters in all supported firmware/library versions.

# 13. Implementation Map

## Core UI / onboarding code

- `airrohr-firmware/airrohr-firmware.ino`
  - `setup()`: boot order and server startup
  - `connectWifi()`: station connect, fallback to AP onboarding, hostname/mDNS
  - `wifiConfig()`: scan networks, SoftAP, DNS wildcard, AP-timeout loop
  - `setup_webserver()`: route registration
  - `webserver_root()`: main menu
  - `webserver_config()`: GET/POST config flow
  - `webserver_config_send_body_get()`: config page rendering
  - `webserver_config_send_body_post()`: config form parsing
  - `webserver_wifi()`: Wi-Fi list fragment
  - `webserver_values()`: current values page
  - `webserver_status()`: status page
  - `webserver_debug_level()`: debug page and level setting
  - `webserver_serial()`: plain log output
  - `webserver_removeConfig()`: config deletion
  - `webserver_reset()`: reboot flow
  - `webserver_not_found()`: captive redirect / 404 behavior
  - `readConfig()`, `writeConfig()`, `init_config()`: persistence
  - `sensor_restart()`: restart/apply mechanism

## HTML / assets / strings

- `airrohr-firmware/html-content.h`
  - shared page header/footer
  - root page links
  - config page helper JS
  - remove/reset confirmation snippets
  - CSS
  - iOS redirect page
- `airrohr-firmware/airrohr-logo-common.h`
  - embedded PNG asset
- `airrohr-firmware/utils.cpp`
  - `wlan_ssid_to_table_row()`
  - `add_table_row_from_value()`
  - `calcWiFiSignalQuality()`
  - `add_sensor_type()`

## Config schema / keys

- `airrohr-firmware/airrohr-cfg.h`
  - `ConfigShapeEntry`
  - `ConfigShapeId`
  - `CFG_KEY_*`
  - `configShape[]`

## Defaults and compile-time behavior

- `airrohr-firmware/ext_def.h`
  - default station/AP/auth settings
  - default sensor/API/display booleans
  - default upload endpoints
- `airrohr-firmware/defines.h`
  - `SSID_BASENAME`
  - `SENSOR_BASENAME`
  - `OTA_BASENAME`
  - config field lengths
- `airrohr-firmware/intl.h`
  - compile-time language header selection
- `airrohr-firmware/intl_*.h`
  - localized text constants
- `airrohr-firmware/platformio.ini`
  - language build flags
  - ESP32 library selections

# 14. Open Questions / Unclear Areas

- `Not clear from repository contents.` whether a hardware-only trigger exists to force configuration mode.
- `Not clear from repository contents.` whether the intended station-mode mDNS URL format is documented anywhere else for users.
- `Not clear from repository contents.` whether the malformed reset form action in `WEB_RESET_CONTENT` changes behavior in any supported browser.
- `Not clear from repository contents.` how duplicate checkbox parameters are resolved by every `WebServer`/`ESP8266WebServer` version this project targets.
- `Not clear from repository contents.` whether any ESP32-specific build currently ships this UI in production; `platformio.ini` contains ESP32 environments but they are named with `DISABLEDenv:`.

# 15. Suggested Next Step for Reimplementation

Based strictly on repository evidence, the safest replacement strategy is:

1. Recreate the onboarding path first.
   - Implement station-connect-first behavior.
   - On failure, start a SoftAP at `192.168.4.1`, run wildcard DNS, and serve `/config`.
   - Preserve `fs_ssid` / `fs_pwd` semantics and route compatibility.

2. Recreate the server-side route surface next.
   - Keep `/`, `/config`, `/wifi`, `/values`, `/status`, `/removeConfig`, `/reset`, `/debug`.
   - Preserve `/generate_204`, `/fwlink`, and not-found redirect behavior if captive-portal compatibility matters.

3. Preserve the persisted config schema.
   - Keep the JSON file name `/config.json`.
   - Keep the backup file `/config.json.old`.
   - Keep existing key names from `airrohr-cfg.h`.
   - Preserve blank-password-means-no-change behavior.

4. Recreate the config form semantics conservatively.
   - Keep the same major field groups and field names.
   - Keep save-and-restart behavior rather than attempting live reconfiguration first.
   - Keep `/wifi` as a clickable SSID list endpoint populated from a scan cache.

5. Decide explicitly how to handle ESP32-only gaps.
   - If compatibility with this repository's observed ESP32 behavior is the goal, either keep `static_ip`, `auto_update`, `use_beta`, and `current_lang` as stored settings with the same limited/no ESP32 effect, or document any intentional change very clearly.

6. Validate on real hardware.
   - Confirm AP onboarding on phone and desktop clients.
   - Confirm captive-portal entry points.
   - Confirm SSID click-to-fill behavior.
   - Confirm POST `/config` persistence and reboot.
   - Confirm BasicAuth behavior in station mode versus onboarding mode.
   - Confirm whether exact browser behavior around the reset form and checkbox submission needs a compatibility shim.
