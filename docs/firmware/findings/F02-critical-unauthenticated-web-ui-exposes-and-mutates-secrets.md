# Finding F02: Unauthenticated web UI exposes and mutates secrets

## Status

Confirmed audit finding. Not implemented.

## Scope

Task file for adding local web authentication and stopping secret echo in firmware UI.

## Source of truth in code

- `firmware/main/src/web_server.cpp`
- `firmware/main/src/web/web_mutating_routes.cpp`
- `firmware/main/src/config_repository.cpp`

## Read next

- `docs/firmware/web-ui.md`
- `docs/firmware/nvs.md`
- `docs/firmware/configuration-reference.md`

**Priority:** Critical
**Category:** Security
**Files / symbols:** `WebServer::start`, `WebServer::handleConfig`, `WebServer::handleSensors`, `WebServer::handleBackends`, `ConfigRepository::makeDefaultDeviceConfig`

## Problem

The HTTP server registers configuration, sensor, backend, diagnostics, logs, Wi-Fi scan, and SNTP mutation routes without authentication. The config and backend pages also render stored secrets back into HTML form fields.

## Why it matters

Anyone connected to the setup AP or the same station network can read Wi-Fi credentials, cellular PAP credentials, SIM PIN, and backend Basic Auth passwords. They can also change Wi-Fi settings, backend upload endpoints, sensor configuration, and reboot the device. This is a direct device takeover and credential disclosure risk.

## Evidence

- `firmware/main/src/web_server.cpp:1831` through `1902` registers `GET` and `POST` routes for `/config`, `/sensors`, `/backends`, `/wifi-scan`, and `/check-sntp` with only `user_ctx`; no auth wrapper is applied.
- `firmware/main/src/config_repository.cpp:130` sets `local_auth_enabled = 0U`.
- `docs/firmware/nvs.md` and `docs/firmware/configuration-reference.md` state that `local_auth_enabled` is stored but not enforced.
- `firmware/main/src/web_server.cpp:738` assigns `model.wifi_password_value = config.wifi_sta_password`.
- `firmware/main/src/web_server.cpp:818` and `821` render cellular password and SIM PIN values.
- `firmware/main/src/web_server.cpp:1063` assigns `card.password = boundedCString(record->auth.basic_password, ...)`.
- `firmware/main/src/web/web_mutating_routes.cpp:694`, `724`, and `758` persist config, persist cellular config, and schedule reboot from unauthenticated `POST /config`.

## Recommended Fix

Add a minimal local-auth layer for all non-asset routes, and stop rendering secret values back to clients. Keep setup recovery practical, but do not expose stored credentials.

## Where To Change

- `firmware/main/include/air360/config_repository.hpp`
- `firmware/main/src/config_repository.cpp`
- `firmware/main/include/air360/web_server.hpp`
- `firmware/main/src/web_server.cpp`
- `firmware/main/src/web/web_mutating_routes.cpp`
- `firmware/main/webui/page_config.html`
- `firmware/main/webui/page_backends.html`
- `docs/firmware/web-ui.md`
- `docs/firmware/configuration-reference.md`
- `docs/firmware/nvs.md`

## How To Change

1. Add an admin password or setup token field to `DeviceConfig`, or derive an initial one from a per-device value printed on the device label.
2. Require authentication for:
   - `/config`
   - `/sensors`
   - `/backends`
   - `/diagnostics`
   - `/logs/data`
   - `POST /wifi-scan`
   - `POST /check-sntp`
3. Leave `/assets/*` public.
4. Consider allowing first-boot setup without credentials only until an admin password is set.
5. Render secret inputs empty with placeholders like "leave blank to keep existing".
6. On POST, preserve the existing secret when the submitted field is empty and the user did not explicitly request clearing it.
7. Add CSRF protection or at least require an auth token in mutating forms.

## Example Fix

```cpp
bool WebServer::authorize(httpd_req_t* request) const {
    if (config_ == nullptr || config_->local_auth_enabled == 0U) {
        return firstBootSetupAllowed();
    }
    // Validate Authorization or session cookie here.
}
```

For password fields:

```cpp
const std::string submitted = findFormValue(fields, "wifi_password");
if (!submitted.empty() || formHasKey(fields, "wifi_password_clear")) {
    copyString(updated.wifi_sta_password, sizeof(updated.wifi_sta_password), submitted);
}
```

## Validation

- Unauthenticated GET `/config`, `/backends`, `/diagnostics`, `/logs/data` returns 401 or redirects to login.
- Authenticated forms no longer include saved Wi-Fi password, cellular password, SIM PIN, or backend password in HTML source.
- Empty password fields preserve existing secrets.
- Explicit clear controls clear secrets.
- Run host tests for form parsing and new auth helpers.
- Run `source ~/.espressif/v6.0/esp-idf/export.sh && idf.py build` from `firmware/`.
- Run `python3 scripts/check_firmware_docs.py`.

## Risk Of Change

High. This changes setup and recovery workflows; it needs hardware and browser validation.

## Dependencies

F09 is related because credentials are also stored plaintext in NVS.

## Suggested Agent Type

Security agent / ESP-IDF agent
