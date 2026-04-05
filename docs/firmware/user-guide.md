# Air360 Firmware User Guide

## Purpose

This guide explains how to use the current Air360 firmware from the moment the device starts in setup AP mode through normal operation in station mode.

It is written for device users, not for firmware developers.

## Before You Start

You need:

- a device flashed with the current Air360 firmware
- a phone, tablet, or laptop with Wi-Fi
- the SSID and password of the Wi-Fi network the device should join

The firmware works in two main network modes:

- `setup AP`
  First-time setup and recovery mode. The device creates its own Wi-Fi network and exposes only the `Device` page.
- `station`
  Normal operating mode. The device joins your Wi-Fi network and exposes the full web UI.

## First Boot: Setup AP Mode

When the device has no valid station Wi-Fi configuration, it starts in setup AP mode.

In this mode:

- the device creates its own Wi-Fi network
- the web UI is available at `http://192.168.4.1/`
- the UI navigation is intentionally limited to the `Device` page

### Step 1. Connect To The Device AP

1. Power on the device.
2. On your phone or laptop, open the Wi-Fi settings.
3. Find the Air360 setup access point.
   The default SSID comes from the firmware defaults configured for the board.
4. Connect to that network.

If the AP has a password, enter the configured setup password.

### Step 2. Open The Setup Page

Open:

```text
http://192.168.4.1/config
```

You can also open:

```text
http://192.168.4.1/
```

In setup AP mode the root route redirects to `Device`.

### Step 3. Configure Station Wi-Fi

On the `Device` page:

1. Check `Device name`.
2. In `Wi-Fi SSID`, choose a network from the scanned list or enter the SSID manually.
3. Enter `Wi-Fi password`.
4. Press `Save and reboot`.

Important behavior:

- the page shows both a manual SSID input and a dropdown with scanned nearby networks
- after saving, the firmware reboots automatically
- setup AP settings are not edited through the UI; the page is focused only on joining the real Wi-Fi network

## After Save: Transition To Station Mode

After reboot, the firmware tries to join the configured Wi-Fi network.

If the join succeeds:

- the device switches to `station` mode
- it receives an IP address from your router
- the full web UI becomes available

If the join fails:

- the firmware falls back to setup AP mode again
- reconnect to the Air360 AP and correct the Wi-Fi settings

## How To Find The Device In Station Mode

Once the device is on your Wi-Fi network, open it through the IP address assigned by your router.

Typical ways to find it:

- look at the connected client list in your router
- look for the configured `Device name` as the DHCP hostname
- use the IP address shown by the router

The current firmware sets the station hostname from `Device name`, normalized into a safe network name.

## Main Sections In Station Mode

In station mode the full UI is available.

The main sections are:

- `Overview`
  Runtime summary, identity information, backend status, and sensor status.
- `Device`
  Device name and station Wi-Fi credentials.
- `Sensors`
  Sensor configuration by category.
- `Backends`
  Upload backend configuration and backend runtime status.
- `Status JSON`
  Raw machine-readable runtime state at `/status`.

## Overview Page

`Overview` is the main runtime dashboard.

It currently shows:

- network mode
- device name
- configured sensor count
- enabled backend count
- uptime
- boot count
- identity information such as chip id, MAC, chip type, current UTC date, reset reason, and IP address
- per-backend overview cards
- per-sensor overview cards

### What Sensor Cards Show

Each sensor card in `Overview` shows:

- sensor name
- binding summary
- configured poll interval
- runtime state
- queued sample count
- latest readings
- runtime error, if one exists

`queued sample count` means how many collected samples for that sensor are currently waiting in the internal upload queue.

## Device Page

`Device` is for station network configuration and identity.

It currently allows you to edit:

- `Device name`
- `Wi-Fi SSID`
- `Wi-Fi password`

Notes:

- in setup AP mode this is the only visible page
- saving device settings reboots the device
- in station mode this page is still available if you need to move the device to another Wi-Fi network

## Sensors Page

`Sensors` is category-based. It does not show one flat list of low-level driver keys.

Current categories:

- `Climate`
- `Temperature / Humidity`
- `Air Quality`
- `Particulate Matter`
- `Location`
- `Gas`

Current supported models:

- `Climate`
  - `BME280`
  - `BME680`
- `Temperature / Humidity`
  - `DHT11`
  - `DHT22`
- `Air Quality`
  - `ENS160`
- `Particulate Matter`
  - `SPS30`
- `Location`
  - `GPS (NMEA)`
- `Gas`
  - `ME3-NO2`

### Category Rules

All current categories except `Gas` are single-sensor categories.

That means:

- you can configure only one `Climate` sensor
- only one `Temperature / Humidity` sensor
- only one `Air Quality` sensor
- only one `Particulate Matter` sensor
- only one `Location` sensor
- `Gas` can contain multiple sensors

### How To Add A Sensor

1. Open `Sensors`.
2. Find the category you want.
3. If the category supports multiple models, choose the model.
4. Set `Poll interval (ms)`.
5. If the sensor uses I2C, adjust `I2C address` only if you intentionally need a non-default address.
6. If the sensor uses GPIO or analog input, choose one of the supported board GPIO pins.
7. Enable the sensor if needed.
8. Press `Stage sensor changes`.

Important current behavior:

- sensor changes are staged in memory first
- they are not written to NVS immediately
- after staging, you must press `Apply and reboot`

### How To Remove Or Change A Sensor

For an existing sensor card:

- change the settings and press `Stage sensor changes`
- or press `Stage sensor deletion`

Then finish with:

- `Apply and reboot`
  Persists the staged sensor set and restarts the device
- `Discard pending changes`
  Throws away staged sensor edits

### Sensor Poll Interval

The current firmware enforces:

- minimum poll interval: `5000 ms`

If a lower value is entered, the UI or server-side validation rejects it.

### Sensor Defaults And Bindings

Current default bindings:

- `BME280`
  I2C bus 0, default address `0x76`
- `BME680`
  I2C bus 0, default address `0x77`
- `ENS160`
  I2C bus 0, default address `0x52`
- `SPS30`
  I2C bus 0, default address `0x69`
- `GPS (NMEA)`
  fixed UART binding from board defaults
- `DHT11`, `DHT22`, `ME3-NO2`
  one of the board sensor GPIO slots

For I2C sensors the current UI allows manual address override.

## Backends Page

`Backends` controls where data is uploaded.

Current supported backends:

- `Sensor.Community`
- `Air360 API`

### What Can Be Configured

The page currently allows:

- global upload interval
- enabling or disabling each backend
- `Sensor.Community` device id override

`Air360 API` does not use a bearer token in the current firmware.

### Save Behavior

Unlike `Sensors`, backend changes are persisted immediately.

There is no staged apply flow for backends.

### Backend Runtime Information

Each backend card shows runtime information such as:

- enabled or disabled
- last attempt time
- HTTP status
- response time
- last error when applicable

On `Overview`, backend cards also show the currently configured global upload interval.

## Status JSON

The raw runtime endpoint is:

```text
/status
```

This endpoint is useful for advanced troubleshooting and integration.

It currently includes:

- build information
- boot count
- reset reason and reset reason label
- network state
- sensor runtime state
- measurements
- per-sensor poll interval
- per-sensor queued sample count
- backend runtime state

## Time Synchronization And Uploads

Uploads require valid Unix time.

Current behavior:

- after station uplink is available, the firmware tries to synchronize UTC time through SNTP
- if time is not valid yet, uploads wait
- the firmware keeps retrying time synchronization in station mode

This means:

- it is normal for uploads not to start immediately at the exact moment the web UI becomes reachable
- `Overview` and `/status` show the current date once time is synchronized

## Typical User Flow

### First-Time Setup

1. Power on the device.
2. Connect to the Air360 setup AP.
3. Open `http://192.168.4.1/config`.
4. Enter station Wi-Fi SSID and password.
5. Press `Save and reboot`.
6. Find the device IP in your router.
7. Open the full UI in station mode.

### Sensor Setup

1. Open `Sensors`.
2. Add the sensors you actually have connected.
3. Stage the changes.
4. Press `Apply and reboot`.
5. After reboot, check `Overview` and `Sensors` for runtime state and readings.

### Backend Setup

1. Open `Backends`.
2. Set the upload interval.
3. Enable the backend you need.
4. For `Sensor.Community`, override the device id only if you intentionally need to.
5. Save the page.
6. Watch backend status on `Overview`.

## Troubleshooting

### The Device Stays In AP Mode

Possible reasons:

- wrong Wi-Fi SSID
- wrong Wi-Fi password
- Wi-Fi network unavailable
- device cannot join the configured network

What to do:

- reconnect to the setup AP
- open `/config`
- correct Wi-Fi credentials
- save and reboot again

### The Web UI Opens But Uploads Do Not Start

Check:

- the device is in `station` mode
- current UTC date is valid, not `1970`
- sensor cards show real readings
- backend cards do not show transport or HTTP errors

### A Sensor Shows No Data

Check:

- the sensor is enabled
- wiring matches the expected transport
- GPIO or I2C address is correct
- the runtime state and runtime error on the sensor card

### A Sensor Queue Keeps Growing

If `queued sample count` keeps increasing:

- backend uploads may be failing
- upload interval may be too long for the current sensor poll interval
- the device may be offline or time may not yet be synchronized

## Current Limitations

- The firmware UI is local and server-rendered; there is no cloud-side device provisioning flow.
- Sensor changes require `Apply and reboot`.
- Setup AP mode intentionally exposes only the `Device` page.
- Backend upload interval is global, not per backend.
- Sensor poll interval cannot be set below `5000 ms`.
