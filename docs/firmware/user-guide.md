# Air360 Firmware User Guide

This guide explains how to use the Air360 firmware web interface — from first boot through sensor and backend configuration in normal operation.

It is written for device users, not for firmware developers.

---

## What You Need

- A device flashed with the Air360 firmware
- A phone, tablet, or laptop with Wi-Fi
- The SSID and password of the Wi-Fi network the device should join

---

## Flashing The Firmware

If you are starting from a release package, the easiest path is the merged `full.bin` image via the browser-based ESP flash tool:

```
https://espflash.app/
```

Recommended flow:

1. Download the current `full.bin` release asset.
2. Open `https://espflash.app/` in a desktop browser with Web Serial support (Chrome or Edge).
3. Connect the device by USB.
4. Select the device serial port in the browser.
5. Select the Air360 `full.bin` file.
6. Start flashing and wait for the device to reboot.

After flashing, if no station Wi-Fi credentials are saved, the device boots into setup AP mode and exposes `http://192.168.4.1/config`.

---

## Network Modes

The firmware operates in one of two modes:

| Mode | When | Access |
|------|------|--------|
| `setup AP` | No valid station credentials, or station join failed | Connect to the device's own Wi-Fi network, open `http://192.168.4.1/` |
| `station` | Joined the configured Wi-Fi network | Open the device by its DHCP IP address on your network |

In **setup AP mode** the navigation is intentionally limited to the `Device` page only. All other pages redirect to `Device`.

In **station mode** the full UI is available.

---

## First Boot: Setup AP Mode

When no Wi-Fi credentials are configured, the device starts in setup AP mode.

### Step 1 — Connect to the device AP

1. Power on the device.
2. Open Wi-Fi settings on your phone or laptop.
3. Connect to the Air360 setup network. The default SSID is `air360`, password `air360password`.

### Step 2 — Open the setup page

Open `http://192.168.4.1/` or `http://192.168.4.1/config` in a browser.

In setup AP mode both routes lead to the Device configuration page.

### Step 3 — Enter station Wi-Fi credentials

1. Optionally change the **Device name** (used as the DHCP hostname on your network).
2. In **Wi-Fi SSID**, pick a network from the scanned dropdown or type the SSID manually.
3. Enter **Wi-Fi password**.
4. Press **Save and reboot**.

The device reboots and attempts to join the configured network.

> To return to setup AP mode at any time, go to `Device`, clear the Wi-Fi SSID field, and save. The device will reboot into setup AP mode.

---

## Finding The Device In Station Mode

After a successful join, the device receives a DHCP address from your router.

Ways to find it:

- Check the connected client list in your router admin panel — look for the configured Device name as the hostname.
- Use a network scanner app on your phone.
- Check the serial monitor output during boot — the assigned IP is logged.

Once you have the IP, open `http://<device-ip>/` in a browser.

---

## Web UI Overview

The navigation bar contains five sections:

| Section | Purpose |
|---------|---------|
| **Overview** | Runtime dashboard: health, identity, sensors, backends |
| **Device** | Network credentials, static IP, and cellular modem settings |
| **Sensors** | Sensor inventory — add, configure, remove |
| **Backends** | Upload targets — enable, configure, monitor |
| **Status JSON** | Raw machine-readable runtime state at `/status` |

---

## Overview Page

The Overview page is the main runtime dashboard.

![Runtime Overview](images/firmware_overview.png)

A **Health pill** (`Healthy` / `Unhealthy`) is shown inline under the page heading.

The stats bar shows four values:

| Field | Description |
|-------|-------------|
| Mode | Current network mode (`station` or `setup AP`) |
| Uplink | Active uplink: `wifi`, `cellular`, `cellular (connecting)`, or `offline`. Cellular is always the primary uplink when enabled. |
| Uptime | Time since last boot |
| Boot count | Total number of boots since first flash |

### Connection block

Shows the current network connection state:

- **Date** — current UTC date and time (from SNTP when synced).
- **Wi-Fi** — SSID and current IP address, or `not connected`.
- **Cellular** (shown only when cellular is enabled) — PPP IP address, signal strength in dBm, and ping status (`ping ok` / `ping failed`).

### Backend cards

Each enabled backend shows its current state, last upload attempt time, HTTP status code, and response time.

### Sensor cards

Each configured sensor shows:

- Sensor model and transport binding
- Configured poll interval
- Runtime state
- Queued sample count — how many collected measurements are currently waiting in the upload queue
- Latest readings
- Runtime error message, if any

---

## Device Page

The Device page manages network credentials, static IP, and cellular modem settings.

![Device Configuration](images/firmware_device.png)

Available fields:

| Field | Description |
|-------|-------------|
| Device name | Logical name shown in the UI and used as the DHCP hostname |
| Wi-Fi SSID | Station network to join |
| Wi-Fi password | Station network password |
| SNTP server | NTP hostname; leave empty to use `pool.ntp.org` |
| Use static IP | When checked, the device uses the configured address instead of DHCP |
| IP / Mask / Gateway / DNS | Static IP parameters; pre-filled from current DHCP lease when not yet configured |
| Enable cellular uplink | Enables the SIM7600E modem as the primary uplink |
| APN | Carrier APN; pre-filled with `internet` when empty |
| Username / Password | Optional PAP credentials |
| SIM PIN | Optional SIM PIN; leave empty if the SIM has no lock |
| Connectivity check host | IPv4 address to ping after PPP connects; leave empty to skip |
| Wi-Fi debug window | Seconds Wi-Fi stays active alongside cellular after boot; `0` = disabled |

**Saving device settings reboots the device.**

This page is available in both setup AP mode and station mode. In station mode it is useful when moving the device to a different Wi-Fi network or enabling cellular.

---

## Sensors Page

The Sensors page manages the sensor inventory. Sensors are organized into categories.

![Sensor Configuration](images/firmware_sensors.png)

### Supported sensors by category

| Category | Models |
|----------|--------|
| Climate | BME280, BME680 |
| Temperature & Humidity | SHT4X, HTU2X, DHT11, DHT22 |
| Temperature | DS18B20 |
| CO2 | SCD30 |
| Light | VEML7700 |
| Particulate Matter | SPS30 |
| Location | GPS (NMEA) |
| Gas | ME3-NO2 |

All categories except **Gas** allow only one configured sensor at a time.

### Default transport bindings

| Sensor | Transport | Default address / pin |
|--------|-----------|----------------------|
| BME280 | I2C bus 0 | `0x76` |
| BME680 | I2C bus 0 | `0x77` |
| SHT4X | I2C bus 0 | `0x44` |
| HTU2X | I2C bus 0 | `0x40` |
| SCD30 | I2C bus 0 | `0x61` |
| VEML7700 | I2C bus 0 | `0x10` |
| SPS30 | I2C bus 0 | `0x69` |
| GPS (NMEA) | UART1 | GPIO18 (RX), GPIO17 (TX) |
| DHT11, DHT22 | GPIO | GPIO4, GPIO5, or GPIO6 |
| DS18B20 | GPIO (1-Wire) | GPIO4, GPIO5, or GPIO6 |
| ME3-NO2 | Analog (ADC) | GPIO4, GPIO5, or GPIO6 |

I2C sensors allow an optional address override if your module uses a non-default address. GPIO and analog sensors require selecting one of the three available board pins.

### Adding a sensor

1. Open **Sensors**.
2. Find the category you want.
3. Select the sensor model.
4. Set the **Poll interval (ms)**. Minimum is 5000 ms for most sensors.
5. For I2C sensors: adjust the **I2C address** only if needed.
6. For GPIO or analog sensors: select the board pin from the dropdown.
7. Make sure the sensor is enabled.
8. Press **Stage sensor changes**.

### Removing or updating a sensor

1. Open the existing sensor card.
2. Change settings and press **Stage sensor changes**, or press **Stage sensor deletion**.

### Applying changes

Sensor edits are staged in memory and are not saved until you explicitly apply them:

- **Apply now** — persists the staged sensor list to NVS and rebuilds the sensor runtime without rebooting the device.
- **Discard pending changes** — discards all staged edits and returns to the last saved state.

After pressing **Apply now**, sensor readings should appear in **Overview** within the first configured poll interval.

---

## Backends Page

The Backends page configures where measurement data is uploaded.

![Upload Backends](images/firmware_backends.png)

### Upload interval

The **Upload interval** field at the top of the page controls how often the device sends a data batch to all enabled backends. The allowed range is 10 000–300 000 ms (10 s to 5 min). The default is 145 000 ms.

### Sensor.Community

To use Sensor.Community:

1. Open **Overview** and note the **Short ID**.
2. Register your device at `https://devices.sensor.community/` using that Short ID.
3. Return to the firmware **Backends** page.
4. Enable Sensor.Community.
5. Leave **Device ID override** at its default value unless you need a different ID for debugging.
6. Press **Save**.

> The ID registered on the Sensor.Community portal must match the ID the firmware sends. By default this is the Short ID. If you fill in the Device ID override field, that value is used instead and must match the portal registration.

### Air360 API

Enable Air360 API and press **Save**. No additional credentials are required in the current firmware version.

### Save behavior

Backend settings are saved immediately when you press **Save** — there is no staged apply flow for backends.

### Backend runtime status

Each backend card shows:

- Enabled or disabled state
- Endpoint URL
- Last upload attempt time
- HTTP status code
- Response time in ms
- Last error, if any

---

## Status JSON

The raw runtime endpoint is available at:

```
http://<device-ip>/status
```

This endpoint is useful for advanced troubleshooting and external integrations. It includes build information, boot count, reset reason, network state, sensor runtime state with latest measurements and queued sample counts, and backend runtime state.

---

## Time Synchronization And Upload Timing

Uploads require valid UTC time. The firmware synchronizes time via SNTP after joining the station network.

What this means in practice:

- It is normal for uploads to not start immediately when the web UI first becomes reachable.
- The **Overview** page shows the current UTC date once time is synchronized. Before sync the date field shows `1970`.
- The **Health** bar shows `time ok` once time sync succeeds.
- The firmware retries time synchronization continuously while in station mode.

---

## Typical Workflows

### First-time setup

1. Power on the device.
2. Connect to the `air360` setup AP (password: `air360password`).
3. Open `http://192.168.4.1/`.
4. Enter station Wi-Fi SSID and password.
5. Press **Save and reboot**.
6. Find the device IP in your router and open the full UI.

### Configuring sensors

1. Open **Sensors**.
2. Add each sensor you have physically connected to the device.
3. Stage the changes.
4. Press **Apply now**.
5. Check **Overview** to confirm sensors show readings within the first poll interval.

### Enabling Sensor.Community upload

1. Open **Overview** → note the Short ID.
2. Register the device at `https://devices.sensor.community/`.
3. Open **Backends** → enable Sensor.Community → press **Save**.
4. Set the upload interval as needed.
5. Monitor upload status on **Overview** or **Backends**.

---

## Troubleshooting

### The device stays in AP mode after reboot

- Wi-Fi SSID or password is incorrect.
- The configured Wi-Fi network is out of range or unavailable.

**Fix:** Reconnect to the setup AP, open `/config`, correct the credentials, and save again.

### The UI opens but uploads do not start

Check:
- The mode shown on **Overview** is `station`, not `setup AP`.
- The current UTC date on **Overview** is valid, not `1970` — time sync must succeed first.
- Sensor cards show actual readings.
- Backend cards do not show transport or HTTP errors.

### A sensor shows no data or an error

Check:
- The sensor is enabled.
- Physical wiring matches the configured transport (correct I2C address or GPIO pin).
- The runtime state and error message on the sensor card in **Overview**.
- For I2C sensors: confirm the sensor is powered and properly connected to SDA/SCL.

### The sensor queue count keeps growing

If the queued sample count on a sensor card keeps increasing without going down:

- Backend uploads are likely failing — check backend cards for errors.
- The device may have lost station uplink.
- UTC time may not be synchronized — check the date on **Overview**.

### Moving the device to a different Wi-Fi network

Open **Device**, update the SSID and password, and press **Save and reboot**. The device will attempt to join the new network.

---

## Current Limitations

- Device name and Wi-Fi changes require a reboot.
- Sensor changes require explicitly pressing **Apply now** — staging alone does not persist.
- Setup AP mode exposes only the Device page.
- The upload interval is global — it applies to all enabled backends.
- Sensor poll interval cannot be set below 5000 ms (2000 ms for DHT11/DHT22).
- The `storage` partition is reserved but not currently used.
- OTA firmware update is not yet implemented.
