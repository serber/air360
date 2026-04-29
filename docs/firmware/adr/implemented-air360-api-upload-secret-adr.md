# Air360 API Upload Secret ADR

## Status

Accepted. Implemented in `firmware/`.

## Decision Summary

When the Air360 API backend is enabled, firmware generates or accepts a
user-provided `upload_secret`, stores it locally, registers only the secret hash
with the backend, and sends the secret as a bearer token on Air360 API ingest
requests.

This is the firmware-side companion to
[`../../backend/adr/implemented-firmware-generated-upload-secret-adr.md`](../../backend/adr/implemented-firmware-generated-upload-secret-adr.md).

## Context

The Air360 API adapter currently uses internal `device_id` in backend write
URLs:

```http
PUT /v1/devices/{device_id}/register
PUT /v1/devices/{device_id}/batches/{batch_id}
```

`device_id` is not intended to be public. Public backend reads use `public_id`.
Still, write requests should require a device-held secret so that a leaked
`device_id` alone is not enough to write measurements.

The previous HMAC proposal protected uploads with canonical request signing,
nonces, and pairing recovery. That is stronger but too complex for the current
stage. A bearer upload secret over HTTPS is the smaller firmware change and is
enough for beta hardening.

## Goals

- Generate a high-entropy upload secret on the device.
- Let the user enter an existing secret after erase-flash or NVS loss.
- Register only an upload secret hash with the backend.
- Send the upload secret only in `Authorization: Bearer ...` on write requests.
- Avoid exposing the secret in status JSON, logs, diagnostics, BLE, or public
  APIs.
- Move Air360 API registration coordinates into a nested `location` object.

## Non-Goals

- Implement HMAC request signing in the first version.
- Implement a pairing-code recovery flow.
- Guarantee recovery if the user loses the upload secret.
- Protect the secret against physical flash extraction.
- Add user accounts or a portal dependency for device writes.

## Architectural Decision

### 1. Secret Lifecycle

Firmware creates `upload_secret` only when the user enables the Air360 API
backend and no existing secret was supplied.

Recommended generated format:

```text
air360_us_v1_<base64url-encoded-32-random-bytes>
```

The Backends page provides two setup paths:

- Generate new upload secret.
- I already have upload secret.

When generating a new secret, the UI shows it to the local user with clear text
that it must be saved for future device reset recovery. The firmware may allow
the secret to be revealed again from local storage, but it must never expose it
through remote status endpoints or logs.

### 2. Local Storage

Store the secret in NVS as credential data associated with the Air360 API
backend. It should be separate from ordinary editable backend fields where
practical.

Stored credential fields:

| Field | Purpose |
|-------|---------|
| `upload_secret` | Bearer secret used for Air360 API writes |
| `secret_version` | Format/version marker, initially `1` |
| `provisioned` | Whether a secret has been configured locally |

The editable backend config continues to own host, path, port, TLS, enabled
state, and location fields.

### 3. Registration Request

Before first upload after boot, firmware registers the device:

```http
PUT /v1/devices/{device_id}/register
Content-Type: application/json
User-Agent: air360/{firmware_version}
```

```json
{
  "schema_version": 1,
  "name": "Air360-AB12",
  "firmware_version": "0.1.0",
  "location": {
    "latitude": 55.751244,
    "longitude": 37.618423
  },
  "upload_secret_hash": "sha256:base64url-sha256-value"
}
```

Hash input:

```text
<upload_secret>
```

The secret format (`air360_us_v1_...`) already carries type and version context,
so no additional prefix is needed. The firmware sends the hash, not the raw
secret, in the registration body.

Registration behavior:

- If Air360 API is enabled and no upload secret exists, generate one before
  registration.
- If the user supplied an existing secret, validate its prefix/shape locally and
  store it before registration.
- If latitude and longitude are both `0.0`, keep the current configuration error
  behavior and do not register.
- HTTP 2xx marks the backend registered for the current boot.
- `401 invalid_upload_secret` means the entered secret does not match the
  existing backend device record; do not upload until the user fixes it.

### 4. Ingest Request

Every Air360 API batch upload includes:

```http
Authorization: Bearer air360_us_v1_<secret>
```

The secret is not included in the JSON body and is not added to query strings.

The existing batch payload shape can remain unchanged.

### 5. User-Facing Recovery

After `erase-flash` or NVS loss:

1. User configures Wi-Fi and opens the local Backends page.
2. User enables Air360 API.
3. User chooses "I already have upload secret".
4. User enters the saved secret.
5. Firmware stores the secret and registers with its hash.
6. Backend accepts the device if the hash matches.

If the user lost the secret, firmware cannot recover the previous backend device
record by itself. The UI should explain that a backend-side reset or a new
device record is required.

### 6. Firmware State

Air360 API credential states:

| State | Meaning | Upload behavior |
|-------|---------|-----------------|
| `missing_secret` | Air360 API enabled but no secret exists | Generate or require user input before registration |
| `provisioned` | Local secret exists | Register hash, then upload with bearer secret |
| `secret_rejected` | Backend rejected the hash or bearer secret | Stop Air360 API uploads and surface config error |

`UploadManager` queue semantics remain unchanged. Auth failures are
backend-specific failures and should not block other enabled backends.

## Affected Firmware Files

- `firmware/main/include/air360/uploads/backend_config.hpp` - add credential
  state or reference a separate credential record.
- `firmware/main/src/uploads/backend_config_repository.cpp` - persist the
  Air360 API credential state if stored with backend config.
- `firmware/main/src/uploads/adapters/air360_api_uploader.cpp` - generate
  secret, send nested `location`, register hash, and add bearer header.
- `firmware/main/include/air360/uploads/adapters/air360_api_uploader.hpp` -
  track credential/auth state.
- `firmware/main/src/web_server.cpp` and `firmware/main/webui/page_backends.html`
  - add generate-secret and existing-secret UI flow.
- `firmware/main/src/status_service.cpp` - expose non-secret state only.
- `docs/firmware/upload-adapters.md` - update implemented contract after code
  changes.
- `docs/firmware/nvs.md` and `docs/firmware/configuration-reference.md` -
  document credential storage after implementation.

## Alternatives Considered

### HMAC-signed uploads

Stronger against replay but requires request canonicalization, nonce storage,
timestamp tolerance, and more backend/firmware state. Deferred until production
needs justify it.

### Backend-generated upload secret

Works for first registration, but makes erase recovery depend on a backend reset
or pairing flow. Firmware-generated secrets let the user retain the recovery
material.

### No upload secret

Simplest, but a leaked internal `device_id` would be enough to write data.

## Practical Conclusion

Firmware should own the upload secret lifecycle. Generate a user-saveable secret
when Air360 API is enabled, allow users to re-enter it after reset, register only
its hash, and use the secret as a bearer token for backend writes.
