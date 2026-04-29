# Backend ADRs

This directory contains backend architecture decision records for the native
Air360 API.

`/backend` remains the source of truth for implemented behavior. ADRs in this
directory describe accepted or proposed backend decisions that may require
follow-up implementation work.

## Implemented

- [implemented-firmware-generated-upload-secret-adr.md](implemented-firmware-generated-upload-secret-adr.md)
  Firmware-generated upload secrets for Air360 API device writes.

## Proposed

- [portal-api-extensions-adr.md](portal-api-extensions-adr.md)
  New read endpoints (`GET /v1/devices`, `GET /v1/devices/:public_id/measurements`) to support the portal map and device detail pages.
