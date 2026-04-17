# Web UI

The firmware includes an embedded HTTP server that serves a multi-page configuration and monitoring interface. All pages are rendered server-side by template substitution — no client-side rendering framework is used.

---

## Server parameters

| Parameter | Value |
|-----------|-------|
| Port | `http_port` from `DeviceConfig` (default 80) |
| Task stack | 10 240 bytes |
| URI handlers | up to 12 |
| URI matching | wildcard (`httpd_uri_match_wildcard`) |
| Response caching | `Cache-Control: no-store` on all pages |
| Log tag | `air360.web` |

The server starts during boot step 9/9. A startup failure is fatal — the boot LED is set to the error state.

---

## Routes

| Method | Path | Handler |
|--------|------|---------|
| `GET` | `/` | Overview page |
| `GET` | `/diagnostics` | Diagnostics page |
| `GET` | `/assets/*` | Static assets (CSS, JS) |
| `GET` | `/wifi-scan` | JSON Wi-Fi scan results |
| `GET` / `POST` | `/config` | Device configuration page |
| `POST` | `/check-sntp` | SNTP server reachability check |
| `GET` / `POST` | `/sensors` | Sensor configuration page |
| `GET` / `POST` | `/backends` | Backend configuration page |

All HTML pages set `Content-Type: text/html; charset=utf-8`. JSON endpoints set `Content-Type: application/json`.

---

## Setup AP redirect

When the device is in `kSetupAp` mode (no station credentials, or station connection failed), `GET /`, `GET /sensors`, and `GET /backends` redirect with `302 Found` to `/config`. Navigation links on the config page are also hidden in this mode — only the device configuration form is shown.

---

## Page: Overview (`/`)

Displays a read-only dashboard. Refreshed on every page load (no auto-refresh).

**Health pill** — aggregated status (`Healthy` / `Unhealthy`) rendered inline under the page heading. No separate panel.

**Stats bar** — four cells:

| Stat | Content |
|------|---------|
| Mode | Network mode (`kStation` / `kSetupAp`) |
| Uplink | `wifi`, `cellular`, `cellular (connecting)`, or `offline`. When cellular is enabled it is always the primary uplink regardless of Wi-Fi state. |
| Uptime | Human-readable uptime since last boot |
| Boot Count | Total boot count from NVS |

**Connection section** — current connection details:
- Current date and time (UTC; shown from SNTP if synced, otherwise best-effort uptime-based estimate).
- Wi-Fi: SSID and current IP address (or `not connected`).
- Cellular (shown only when `cellular_enabled = 1`): PPP IP address, RSSI in dBm, and ping status (`ping ok` / `ping failed`). RSSI and ping are omitted when not available.

**Backends section** — one row per configured backend showing type, enabled state, last upload result, and last upload time.

**Sensors section** — one row per configured sensor showing sensor type, runtime state, transport summary, and the latest reading values.

---

## Page: Diagnostics (`/diagnostics`)

Read-only troubleshooting page with runtime internals that are useful when the device appears unstable or memory-constrained.

The page currently shows:

- **Stats bar**: total available 8-bit heap, current free heap, minimum free heap seen since boot, and largest free block
- **Memory**: free/minimum/largest block for both 8-bit heap and internal heap
- **Tasks**: FreeRTOS stack high watermark for the sensor task, upload task, and cellular task
- **Network Recovery**: current Wi-Fi mode / last Wi-Fi error and cellular reconnect counters
- **Raw Status JSON**: a pretty-printed, read-only console-style dump with build, health, sensor, backend, and diagnostics fields
- **Copy JSON** button: copies the formatted JSON dump to the clipboard, with a manual-selection fallback if the browser clipboard API is unavailable

This page is intended for diagnostics and capacity checks, not for normal day-to-day operation.

---

## Page: Device Configuration (`/config`)

Form for network credentials, device identity, static IP, and cellular modem settings. Accessible in all network modes (including setup AP). Field constraints and validation rules are in [configuration-reference.md](configuration-reference.md#device-configuration-device_cfg). The network mode logic that determines when setup AP is active is in [network-manager.md](network-manager.md).

**Network and identity fields:**

| Field | Input | Notes |
|-------|-------|-------|
| Device name | `<input maxlength=31>` | Stored as `device_name` in `DeviceConfig` |
| Wi-Fi SSID | `<input maxlength=32>` | Empty = setup AP mode on next boot |
| Wi-Fi password | `<input type=password maxlength=63>` | Show/Hide toggle button; disabled when SSID is empty |
| SNTP server | `<input maxlength=63>` + Check SNTP button | Empty = use firmware default (`pool.ntp.org`) |

In `kSetupAp` mode, the SSID field also shows a **network selector** dropdown populated from a `GET /wifi-scan` request fired on page load. Selecting a network fills the SSID text input. The dropdown is hidden in station mode.

The **Check SNTP** button fires `POST /check-sntp` with the current input value and displays the result inline below the field. It does not submit the form.

**Static IP fields** (collapsed unless `Use static IP` is checked):

| Field | Input | Notes |
|-------|-------|-------|
| Use static IP | `<input type=checkbox>` | Enables the fieldset; toggles disabled state via JS |
| IP address | `<input maxlength=15>` | `sta_ip` in `DeviceConfig`; placeholder `192.168.1.100` |
| Subnet mask | `<input maxlength=15>` | `sta_netmask`; placeholder `255.255.255.0` |
| Gateway | `<input maxlength=15>` | `sta_gateway`; placeholder `192.168.1.1` |
| DNS server | `<input maxlength=15>` | `sta_dns`; placeholder `8.8.8.8`; empty = use gateway |

When `sta_ip` is not yet stored and the device is currently connected via DHCP, the IP, netmask, and gateway fields are **pre-filled from the current DHCP lease** (`esp_netif_get_ip_info` on `WIFI_STA_DEF`) to make it easier to convert an existing lease to a static assignment. DNS is pre-filled from the primary DNS server if available.

**Mobile Uplink fields** (collapsed unless `Enable cellular uplink` is checked):

| Field | Input | Notes |
|-------|-------|-------|
| Enable cellular | `<input type=checkbox>` | Stored as `cellular_enabled` in `CellularConfig`; enables the fieldset |
| APN | `<input maxlength=63>` | Required; pre-filled with `internet` when empty |
| Username | `<input maxlength=31>` | Optional; leave empty if carrier does not require it |
| Password | `<input type=password maxlength=63>` | Optional; Show/Hide toggle |
| SIM PIN | `<input type=password maxlength=7>` | Optional; leave empty if SIM has no PIN lock |
| Connectivity check host | `<input maxlength=63>` | IPv4 address to ping after PPP connects; pre-filled with `8.8.8.8` when empty; leave empty to skip |
| Wi-Fi debug window | `<input type=number min=0 max=3600>` | Seconds Wi-Fi station stays active alongside cellular after boot; `0` = disabled |

**Submit action:** `POST /config`
- Validates field lengths, password constraints, and SNTP server characters server-side.
- Saves `DeviceConfig` via `ConfigRepository::save()` and `CellularConfig` via `CellularConfigRepository::save()`.
- Responds with "Configuration saved. Device is rebooting now." and calls `esp_restart()`.
- On validation failure, re-renders the form with the submitted values preserved and an error notice.

---

## Page: Sensor Configuration (`/sensors`)

Sensor edits use a **two-phase staged commit** pattern. Field constraints, per-sensor address rules, and poll interval ranges are in [configuration-reference.md](configuration-reference.md#sensor-configuration-sensor_cfg).

1. Each form action (`add`, `update`, `delete`) modifies an in-memory **staged config** (`staged_sensor_config_`) and sets `has_pending_sensor_changes_ = true`. Nothing is written to NVS yet.
2. "Apply now" (`action=apply`) writes the staged config to NVS and calls `SensorManager::applyConfig()` — changes take effect immediately without a reboot.
3. "Discard pending changes" (`action=discard`) resets the staged config to the last saved config.

**Staging banner** — shown at the top of the page whenever `has_pending_sensor_changes_` is true. Displays current pending status and the Apply / Discard buttons.

**Sensor categories** — sensors are grouped into five fixed categories:

| Category | Sensors | Multiple allowed |
|----------|---------|-----------------|
| Climate | BME280, BME680, DHT11, DHT22, DS18B20, HTU2X, SHT4X | No |
| Light | VEML7700 | No |
| Particulate Matter | SPS30 | No |
| Location | GPS (NMEA) | No |
| Gas / CO2 | SCD30, ME3-NO2 | Yes |

For single-sensor categories, the "Add sensor" form is hidden if the category already has one configured sensor. The Gas category allows multiple sensors simultaneously.

**Per-sensor card** — each configured sensor shows:
- Runtime state pill (`kInitialized`, `kPolling`, `kAbsent`, `kError`)
- Transport summary (e.g., `I2C bus 0 @ 0x76`, `GPIO 4`)
- Poll interval and queued sample count
- Latest reading values
- Edit form (model selector, poll interval, I2C address or GPIO pin, enabled checkbox)
- "Stage sensor deletion" button

**Model selector behaviour** — when the sensor type is changed within a form, JavaScript updates the visible I2C address field or GPIO pin selector to match the new sensor's transport type and injects the default I2C address.

**POST `/sensors` actions:**

| `action` value | Effect |
|---------------|--------|
| `add` | Creates a new `SensorRecord`, assigns next ID, stages it |
| `update` | Updates the existing record with matching `sensor_id`, stages it |
| `delete` | Removes the record by `sensor_id`, stages the deletion |
| `apply` | Writes staged config to NVS, calls `applyConfig()` |
| `discard` | Resets staged config to last saved config |

Category uniqueness is enforced at stage time — staging a second sensor in a single-sensor category returns an error.

---

## Page: Backend Configuration (`/backends`)

A single form containing upload settings and one card per backend type.

**Upload settings panel** — `upload_interval_ms` numeric input (range 10 000–300 000 ms). Validated server-side.

**Backend cards** — one card per registered backend (`Sensor.Community`, `Air360 API`):
- Enabled checkbox — toggling it dims the card via JavaScript.
- Sensor.Community only: `device_id_override` field (overrides the short chip ID sent in `X-Sensor`).
- Upload status summary (last result, last upload timestamp).

**Submit action:** `POST /backends`
- Validates upload interval.
- Reads enabled state from checkboxes (unchecked = absent from form body = `enabled = 0`).
- Calls `applyBackendStaticDefaults()` to rewrite `endpoint_url` from the compiled-in default (endpoint URL is not user-editable via the web UI).
- Saves to NVS and calls `UploadManager::applyConfig()` — takes effect immediately without a reboot.

---

## Endpoint: `/wifi-scan`

`GET /wifi-scan` returns JSON with the last Wi-Fi scan result:

```json
{
  "networks": [
    { "ssid": "MyNetwork", "rssi": -62 },
    { "ssid": "OtherNet",  "rssi": -78 }
  ],
  "last_scan_uptime_ms": 12345,
  "last_scan_error": ""
}
```

If in `kSetupAp` mode and no scan has been done yet (`last_scan_uptime_ms == 0`), a new scan is triggered synchronously on this request. Otherwise the cached result from the last scan is returned. Duplicate SSIDs and hidden networks are already filtered by `NetworkManager::scanAvailableNetworks()`.

---

## Endpoint: `/check-sntp`

`POST /check-sntp` performs a runtime reachability check for a candidate NTP server. The request body is form-encoded: `server=<hostname>`. The response is JSON.

**Success:**
```json
{ "success": true }
```

**Failure:**
```json
{ "success": false, "error": "<reason>" }
```

| `error` value | Meaning |
|---------------|---------|
| `invalid_input` | Server string is empty, too long, or contains invalid characters |
| `not_connected` | Device is not connected to station Wi-Fi |
| `sync_failed` | SNTP init failed or server did not respond within the timeout |

The check deinitialises the existing SNTP session (if any) and initialises a new one with the test server. If the check succeeds, SNTP stays running with the test server until the next reboot. If it fails, SNTP is deinitialised and the maintenance loop will retry with the configured server.

This endpoint does not modify stored configuration. Use `POST /config` followed by a reboot to persist the server.

---

## Static assets (`/assets/*`)

CSS (`air360.css`) and JavaScript (`air360.js`) are served from `/assets/air360.css` and `/assets/air360.js`. Both are compiled into the firmware binary as C arrays via `web_assets.hpp`. Requests to unrecognised asset paths return HTTP 404.

---

## JavaScript behaviour

`air360.js` runs one `DOMContentLoaded` listener that sets up:

| Feature | Mechanism |
|---------|-----------|
| **Dirty tracking** | Forms with `data-dirty-track` mark their parent panel with `panel--dirty` when any field changes. A `beforeunload` guard warns if unsaved changes exist when leaving the page. |
| **Sensor form sync** | When the sensor type `<select>` changes, the I2C address field or GPIO pin selector is shown/hidden and the default I2C address is injected. |
| **Config form sync** | When the Wi-Fi SSID field is cleared, the password field is disabled and the hint text updates. |
| **Wi-Fi network selector** | On the config page in setup AP mode, `loadWifiNetworks()` calls `GET /wifi-scan` asynchronously and populates the SSID dropdown. Selecting an option fills the SSID text input. |
| **Check SNTP** | On the config page, `checkSntp()` fires `POST /check-sntp` with the current SNTP server input value and displays the result in an inline status paragraph. |
| **Backend card sync** | The enabled checkbox toggles the `panel--inactive` CSS class on the backend card panel. |
| **Confirm dialogs** | Forms with `data-confirm` show a `window.confirm()` dialog before submitting (used for Apply, Discard, Delete, and Save-and-reboot). |
| **Show/Hide password** | Buttons with `data-secret-toggle` toggle `input.type` between `"password"` and `"text"`. |

No external libraries are used. The script is plain ES2020 and runs synchronously with the page load.

---

## Template rendering

HTML pages are assembled server-side using a simple `{{PLACEHOLDER}}` substitution engine (`renderTemplate` / `renderPageDocument`). Templates live in `firmware/main/webui/` and are compiled into the binary as C string literals. All user-supplied values passed into templates are HTML-escaped before substitution to prevent injection.

The page shell (navigation, `<head>`, layout wrapper) is generated by `renderPageDocument()`, which takes an active page key to highlight the correct navigation link.
