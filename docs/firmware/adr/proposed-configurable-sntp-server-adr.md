# Configurable SNTP Server ADR

## Status

Proposed.

This document records a planned firmware feature. It is not a description of already implemented behavior.

## Decision Summary

Air360 should allow the user to override the default SNTP server from the `Device` configuration page.

The first version should add:

- a persisted `SNTP server` field in device configuration
- a new `SNTP` block on the `Device` page
- a `Check SNTP` action next to the input
- continued use of the same global `Save and reboot` action for persisting the configured server

The `Check SNTP` action is a runtime validation step. It is not the same as saving the configuration.

## Context

The current firmware uses a hardcoded default SNTP server:

- `pool.ntp.org`

This is simple, but it is too rigid for some deployment environments:

- local routers exposing their own NTP service
- restricted networks where public NTP is filtered
- installations that need a specific local or corporate time source
- debugging of time-sync failures

The current device UI has no way to inspect or override this setting.

## Goals

- allow a user to configure the NTP server without rebuilding firmware
- keep the setting alongside other device-level networking settings
- provide a simple runtime check before saving or rebooting
- avoid adding a separate save flow just for the SNTP field

## Non-Goals

- full multi-server NTP configuration in the first version
- advanced SNTP tuning such as poll intervals or smooth sync mode
- changing time zone handling
- reworking the entire device configuration model

## Architectural Decision

### 1. `SNTP server` belongs to device configuration

The configured server should be stored as part of the persisted device config.

Why:

- it is a device-wide network/runtime setting
- it naturally belongs with station Wi-Fi settings
- it should survive reboot

### 2. The `Device` page should expose a dedicated `SNTP` block

The `Device` page should gain a separate block for time-sync configuration.

The minimum UI should include:

- `SNTP server` text input
- `Check SNTP` button next to the input

The existing `Save and reboot` button remains the only persistence action on the page.

### 3. `Check SNTP` is a validation action, not a save action

The runtime check should:

- use the currently entered SNTP server value
- report whether that server can be used for time synchronization
- avoid mutating stored device config

This allows a user to test a candidate server before committing it.

### 4. Runtime checks should not require a full reboot

The `Check SNTP` action should execute as a dedicated runtime operation.

Expected behavior:

- validate the provided server string
- attempt an SNTP initialization or sync check using that server
- return a clear success/failure result to the UI

The exact implementation can evolve, but the intended UX is:

- user enters server
- clicks `Check SNTP`
- gets feedback immediately
- saves later through the normal `Save and reboot` path if satisfied

## Expected UX

On the `Device` page:

- `Network and Device Settings`
- `SNTP`
  - `SNTP server`
  - `Check SNTP`
- `Save and reboot`

Expected user flow:

1. enter or adjust the SNTP server
2. click `Check SNTP`
3. review the result
4. click `Save and reboot` only if the setting is correct

## Validation Expectations

At minimum, the feature should handle:

- empty value meaning “use firmware default”
- syntactically invalid hostnames
- runtime failure to reach or use the server
- successful synchronization using the entered server

The UI feedback should distinguish:

- invalid input
- request failed
- sync failed
- sync succeeded

## Alternatives Considered

### Option A. Keep the server hardcoded

Rejected.

Why:

- too rigid for real deployments
- weak diagnostics story when time sync fails

### Option B. Add one configurable SNTP server with a runtime check

Accepted direction.

Why:

- simplest useful feature
- low UI complexity
- fits the current device config model

### Option C. Full multi-server SNTP configuration

Deferred.

Why:

- more complexity in storage and UI
- not required for the first practical improvement

## Practical Conclusion

Air360 should expose a configurable `SNTP server` on the `Device` page and provide a separate `Check SNTP` action for runtime validation.

The setting should be persisted only through the normal `Save and reboot` flow, while the check action should remain a lightweight runtime test.
