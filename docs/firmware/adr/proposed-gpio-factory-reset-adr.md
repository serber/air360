# GPIO Factory Reset ADR

## Status

Proposed.

## Decision Summary

Add a GPIO-triggered factory reset: holding a designated pin low during boot erases NVS and restores factory defaults, giving a hardware recovery path without requiring a reflash.

## Context

The current firmware has no hardware recovery mechanism. If a device ends up in a state where the web UI is inaccessible (wrong Wi-Fi credentials with no fallback AP, corrupted config, web server failure), the only recovery path is physical reflash via USB with `idf.py flash`.

A GPIO factory reset is a standard pattern in embedded firmware. The detection happens early in the boot sequence — after NVS init but before config load — so it cleanly erases all stored config and lets the normal boot sequence proceed with defaults (which includes entering setup AP mode).

## Goals

- Provide a hardware recovery path that does not require USB or build toolchain access.
- Erase all NVS config on trigger (device config, sensor config, backend config).
- Continue normal boot with factory defaults after erase.
- Not interfere with normal boot when the pin is not held.

## Non-Goals

- Erasing SPIFFS data (measurement queue persistence, if implemented separately).
- Triggering reset during runtime (only at boot).
- Adding a physical button to the PCB as a hardware design requirement — the pin can be an exposed test pad or an existing GPIO used for another purpose.

## Architectural Decision

### Detection window

Between boot step 2 (NVS init) and boot step 4 (config load), read the designated GPIO pin. If the pin is held low (active-low, internal pull-up enabled) for a debounce window of 3 seconds, trigger the factory reset.

```
Boot step 2: NVS init
     ↓
[NEW] Check factory reset GPIO for 3 s
     ├─ Pin held LOW → nvs_flash_erase() + log "Factory reset triggered"
     └─ Pin high or released → continue normally
Boot step 4: Load or create device config
```

A 3-second hold requirement prevents accidental triggers from momentary noise.

### GPIO configuration

- Pin: `CONFIG_AIR360_FACTORY_RESET_GPIO` (Kconfig, default `GPIO_NUM_0` — the BOOT button present on most ESP32-S3 dev boards)
- Mode: input, internal pull-up enabled
- Active level: LOW (button pressed = pin pulled to GND)

### Reset action

On trigger:
1. `ESP_LOGW(kTag, "Factory reset GPIO held — erasing NVS")` with a visible countdown log.
2. `nvs_flash_erase()` — erases the entire NVS partition.
3. `nvs_flash_init()` — reinitializes (so the partition is ready for the subsequent config load).
4. Red LED blink pattern (3 fast blinks) as visual confirmation.
5. Continue boot normally — step 4 will find no config and write factory defaults.

The device boots into setup AP mode because `wifi_sta_ssid` is empty in the freshly written default config.

### Kconfig option

```kconfig
config AIR360_FACTORY_RESET_GPIO
    int "Factory reset GPIO pin number"
    default 0
    help
        GPIO pin that triggers a factory reset when held low during boot.
        Set to -1 to disable the factory reset feature.
```

Setting the value to `-1` disables the feature entirely with no runtime cost.

## Affected Files

- `firmware/main/src/app.cpp` — add GPIO check between step 2 and step 4, add `checkFactoryReset()` helper
- `firmware/main/Kconfig.projbuild` (or `firmware/Kconfig.projbuild`) — add `CONFIG_AIR360_FACTORY_RESET_GPIO`
- `firmware/sdkconfig.defaults` — set default value

## Alternatives Considered

### Option A. No hardware reset (current state)

Recovery requires physical USB reflash. Unacceptable for non-developer users.

### Option B. Triple-reboot detection

Count reboots in NVS; if 3 reboots occur within 10 seconds, trigger factory reset. Avoids needing a dedicated GPIO. More complex, and reboots on power instability could trigger it accidentally.

### Option C. GPIO hold at boot (accepted)

Simple, deterministic, no false triggers with a 3-second hold window. Uses the BOOT button already present on ESP32-S3 dev boards. Configurable or disableable via Kconfig.

## Practical Conclusion

Add a Kconfig-guarded GPIO check in `App::run()` between NVS init and config load. A 3-second hold erases NVS and continues boot with factory defaults. The feature uses the existing BOOT button by default and can be disabled by setting `CONFIG_AIR360_FACTORY_RESET_GPIO=-1`.
