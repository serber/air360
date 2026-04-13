# Cellular Uplink ADR

## Status

Implemented.

## Decision Summary

Air360 should support `SIM7600E` as a cellular IP uplink, operated via `PPP over UART` using the
ESP-IDF `esp_modem` managed component.

When cellular is enabled it becomes the primary uplink for all internet traffic: SNTP, backend
uploads, and any future outbound connections.  Wi-Fi remains available for a configurable
**debug window** after boot (default 10 minutes) and then shuts down automatically.  Setup AP
remains the provisioning and recovery entry point.

A **connectivity check** runs immediately after the PPP session is established.  Its result is
visible in the UI and in `/status` but does not block uploads.

---

## Context

The current firmware assumes Wi-Fi station mode as the only internet path.  That works for
typical home or lab deployments, but it blocks:

- outdoor and remote monitoring points
- locations without existing Wi-Fi infrastructure
- mobile or temporary installations

`SIM7600E` was chosen because it is a widely available LTE module, it supports standard UART
PPP, and it matches the target hardware platform.

The key design goal is **minimal disruption to the existing upload pipeline**: the modem must
look like a generic IP bearer to `UploadManager`, `UploadTransport`, and the backend adapters.

---

## Goals

- allow sensor data to be collected and uploaded from locations without Wi-Fi
- keep `Sensor.Community` and `Air360 API` uploads working without modem-specific code inside
  the backend adapters
- keep the existing queue and retry model intact: measurements accumulate while the modem is
  offline and drain when connectivity returns
- allow local administration through the web UI during a boot-time debug window even when Wi-Fi
  is not the permanent uplink
- expose modem state, signal, and operator information in the web UI and `/status`

---

## Non-Goals

- SMS, voice, or GNSS features of the module
- multiple SIM profiles or carrier switching
- USB modem mode
- replacing Wi-Fi onboarding with a cellular captive-portal flow
- handling every SIM7600 variant in the first milestone

---

## Architectural Decisions

### 1. Explicit uplink mode

Introduce a firmware-level concept of **active uplink**: the network interface currently used for
all outbound traffic.

Two uplink modes:

| Mode | Description |
|------|-------------|
| `wifi` | Wi-Fi station is the uplink. Current default behavior. |
| `cellular` | PPP over `SIM7600E` is the uplink. Wi-Fi also runs during the debug window then stops. |

The uplink mode is derived at runtime from the cellular config flag, not stored explicitly.
If `cellular_enabled` is true the firmware selects cellular uplink.  Otherwise it selects Wi-Fi
uplink (current behavior).

### 2. Wi-Fi debug window

When cellular is the selected uplink, Wi-Fi station is still started alongside the modem.
This allows the operator to reach the local web UI during initial deployment.

Rules:

- if the device has station credentials, it attempts to connect to Wi-Fi station mode
- after the **debug window** expires (configurable, default 600 s), Wi-Fi is stopped
  regardless of connection state
- if the device has no station credentials, no Wi-Fi station attempt is made and no AP is
  started during the debug window (setup AP is only started when there is no usable uplink at
  all)
- the debug window timer starts from boot, not from successful Wi-Fi association; the 10-minute
  window is absolute, not extended by reconnects

Rationale: the window gives a newly deployed device enough time to be reached and configured
while keeping the radio off during long unattended runs.

### 3. PPP over UART via esp_modem

The modem is integrated using the ESP-IDF `espressif/esp_modem` managed component.

The component provides:

- AT-command initialization of `SIM7600E`
- PDP context activation
- PPP mode entry and netif binding
- a `CellularDCE` abstraction for querying modem state (RSSI, operator, registration)

The resulting `ppp_netif` is used directly by lwIP.  All TCP/UDP stack consumers — including
`esp_http_client`, `esp_netif_sntp`, and `esp_ping` — route through it automatically when it is
the default netif.

### 4. Connectivity check

Immediately after the PPP session is established, the firmware performs a connectivity check
to confirm that end-to-end IP routing works.

Implementation:

- use `esp_ping` to send an ICMP echo to the configured `connectivity_check_host`
- timeout: 5 s, 3 attempts
- on success: mark `modem_connectivity_ok = true`
- on failure: mark `modem_connectivity_ok = false`, log a warning; **do not block uploads**

The check host is configurable in `CellularConfig` (default `"8.8.8.8"`).

If `connectivity_check_host` is empty, the check is skipped and `modem_connectivity_ok` is left
unset.

### 5. SNTP over the active uplink

No change to SNTP logic is required.  `esp_netif_sntp` resolves the configured server through
the system DNS, which follows the active netif.

When cellular is the uplink, the PPP netif is set as the default netif after PPP comes up.  SNTP
then runs over cellular transparently.

### 6. Upload pipeline is bearer-agnostic

`UploadManager` currently gates on `station_connected`.  This check must be replaced by a query
against a new `UplinkStatus` concept:

```
bool uplinkReady() const;   // true when any uplink has an IP and time is valid
```

`UplinkStatus` is updated by both `NetworkManager` (Wi-Fi) and the new `CellularManager` (PPP).
Backend adapters are not modified.

### 7. Separate CellularManager component

Modem logic lives in a dedicated `CellularManager` class.  It does not touch Wi-Fi code.

Responsibilities:

- power-on sequence (PWRKEY GPIO if wired)
- AT initialization and SIM check
- PDP context setup
- PPP session management
- connectivity check
- state reporting via `CellularState`
- backoff and reset on repeated failures

`NetworkManager` retains all Wi-Fi and setup-AP logic unchanged.

### 8. CellularConfig in NVS

Stored as a separate NVS blob keyed `"cellular_cfg"` in the existing `"air360"` namespace.
Versioned with its own `magic` and `schema_version`, independent of `DeviceConfig`.

Required fields:

| Field | Type | Notes |
|-------|------|-------|
| `enabled` | `uint8_t` | 0 = disabled |
| `apn[64]` | `char[]` | PDP context APN |
| `username[32]` | `char[]` | optional, empty if not required |
| `password[64]` | `char[]` | optional, empty if not required |
| `sim_pin[8]` | `char[]` | optional, empty if SIM has no PIN |
| `connectivity_check_host[64]` | `char[]` | default `"8.8.8.8"`, empty = skip |
| `wifi_debug_window_s` | `uint16_t` | seconds, default 600 |
| `uart_port` | `uint8_t` | Kconfig default: UART1 |
| `uart_rx_gpio` | `uint8_t` | Kconfig default: GPIO18 |
| `uart_tx_gpio` | `uint8_t` | Kconfig default: GPIO17 |
| `uart_baud` | `uint32_t` | default 115200 |
| `pwrkey_gpio` | `uint8_t` | 0xFF = not wired |
| `sleep_gpio` | `uint8_t` | 0xFF = not wired; drives modem DTR/sleep pin |
| `reset_gpio` | `uint8_t` | 0xFF = not wired |

Not persisted: IMEI, ICCID, operator name, RSSI — these are read from the modem at runtime.

**Hardware note — GPIO17/18 conflict with GPS UART.**
The current firmware maps UART1 (GPS NMEA driver) to RX=GPIO18, TX=GPIO17 at 9600 baud.
The modem uses the same GPIO pair and the same UART port.
These two peripherals are mutually exclusive: a hardware revision that includes the `SIM7600E`
modem must not simultaneously wire a GPS module to GPIO17/18.  On such a board the GPS sensor
type should either be removed from the sensor registry or mapped to a different UART/GPIO pair.
The Kconfig defaults (`CONFIG_AIR360_CELLULAR_DEFAULT_UART = 1`,
`CONFIG_AIR360_CELLULAR_DEFAULT_RX_GPIO = 18`, `CONFIG_AIR360_CELLULAR_DEFAULT_TX_GPIO = 17`)
match the physical wiring of the modem hardware revision.  The GPS defaults remain unchanged
and co-exist safely on hardware that does not include the modem.

### 9. Setup AP is unchanged

Setup AP remains the recovery path when no usable uplink configuration exists.  The boot
sequence triggers it when:

- cellular is disabled AND Wi-Fi station credentials are absent, OR
- cellular is enabled, PPP fails repeatedly, AND the debug window has not expired

### 10. Overview page additions

The Overview page gains a new **Uplink** section showing:

- active uplink: `wifi` / `cellular` / `none`
- Wi-Fi IP (if connected)
- Cellular IP (if PPP active)
- cellular signal quality and operator (if modem registered)
- connectivity check result (ok / failed / skipped)

---

## CellularState

```cpp
struct CellularState {
    bool enabled = false;
    bool modem_detected = false;
    bool sim_ready = false;
    bool registered = false;
    bool ppp_connected = false;
    bool connectivity_ok = false;        // result of last ping check
    bool connectivity_check_skipped = false;
    std::string ip_address;
    std::string operator_name;
    std::string rat;                     // e.g. "LTE"
    int rssi_dbm = 0;
    std::string last_error;
};
```

---

## Uplink Selection at Boot

```
boot
 └─ cellular_enabled?
     ├─ yes
     │   ├─ start CellularManager
     │   │   ├─ PPP OK → set PPP netif as default, connectivity check, RSSI polling starts
     │   │   └─ PPP fails N times → uplink fallback cascade:
     │   │         ├─ try Wi-Fi station
     │   │         │   ├─ OK  → active_bearer = kWifi
     │   │         │   └─ fail → start setup AP
     │   │         └─ (no credentials) → start setup AP
     │   ├─ start Wi-Fi station (debug window timer = wifi_debug_window_s)
     │   └─ on timer expiry → stop Wi-Fi station + close AT command channel
     └─ no
         └─ standard Wi-Fi / setup-AP flow (unchanged)
```

The debug window timer runs in parallel with modem bring-up.  If the modem comes up before
the window closes, both bearers are briefly active; PPP is the default netif.  When the
window expires, Wi-Fi stops and the AT channel closes; the modem remains in pure PPP mode.

---

## Failure Handling

| Failure | Behavior |
|---------|----------|
| Modem not responding | Log error; retry with backoff; expose in `CellularState.last_error` |
| SIM not ready / PIN error | Log error; do not retry PIN automatically; surface in UI |
| PDP context failure | Retry with backoff |
| PPP drop | Attempt reconnect with backoff |
| Connectivity check failure | Log warning; set `connectivity_ok = false`; **do not reconnect** — advisory only |
| PPP reconnect attempts exhausted | Trigger **uplink fallback cascade** (see below) |

**Uplink fallback cascade** (cellular → Wi-Fi → setup AP):

After `CONFIG_AIR360_CELLULAR_MAX_RECONNECT_ATTEMPTS` consecutive PPP failures (default: 4):

1. **Try Wi-Fi station** — if station credentials are configured, attempt a normal station join.
   If Wi-Fi connects, `active_bearer` becomes `kWifi` and uploads resume over Wi-Fi.
   Log: `"Cellular exhausted reconnect attempts, falling back to Wi-Fi"`.

2. **Try setup AP** — if Wi-Fi station join also fails (or no credentials exist), start setup AP
   so the device can be reached and reconfigured.
   Log: `"Wi-Fi also unavailable, starting setup AP for recovery"`.

Measurements continue to queue throughout the cascade.  The cascade runs only once per boot;
if cellular recovers after the fallback, it does not automatically reclaim the uplink role until
the next reboot.

Maximum reconnect attempts and backoff intervals are configurable via Kconfig defaults.

---

## Security Notes

- APN credentials and SIM PIN are stored in NVS using the same access model as Wi-Fi credentials
- credential fields are not echoed back to the UI in plaintext (show/hide toggle, same as
  Wi-Fi password)
- no AT-command passthrough route is exposed in the web server

---

## Alternatives Considered

### Option A — AT-driven HTTP via modem

The modem handles HTTP requests using its own AT command set.

Rejected. Duplicates existing HTTP logic, pushes backend-specific behavior into AT sequences,
makes retry and error handling harder to maintain.

### Option B — PPP over UART via esp_modem (accepted)

The modem is a transparent IP bearer. All application logic stays in firmware.

Accepted. Minimal disruption to the upload pipeline and TLS/SNTP paths.

### Option C — USB NCM/ECM modem mode

Deferred. Higher hardware complexity; UART PPP is the right first milestone for this platform.

### Option D — Wi-Fi preferred, cellular as fallback

Not adopted for this design. The user's deployment model treats cellular as the primary uplink.
Wi-Fi is for local administration, not for traffic routing. Inverting the priority avoids
using metered cellular only to compensate for intermittent Wi-Fi.

---

## Implementation Plan

### Phase 0 — Groundwork (no functional change)

**Goal:** introduce the uplink abstraction so later phases can wire into it cleanly.

1. Add `UplinkStatus` struct to `network_manager.hpp` (or a new `uplink_status.hpp`):
   - `bool uplink_ready` — true when any bearer has an IP and time is valid
   - `UplinkBearer active_bearer` enum: `kNone / kWifi / kCellular`
   - `std::string cellular_ip`
2. Extend `NetworkState` with a `cellular_ip` field for later use.
3. Patch `UploadManager`: replace the `station_connected` gate with a call to
   `uplink_ready()` sourced from the new struct.  No behavioral change yet since cellular
   is always disabled.
4. Add `CellularConfig` struct and `CellularConfigRepository` to NVS (same pattern as
   `ConfigRepository`).  `makeDefaultCellularConfig()` returns disabled config.
5. Load `CellularConfig` in `app.cpp` alongside `DeviceConfig`.  Log it but do nothing
   with it yet.
6. Register `CellularConfigRepository` in `CMakeLists.txt` / `idf_component_register`.

---

### Phase 1 — Modem bring-up

**Goal:** `SIM7600E` gets an IP and PPP netif is active.

1. Add `espressif/esp_modem` to `idf_component.yml`.
2. Create `cellular_manager.hpp` / `cellular_manager.cpp`:
   - `init(const CellularConfig&)` — creates UART DTE and SIM7600 DCE
   - `start()` → power key sequence → AT sync → SIM check → PDP activate → PPP start
   - `state()` → `CellularState`
   - internal FreeRTOS task for reconnect loop
3. UART DTE configured from `CellularConfig.uart_*` fields.
4. `CellularManager` sets `ppp_netif` as the default netif after PPP is established.
5. `CellularState.ip_address` is populated from the PPP netif's IP info.
6. Wrap the PPP IP event to update `UplinkStatus.active_bearer = kCellular` and
   `uplink_ready = true`.

**Kconfig additions:**
- `CONFIG_AIR360_CELLULAR_DEFAULT_UART` = 1
- `CONFIG_AIR360_CELLULAR_DEFAULT_RX_GPIO` = 18
- `CONFIG_AIR360_CELLULAR_DEFAULT_TX_GPIO` = 17
- `CONFIG_AIR360_CELLULAR_DEFAULT_PWRKEY_GPIO` = 0xFF (unset; board-specific)
- `CONFIG_AIR360_CELLULAR_DEFAULT_SLEEP_GPIO` = 0xFF (unset; board-specific)
- `CONFIG_AIR360_CELLULAR_MAX_RECONNECT_ATTEMPTS` = 4
- `CONFIG_AIR360_CELLULAR_RECONNECT_BACKOFF_MAX_S` = 60

**Success criterion:** device boots with `cellular_enabled = true`, gets a PPP IP, logs it.

---

### Phase 2 — Connectivity check

**Goal:** confirm end-to-end routing after PPP comes up.

1. After `ppp_connected`, call `runConnectivityCheck(host, timeout_ms, retries)`.
2. Use `esp_ping` to send ICMP echo.
3. On success: `CellularState.connectivity_ok = true`.
4. On failure: `CellularState.connectivity_ok = false`, log `W` level warning.
5. Result stored in `CellularState`; surfaced in `/status` and Overview.
6. `connectivity_check_host` is taken from `CellularConfig`; empty = skip.

**Success criterion:** after PPP comes up, log shows ping result; check does not block uploads.

---

### Phase 3 — Wi-Fi debug window

**Goal:** Wi-Fi station starts at boot alongside cellular, stops after the debug window.

1. In `app.cpp`, when `cellular_enabled`:
   - call `NetworkManager::connectStation()` normally
   - start a one-shot `esp_timer` with duration `wifi_debug_window_s` seconds
   - on timer fire: call `NetworkManager::stopStation()` (new method)
2. `NetworkManager::stopStation()`: calls `esp_wifi_disconnect()` → `esp_wifi_stop()`;
   updates `NetworkState` accordingly.
3. The Wi-Fi IP is captured in `NetworkState.ip_address` as today.
4. `UplinkStatus.active_bearer` remains `kCellular` regardless of Wi-Fi state.
5. Log events: "Wi-Fi debug window active (600 s)", "Wi-Fi debug window expired, stopping
   station".

**Success criterion:** Wi-Fi is reachable for the first N minutes; after the window closes,
all traffic continues over PPP.

---

### Phase 4 — Upload pipeline and SNTP integration

**Goal:** uploads and time sync use the active uplink regardless of bearer.

1. `UploadManager` already reads `uplink_ready()` from Phase 0.  Wire `CellularManager` to
   update `UplinkStatus` on PPP connect / disconnect.
2. `NetworkManager::synchronizeTime()` currently calls `esp_netif_sntp_init()`.  Because the
   PPP netif is now the default, SNTP resolves through it automatically — **no code change
   needed**.  Verify by running SNTP with cellular active and confirming time sync.
3. If cellular is the uplink and Wi-Fi debug window has not yet expired, SNTP may bind to
   either netif.  Set the default netif priority explicitly: PPP netif takes precedence
   when both are up.

**Success criterion:** a device with no station credentials uploads measurements and has valid
time via cellular only.

---

### Phase 5 — Configuration UI

**Goal:** operator can provision cellular from the Device page.

1. Add `CellularConfig` fields to the POST handler for `/config` (or add a dedicated `/cellular-config`
   endpoint — see trade-offs below).
2. Extend `page_config.html` with a **Mobile Uplink** section:
   - `Enable cellular` toggle
   - `APN` text input
   - `Username` text input (optional)
   - `Password` secret input with show/hide (optional)
   - `SIM PIN` secret input (optional)
   - `Connectivity check host` text input
   - `Wi-Fi debug window` number input (seconds)
   - Advanced / collapsed section: UART port, RX GPIO, TX GPIO, PWRKEY GPIO, RESET GPIO
3. Validate APN as a non-empty string if `enabled = true`.
4. Password and PIN fields follow the existing secret-toggle pattern.
5. `Save and reboot` applies to both `DeviceConfig` and `CellularConfig` in a single
   submit — write both NVS blobs then schedule the restart.

**Trade-off note:** keeping cellular config on the same Device page ensures it is reachable in
setup AP mode (which only exposes `/config`).  A separate endpoint would be cleaner but would
require AP-mode navigation changes.

**Success criterion:** user can enter APN and toggle cellular from the web UI without serial
access.

---

### Phase 6 — Status and diagnostics

**Goal:** modem state is visible in the Overview page and `/status`.

1. Add `CellularState` to `StatusService`.  `StatusService::cellularState()` returns it.
2. Extend `page_root.html` (Overview) with an **Uplink** section:
   - active uplink badge: `Wi-Fi` / `Cellular` / `None`
   - Wi-Fi IP (shown if station connected)
   - Cellular IP (shown if PPP connected)
   - Operator, RAT, RSSI (if registered)
   - Connectivity check result icon
3. Extend `/status` JSON with a `"cellular"` key containing `CellularState` fields.
4. `CellularManager` polls RSSI and operator name periodically (every 30 s) using the
   `esp_modem` AT command channel while the Wi-Fi debug window is active.  The AT channel
   is kept open alongside PPP only for this window.  When the debug window expires and
   Wi-Fi is stopped, the AT channel is also closed; the modem stays in pure PPP data mode
   and RSSI polling stops.  The last known values remain visible in the UI until the next
   PPP reconnect.

**Success criterion:** Overview page shows `Cellular` as active uplink with signal and operator.

---

### Phase 7 — Robustness and field hardening

1. **Reconnect count and backoff**: `CONFIG_AIR360_CELLULAR_MAX_RECONNECT_ATTEMPTS` = 4
   (default).  Exponential backoff between attempts, capped at
   `CONFIG_AIR360_CELLULAR_RECONNECT_BACKOFF_MAX_S`.  On exhaustion, trigger the uplink
   fallback cascade (cellular → Wi-Fi station → setup AP).
2. **Modem hardware reset**: before the final reconnect attempt, pulse `reset_gpio` if wired,
   then pulse `pwrkey_gpio` to force a clean power cycle.
3. **Sleep pin**: after a successful PPP session ends cleanly (e.g. intentional shutdown), drive
   `sleep_gpio` to put the modem in low-power sleep rather than cutting power abruptly.
4. **Watchdog**: `CellularManager` task must call `esp_task_wdt_reset()` during long AT waits
   (same pattern as `NetworkManager`).
5. **AT channel lifecycle**: open at modem init; kept open for RSSI polling while the debug
   window is active; closed when the debug window expires.  Re-opened on PPP reconnect only
   if the debug window has not yet expired.
6. **Log verbosity**: `ESP_LOGD` for AT transcript; `ESP_LOGI` for state transitions;
   `ESP_LOGW` for recoverable errors; `ESP_LOGE` for cascade trigger.

---

## Acceptance Criteria

- [ ] device boots with `cellular_enabled = true`, establishes PPP, gets a cellular IP
- [ ] SNTP synchronizes time over PPP
- [ ] `Sensor.Community` and `Air360 API` uploads succeed over cellular
- [ ] Wi-Fi station is available during the debug window and stops when it expires
- [ ] connectivity check result is shown in the UI
- [ ] operator and signal quality are shown in the Overview
- [ ] APN and other cellular settings are configurable from the Device page in setup AP mode
- [ ] a device with no Wi-Fi credentials still uploads via cellular
- [ ] queued measurements drain after a modem reconnect
- [ ] setup AP starts if no uplink is available and the device needs recovery

---

## Resolved Design Questions

| Question | Decision |
|----------|----------|
| GPIO defaults for first hardware revision | UART1, RX=GPIO18, TX=GPIO17; PWRKEY and SLEEP GPIO numbers are board-specific and default to `0xFF` (not wired) until the first hardware revision is finalized |
| AT command channel alongside PPP | Keep AT channel open during the debug window (first 600 s) for RSSI and operator polling; close it when the debug window expires; modem stays in pure PPP data mode thereafter |
| Debug window duration in UI vs Kconfig | Exposed as a configurable field in `CellularConfig` and editable in the Device page UI; Kconfig default is 600 s |
| Connectivity check on failure — reconnect or advisory? | Advisory only; a failed ping does not trigger a PPP reconnect; reconnects are driven by PPP drop events only |
| Reconnect attempts before fallback | 4 attempts (Kconfig default), then uplink fallback cascade: cellular → Wi-Fi station → setup AP |
