# Static IP Configuration ADR

## Status

Implemented.

## Decision Summary

Air360 should allow configuring a static IPv4 address for Wi-Fi station mode from the `Device` page.

The first version should add:

- an optional `Use static IP` toggle in device configuration
- persisted IPv4 fields for:
  - `IP address`
  - `Subnet mask`
  - `Gateway`
  - `DNS server`
- the ability to fall back to DHCP when static IP is disabled

The setting belongs to device-level network configuration and should use the same `Save and reboot` flow as the other `Device` settings.

## Context

The current firmware relies on DHCP for station mode.

That is fine for many home deployments, but it is limiting in environments where operators want:

- predictable addressing on local networks
- easier firewall or router rules
- easier monitoring and device inventory
- operation on networks where DHCP behavior is undesirable or unreliable

This is a recurring request in the broader air-quality sensor ecosystem and fits Air360's local-first administration model.

## Goals

- allow a user to configure a static IPv4 address for station mode
- keep the feature on the existing `Device` page
- preserve DHCP as the default and simplest mode
- keep validation strict enough to avoid obviously broken configuration

## Non-Goals

- IPv6 configuration
- multiple network profiles
- runtime network reconfiguration without reconnect
- enterprise routing features beyond basic IPv4 station settings
- advanced DNS search or multi-DNS settings in the first version

## Architectural Decision

### 1. Static IP belongs to device configuration

Static IP is a device-wide networking concern, so it should be stored in persisted device config.

Suggested fields:

- `station_use_static_ip`
- `station_ip`
- `station_netmask`
- `station_gateway`
- `station_dns`

These fields should apply only to station mode, not setup AP mode.

### 2. DHCP remains the default

If static IP is disabled:

- station mode continues to use DHCP

If static IP is enabled:

- the configured IPv4 settings are applied before or during station bring-up

This keeps the default behavior simple while allowing deterministic addressing where needed.

### 3. The feature should live on the `Device` page

The `Device` page already owns station Wi-Fi settings.

Static IP configuration should therefore appear in the same place, under a dedicated `Network` or `Static IP` block.

Suggested fields:

- `Use static IP`
- `IP address`
- `Subnet mask`
- `Gateway`
- `DNS server`

These fields should be hidden or disabled unless `Use static IP` is enabled.

### 4. Save behavior stays unified

The first version should not introduce a second save action.

Expected UX:

- user edits Wi-Fi and/or static IP fields
- user clicks the same `Save and reboot` button
- firmware persists the new network config
- device reconnects using the new network mode

## Validation Expectations

At minimum, validation should reject:

- malformed IPv4 fields
- missing required fields when static IP is enabled
- `gateway` outside the same subnet if that check is practical

The UI should show clear per-form errors rather than silently accepting broken input.

## Runtime Behavior

Expected station behavior:

- static IP disabled: use DHCP
- static IP enabled: configure station netif with the stored IPv4 values

Setup AP behavior should remain unchanged.

If the configured static IP settings are invalid or unusable at runtime:

- station connect should fail clearly
- the device should still have a recovery path through setup AP

## Alternatives Considered

### Option A. Keep DHCP-only behavior

Rejected.

Why:

- too limiting for managed or semi-managed networks
- prevents a common deployment pattern

### Option B. Add optional static IPv4 configuration

Accepted direction.

Why:

- practical
- easy to explain
- fits the current local web UI model

### Option C. Add full advanced networking configuration

Deferred.

Why:

- too much complexity for the current firmware scope
- not needed for the first useful step

## UI Implications

The `Device` page should gain a dedicated block for static station IP settings.

Suggested user flow:

1. connect the device to Wi-Fi as usual
2. enable `Use static IP`
3. fill in IPv4 fields
4. save and reboot
5. reach the device on the configured address

The UI should also make it obvious that:

- these settings apply to station mode only
- setup AP keeps its own recovery address and does not depend on the station static IP

## Practical Conclusion

Air360 should support optional static IPv4 configuration for station mode through the `Device` page.

DHCP remains the default, while static IP becomes an explicit device-level override for installations that need deterministic local addressing.
