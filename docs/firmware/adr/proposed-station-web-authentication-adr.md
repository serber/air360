# Station Web Authentication ADR

## Status

Proposed.

## Decision Summary

Add optional local web UI authentication for station mode only. The first implementation should use HTTP Basic authentication checked on every protected request, not a token/session flow.

The Device configuration page will gain a "Local web authorization" section where the user can enable or disable authorization, enter a username, and set a password. When enabled and the firmware is in `NetworkMode::kStation`, all web UI pages and HTTP endpoints require valid credentials. When the firmware is in setup AP mode, authorization is not enforced so provisioning and recovery remain possible.

## Context

The current firmware already has `DeviceConfig::local_auth_enabled`, but the field is reserved and not enforced by the web server. The embedded HTTP server is local to the device, uses server-side rendered pages, and exposes both read-only diagnostics and mutating configuration routes.

Current relevant routes:

| Method | Path | Purpose |
|--------|------|---------|
| `GET` | `/` | Overview page |
| `GET` | `/diagnostics` | Diagnostics page and raw status JSON |
| `GET` | `/logs/data` | Runtime log buffer |
| `GET` | `/assets/*` | Embedded CSS/JS assets |
| `GET` / `POST` | `/wifi-scan` | Wi-Fi scan data and scan trigger |
| `GET` / `POST` | `/config` | Device and cellular configuration |
| `POST` | `/check-sntp` | SNTP reachability check |
| `GET` / `POST` | `/sensors` | Sensor configuration |
| `GET` / `POST` | `/backends` | Backend configuration |

The web UI is also the setup AP provisioning interface. If authorization were enforced in setup AP mode, a forgotten password could block the user from reconfiguring Wi-Fi. The requirement is therefore explicitly station-only authorization.

The firmware currently stores sensitive operational secrets such as Wi-Fi, cellular, and backend passwords in NVS. Authentication credentials should still avoid plaintext storage where practical because the web server only needs to verify a submitted password, not display it.

## Goals

- Allow the user to enable or disable local web UI authorization from the Device page.
- Let the user configure a username and password from the same Device form.
- Enforce authorization only while the network state is `NetworkMode::kStation`.
- Protect both read-only and mutating endpoints in station mode.
- Keep setup AP provisioning unauthenticated.
- Avoid adding a token/session subsystem in the first version.
- Store password verification material rather than the plaintext password.

## Non-Goals

- HTTPS/TLS for the embedded web server.
- Internet-facing security hardening.
- User roles or multiple accounts.
- Login/logout pages in the first version.
- CSRF protection in the first version.
- Protecting setup AP mode.
- Remote backend account authentication.

## Architectural Decision

### 1. Authentication mechanism: HTTP Basic

Use HTTP Basic authentication for the first version.

When station-mode authorization is active and a request has no valid `Authorization: Basic ...` header, the server returns:

```http
HTTP/1.1 401 Unauthorized
WWW-Authenticate: Basic realm="Air360"
Cache-Control: no-store
```

Browsers then show their native credential prompt and resend requests with the `Authorization` header. Firmware validates the header for every protected request. There is no login endpoint, bearer token, cookie, or in-RAM session table.

This means "token validation on every endpoint" becomes "Basic credential validation on every endpoint." It is simpler and equivalent for the current local HTTP threat model because both Basic credentials and bearer tokens would travel over the same unencrypted HTTP channel unless HTTPS is added later.

### 2. Station-only enforcement

Authorization is enforced only when all of these are true:

1. `DeviceConfig::local_auth_enabled != 0`
2. the loaded auth credentials are valid and have a password set
3. `NetworkManager` / `StatusService` reports `NetworkMode::kStation`

In `NetworkMode::kSetupAp`, all routes continue to behave as they do today. This preserves first-time provisioning and field recovery.

If the device is in cellular-primary mode and Wi-Fi station is temporarily active for the debug window, authorization still applies while the network state is `kStation`.

### 3. Protected surface

Protect every registered route in station mode:

| Route family | Protected in station mode |
|--------------|---------------------------|
| HTML pages: `/`, `/diagnostics`, `/config`, `/sensors`, `/backends` | Yes |
| JSON/text endpoints: `/logs/data`, `/wifi-scan`, `/check-sntp` | Yes |
| Assets: `/assets/*` | Yes |
| Captive portal catch-all | No effect in station mode; setup AP remains unauthenticated |

Assets can technically be public, but protecting them keeps the rule simple and avoids route drift as assets grow.

### 4. Credential storage

Add a dedicated `auth_cfg` NVS blob instead of expanding `DeviceConfig` for username/password material.

`DeviceConfig::local_auth_enabled` remains the enable/disable flag. A new `AuthConfigRepository` owns credential persistence:

```cpp
struct AuthConfig {
    std::uint32_t magic;
    std::uint16_t schema_version;
    std::uint16_t record_size;
    std::uint8_t password_set;
    std::uint8_t reserved0[3];
    char username[32];
    std::uint8_t password_salt[16];
    std::uint8_t password_hash[32];
    std::uint8_t reserved1[32];
};
```

Password hashing:

- Generate a random 16-byte salt when the password is set or changed.
- Store `SHA-256(salt || password)` in `password_hash`.
- Compare hashes with a constant-time equality helper.
- Never render the stored password or hash back into the HTML form.

This is not a replacement for HTTPS and is not intended to resist a determined offline attack against extracted flash. It does avoid storing the local web password in plaintext and is sufficient for the first local-access control layer.

Use a separate blob so adding credentials does not force a `DeviceConfig` schema bump and does not wipe existing Wi-Fi/device settings. The NVS documentation must be updated to list `auth_cfg` as a sixth key.

### 5. Device page UI

Add a "Local web authorization" section to the Device configuration page:

| Field | Input | Notes |
|-------|-------|-------|
| Enable authorization | checkbox | Stored as `DeviceConfig::local_auth_enabled`; applies only in station mode |
| Username | text input, max 31 | Required when authorization is enabled |
| New password | password input, max 63 | Required when enabling authorization for the first time; optional on later edits |
| Confirm password | password input, max 63 | Must match `New password` when provided |

Form behavior:

- If authorization is disabled, username/password fields are disabled by JavaScript and ignored by the server.
- If authorization is enabled and no password is already set, a non-empty password is required.
- If authorization is enabled and a password already exists, leaving `New password` empty keeps the existing password hash.
- If `New password` is non-empty, it must pass length validation and match `Confirm password`.
- After a successful save, the page follows the existing Device page behavior and schedules a reboot.

The UI may show "Password already set" as state, but must not pre-fill the password field.

### 6. Request validation integration

Add a small auth helper used at the start of each route handler or through a local wrapper around registered handlers:

```cpp
bool WebServer::requiresAuth() const;
esp_err_t WebServer::enforceAuth(httpd_req_t* request);
```

`requiresAuth()` checks the station-only conditions. `enforceAuth()` parses the Basic header, validates username and password against `AuthConfig`, and sends `401` on failure.

Handlers should call the helper before reading the request body or performing side effects. Mutating POST handlers must not parse or apply form data before authorization succeeds.

### 7. Status and diagnostics

Expose only non-secret auth state:

- `local_auth_enabled`
- `local_auth_config_present`
- `local_auth_password_set`
- no username unless there is a clear operator need
- never expose password hash, salt, submitted password, or Authorization header contents

Logs may report authentication failures at a coarse level, but must not include supplied credentials or raw headers.

## Affected Files

- `firmware/main/include/air360/config_repository.hpp` — keep `local_auth_enabled` as the enable flag.
- `firmware/main/src/config_repository.cpp` — validate `local_auth_enabled` as a boolean if not already enforced.
- `firmware/main/include/air360/auth_config_repository.hpp` — new credential blob type and repository interface.
- `firmware/main/src/auth_config_repository.cpp` — new NVS load/save and password hash helpers.
- `firmware/main/src/config_transaction.cpp` and `firmware/main/include/air360/config_transaction.hpp` — extend combined Device page save to include `auth_cfg` when credentials change.
- `firmware/main/include/air360/web_server.hpp` — store loaded `AuthConfig` / repository references and expose auth helper methods.
- `firmware/main/src/web_server.cpp` — register/use auth enforcement for all routes.
- `firmware/main/src/web/web_mutating_routes.cpp` — parse and validate auth fields in `POST /config`.
- `firmware/main/src/web_ui.cpp` and `firmware/main/webui/page_config.html` — render the Device page auth section.
- `firmware/main/src/status_service.cpp` — expose non-secret auth state only.
- `firmware/main/src/app.cpp` — load/create `AuthConfig` during boot and pass it into `WebServer::start()`.
- `docs/firmware/nvs.md` — document `auth_cfg`.
- `docs/firmware/configuration-reference.md` — document auth fields and validation.
- `docs/firmware/web-ui.md` — document the Device page auth UI and station-only route protection.
- `docs/firmware/startup-pipeline.md` — mention auth config load if boot steps change.

## Alternatives Considered

### Option A. No authorization

Rejected. Anyone on the same station network can read diagnostics and change device, sensor, and backend configuration.

### Option B. Login endpoint plus bearer token

Rejected for the first version. A token flow would require a login form, token generation, token lifetime, storage, logout behavior, cookie or header transport, and CSRF decisions for mutating form posts.

On local HTTP without TLS, a bearer token is still visible on the network in the same way as a Basic header. It does not materially improve the first-version threat model but adds more firmware state and more edge cases.

### Option C. HTTP Basic on every station-mode request (accepted)

Accepted. It matches the simple server-side UI, has no session lifecycle, works with browser-native prompts, and keeps enforcement local to the HTTP request path.

### Option D. Store auth username/password directly in `DeviceConfig`

Rejected for the first version because the existing `DeviceConfig` schema is already deployed. Adding credential fields there would require a schema bump and would reset stored device configuration unless a migration is added. A dedicated `auth_cfg` blob avoids that disruption.

### Option E. Store plaintext password in NVS

Rejected. Other operational secrets are currently plaintext in NVS, but web UI authentication only needs password verification. A salted hash is a small implementation cost and avoids persisting the local web password directly.

## Practical Conclusion

Implement station-mode local authorization with HTTP Basic. Keep `DeviceConfig::local_auth_enabled` as the UI-controlled enable flag, add a dedicated `auth_cfg` NVS blob for username and salted password hash, and require valid credentials at the start of every route handler when the device is in `NetworkMode::kStation`.

Do not introduce login tokens in the first implementation. Tokens become worth revisiting only together with a broader browser-security design, such as HTTPS, session cookies, logout, CSRF protection, and possibly user roles.
