# Firmware-Generated Upload Secret ADR

## Status

Proposed.

## Decision Summary

Protect Air360 API write endpoints with a firmware-generated `upload_secret`.
The device generates the secret when the Air360 API backend is enabled, shows it
to the local user for backup, and registers only a hash of that secret with the
backend. Later ingest requests use `Authorization: Bearer <upload_secret>`.

The backend never exposes `device_id` through public read APIs and does not use
`device_id` as a public identifier. Public read APIs continue to use `public_id`.

The registration contract is updated to carry coordinates in a nested
`location` object instead of flat `latitude` / `longitude` fields.

## Context

The current backend routes identify device write traffic by internal
`device_id`:

```http
PUT /v1/devices/{device_id}/register
PUT /v1/devices/{device_id}/batches/{batch_id}
```

`device_id` is not intended to be public. External reads use the generated
`public_id`.

That reduces the practical risk of measurement spoofing, but `device_id` alone
is still only an identifier, not proof that the sender is the device. A simple
bearer secret is enough for the current product stage and avoids the operational
cost of HMAC canonicalization, nonces, clock tolerance, pairing sessions, and
secret recovery flows.

## Goals

- Require more than `device_id` for measurement writes.
- Keep the device/backend protocol simple.
- Avoid user accounts or a portal dependency for device writes.
- Support device re-registration after firmware erase when the user saved the
  existing secret.
- Keep `public_id` as the only public device identifier.
- Store only a server-side hash of the upload secret.
- Move registration coordinates into `location`.

## Non-Goals

- Protect against physical extraction of the secret from device flash.
- Provide self-service recovery when the user lost the secret.
- Implement HMAC request signatures, nonce replay protection, or clock-based
  request validation in the first version.
- Authenticate Sensor.Community, InfluxDB, or Custom Upload backends.

## Architectural Decision

### 1. Secret Ownership

The firmware generates `upload_secret`. The backend stores only
`upload_secret_hash`.

Recommended secret format shown to the user:

```text
air360_us_v1_<base64url-encoded-32-random-bytes>
```

Backend hash input:

```text
air360-upload-secret-v1:<upload_secret>
```

Backend stored value:

```text
sha256:<base64url(sha256(hash_input))>
```

The secret is high entropy, so a single SHA-256 hash with a fixed context string
is sufficient for lookup and comparison. If the secret format later changes, the
prefix and hash context must change together.

### 2. Registration Contract

The firmware registers or refreshes metadata with:

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

Registration rules:

- `location.latitude` is required and must be between `-90` and `90`.
- `location.longitude` is required and must be between `-180` and `180`.
- `upload_secret_hash` is required.
- A new `device_id` creates the device record and stores the hash.
- An existing `device_id` with the same hash updates metadata and succeeds.
- An existing `device_id` with a different hash fails and does not update
  metadata.
- The backend may continue storing latitude and longitude as separate database
  columns; the nested object is an API contract decision, not necessarily a
  storage-shape decision.

Success response:

```http
200 OK
Content-Type: application/json
Cache-Control: no-store
```

```json
{
  "schema_version": 1,
  "status": "registered",
  "public_id": "550e8400-e29b-41d4-a716-446655440000",
  "registered_at": "2026-04-27T09:15:00.000Z",
  "last_seen_at": "2026-04-27T09:15:00.000Z"
}
```

The response does not need to echo `device_id`; it is already present in the
request path and remains an internal write identifier.

### 3. Ingest Authentication

Every Air360 API ingest request includes the upload secret:

```http
PUT /v1/devices/{device_id}/batches/{batch_id}
Content-Type: application/json
Authorization: Bearer air360_us_v1_<secret>
User-Agent: air360/{firmware_version}
```

Backend verification:

1. Find the device by path `device_id`.
2. Hash the bearer secret with the same context string used at registration.
3. Compare with the stored `upload_secret_hash` using constant-time comparison.
4. Process the batch only after authentication succeeds.

The secret must not be accepted from query parameters or JSON request bodies for
ingest.

### 4. Re-Registration After Erase

If the device is erased and the user saved the secret:

1. The user opens the local device UI.
2. The user enables Air360 API and chooses "I already have upload secret".
3. Firmware stores the entered secret locally.
4. Firmware sends the hash in the registration request.
5. Backend accepts the request if the hash matches the existing device record.

If the user lost the secret, this ADR does not define public recovery. A future
manual reset or account-based recovery flow can be added later.

### 5. Error Contract

| HTTP | `error.code` | Meaning |
|------|--------------|---------|
| 400 | `validation_error` | Missing or invalid registration field |
| 401 | `invalid_upload_secret` | Bearer secret or registration hash does not match the device |
| 404 | `device_not_found` | Ingest attempted before registration |
| 429 | `rate_limited` | Too many attempts |
| 500 | `internal_error` | Unexpected backend failure |

Error body:

```json
{
  "error": {
    "code": "invalid_upload_secret",
    "message": "Upload secret does not match this device"
  }
}
```

Existing duplicate-batch idempotency can remain a success response and does not
need to change for upload-secret authentication.

### 6. Security Notes

- `device_id` remains internal and must not be exposed in public read APIs.
- `public_id` remains the public device identifier.
- All write endpoints require HTTPS in production.
- Backend logs must avoid recording bearer tokens.
- Registration and ingest should be rate-limited by source IP and `device_id`.
- The first registration remains TOFU: if an attacker learns a `device_id`
  before the real device registers, they can claim it with their own hash. This
  is accepted for the current stage because `device_id` is not public.

## Alternatives Considered

### Backend-generated secret

The backend could generate and return `upload_secret` during registration. This
works, but it makes erase recovery harder because the backend must return or
rotate a secret later. Firmware-generated secrets let the user keep the recovery
material outside the backend.

### HMAC-signed requests

HMAC with timestamp and nonce gives replay protection, but it adds clock
tolerance, nonce storage, canonical request rules, and more firmware state. It
is deferred as production hardening.

### No authentication

This is simpler but makes `device_id` the only write gate. That is acceptable
only while `device_id` stays private and the service is in early beta.

## Affected Backend Files

- `backend/src/routes/v1/devices.ts` - accept nested `location` and
  `upload_secret_hash` during registration.
- `backend/src/routes/v1/ingest.ts` - require bearer upload secret for ingest.
- `backend/src/modules/devices/device-repository.ts` - store and compare
  `upload_secret_hash`.
- `backend/src/db/schema.ts` - add `upload_secret_hash`.
- `backend/migrations/` - add `upload_secret_hash` to `devices`.
- `docs/backend/README.md` - update API reference after implementation.

## Practical Conclusion

Use a firmware-generated bearer `upload_secret` for Air360 API writes. Register
only the hash with the backend, store only the hash server-side, use `public_id`
for public reads, and keep `device_id` as an internal write identifier.
