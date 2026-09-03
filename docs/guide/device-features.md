# Air360 Firmware Features — Device Page

The **Device** page in the firmware web interface is where all network and device
settings live. It is organised into cards, each covering one feature. This guide
describes what every card does and how to use it.

The Device page is reachable in every network mode, including setup AP — so even
a device with broken Wi-Fi credentials can always be reconfigured or reflashed.

---

## Identity

A single **Device name** field (1–31 characters) that gives the device a
human-readable identity across everything it touches.

### What it does

The name you set here appears in several places at once:

- **On the local network (mDNS)** — the device is reachable at
  `http://{device_name}.local` in station mode. The hostname is derived from the
  name: it's lowercased, spaces and other non-alphanumeric characters become
  `-`, and it falls back to `air360` if nothing usable remains. So "Balcony
  Station" makes the device reachable at `http://balcony-station.local`.
- **Setup access point name** — helps you tell devices apart when configuring
  more than one.
- **Upload payloads** — identifies the device to backends.
- **Home Assistant** — the name shown for the BLE device (see BLE advertising).

The name must not be empty. Changing it takes effect after the reboot that
follows saving, since the mDNS hostname is set at network start.

---

## Wi-Fi station

Credentials for the device's primary Wi-Fi uplink — the network it joins to reach
the internet and to serve its own web interface.

### What it does

- **SSID** — the name of the Wi-Fi network to join (up to 32 characters).
- **Password** — the network password (up to 63 characters), with a **Show/Hide**
  toggle so you can verify what you typed. The password field is disabled while
  the SSID is empty.

Saving valid credentials makes the device reboot, join the network, and become
reachable at its DHCP address and at `http://{device_name}.local`.

### Network scanner

In **setup AP mode** the SSID field is a dropdown populated by an on-device Wi-Fi
scan: the firmware lists nearby networks (name and signal strength), and picking
one fills in the SSID for you — no need to type it by hand. Scanning runs in the
background, and hidden or duplicate networks are filtered out. In station mode
the dropdown is hidden and the SSID is a plain text field.

### Setup AP fallback

Wi-Fi credentials are optional to have, but the device always needs a way in:

- **Leave the SSID empty and save** — on the next boot the device starts its own
  setup access point (`air360` / `air360password` at `http://192.168.4.1`)
  instead of joining a network. This is the deliberate way to reset connectivity.
- **If a configured network can't be joined** (wrong password, out of range), the
  device automatically falls back to the same setup AP so you never lose access.

### Automatic recovery

- **Background retry** — while the setup AP is active and credentials are stored,
  the device keeps retrying the configured network every few minutes. The setup
  AP stays available the whole time, so it reconnects on its own once the network
  comes back — no manual intervention needed.
- **Reconnect on disconnect** — during normal operation, an unexpected Wi-Fi drop
  triggers automatic reconnect attempts in the background.

### LED at a glance

The onboard LED reflects Wi-Fi state: **green** = connected in station mode,
**pink** = setup AP mode (no credentials or a failed join).

---

## Wi-Fi power save

A single toggle that lets the Wi-Fi radio sleep between beacons to cut idle power
draw. Off by default.

### What it does

- **Off (default)** — the radio stays fully awake, giving the lowest upload
  latency and the most responsive web interface.
- **On** — enables modem sleep: the radio powers down between the router's DTIM
  beacons and wakes just often enough to stay associated. This lowers average
  current draw significantly (roughly **80–100 mA → 20–30 mA**) at the cost of
  slightly higher upload and page-load latency.

### When to use it

- **Turn it on** for battery- or solar-powered installations, where cutting a few
  tens of milliamps meaningfully extends runtime.
- **Leave it off** for mains-powered stations, where responsiveness matters more
  than power draw.

Power save applies to **station mode only** — it has no effect on the setup
access point, which always keeps its radio fully awake so setup stays snappy. The
setting takes effect right after the device joins the network.

---

## Power gate (INA)

A card that appears only when an **INA219 or INA226** power monitor is in your
sensor list. It stops the device from powering the radios while the supply is
too weak, which is the morning problem on solar builds: the pack sat at the BMS
cutoff all night, the panel now delivers a trickle, and every Wi-Fi burst pulls
the rail into a brownout reset before the battery can recover. Off by default.

### What it does

The firmware boots in a power-aware order. Before the modem, Wi-Fi, BLE, or any
other sensor is started, only the INA runs and takes a bus-voltage reading.

- **Reading at or above the threshold** — boot continues normally.
- **Reading below the threshold** — the LED goes dark and the device deep-sleeps
  for the *First sleep* duration. On every consecutive low-voltage wake-up the
  sleep doubles (5 → 10 → 20 → 30 min with the defaults) up to *Maximum sleep*.
  The chain resets as soon as one boot passes the gate.
- **Four low-voltage sleeps in a row** — the next boot goes through anyway, radios
  and all, and the Overview page shows a red *Bypassed* chip. This is the escape
  hatch for a threshold set above what your supply ever reaches: open `/config`
  and lower it. If the supply really is too weak, that boot browns out and the
  sleep chain simply starts over.
- **No INA reading within *Sample wait*** — the gate is skipped and boot
  continues. A broken or missing power monitor never keeps the station offline.
  The one exception: if the previous reset was a brownout and the INA still gave
  no reading, the device sleeps as well.

While the device sleeps nothing is served: no web UI, no uploads, no BLE. It
wakes on its own timer and re-evaluates.

### Choosing the threshold

The value is compared with the INA **bus voltage**, so it depends on where the
module sits in your power chain:

| INA wired into | Bus voltage measures | Suggested threshold |
|----------------|----------------------|---------------------|
| The shield's 5 V input path (standard assembly) | LM2596 output; it sags to ~4.5–4.8 V when a 2S LiFePO4 pack drops near 6.2–6.5 V | `4700` mV (default) |
| The battery side, between BMS and step-down (INA226 only, up to 36 V) | Pack voltage directly | ~`6200` mV for 2S LiFePO4 |

The reading is taken with no radio running, so it is the pack's lightly-loaded
voltage. The card shows the latest INA reading above the fields; watch it over a
few days and set the threshold a little above the level at which the device is
known to survive a Wi-Fi join.

### Fields

| Field | Default | Meaning |
|-------|---------|---------|
| Start threshold | `4700` mV | Boot continues only at or above this bus voltage |
| Sample wait | `15` s | Longest wait for the first INA reading before the gate is skipped |
| First sleep | `300` s | Deep-sleep duration after the first low-voltage boot |
| Maximum sleep | `1800` s | Cap for the doubling sleep duration |

### What you see

- The **Overview** page shows a `Power gate` row in the System card while the
  gate is enabled: *Passed* with the measured voltage, or *Skipped* with the
  reason, plus how many low-voltage sleeps preceded this boot.
- The **Diagnostics** raw JSON carries a `power_gate` object with the same data.
- The serial log prints one `air360.power_gate` line per decision, including the
  sleeps you never see in the browser.

### Limitations

- Deep sleep powers down the ESP32-S3 only. The step-down module's quiescent
  current, a cellular modem left powered, and sensors with their own regulators
  keep drawing from the pack. The gate removes the radio peaks, not the whole
  load.
- The gate runs once per boot. A supply that collapses later is still handled by
  the brownout detector and the next boot.

---

## Time (SNTP)

Where the device gets its clock. The ESP32-S3 has no battery-backed real-time
clock, so after every boot it synchronises the time over the network using SNTP.

### What it does

- **SNTP server** — the NTP server to sync from. Leave it empty to use the
  default, `pool.ntp.org`; set your own (up to 63 characters) if you run a local
  time server or prefer a specific pool.
- **Check SNTP** — a button that tests reachability of the server you typed
  right now and shows the result inline, without saving or rebooting. Use it to
  confirm a custom server works before committing to it.

Synchronisation runs over the station Wi-Fi connection and is not attempted in
setup AP mode. The device clock runs in **UTC**.

### Why it matters

Accurate time is a hard requirement for uploading data — every measurement is
timestamped, and the device will not queue or send anything until the clock is
valid:

- Before sync, sensors are still polled and live readings appear in the web
  interface, but nothing accumulates in the upload queue.
- Once sync succeeds, buffered and new measurements start flowing to the enabled
  backends.

If the server is briefly unreachable at boot, the device keeps retrying in the
background while the connection is healthy, so it recovers on its own once the
server responds.

---

## Static IP

By default the device takes its address from the router by DHCP. This card lets
you pin a fixed IPv4 address instead — useful when you want the device always
reachable at the same address for port forwarding, bookmarks, or firewall rules.

### What it does

A switch reveals four fields when turned on:

- **IP address** — the fixed address to claim (e.g. `192.168.1.100`).
- **Subnet mask** — e.g. `255.255.255.0`.
- **Gateway** — the router address (e.g. `192.168.1.1`).
- **DNS server** — e.g. `8.8.8.8`; leave it empty to use the gateway as DNS.

With the switch off, the device stays on DHCP and the fields are hidden.

### Convenience

When you enable static IP for the first time on a device that is currently
connected via DHCP, the firmware **pre-fills the address, netmask, and gateway
from the current lease** (and DNS if available). In most cases you can just flip
the switch, tweak the last octet if you like, and save — no need to look up your
network settings.

Static IP applies to **station mode only**; the setup access point always keeps
its fixed `192.168.4.1` address regardless of this setting.

---

## Mobile uplink

Lets the device upload over a cellular modem and SIM card instead of Wi-Fi — the
right choice for rooftop stations, remote field sites, and anywhere without a
reliable Wi-Fi network. Turned off by default; a switch reveals the settings.

### What it does

When enabled, the cellular modem becomes the device's **primary uplink**. The
fields:

- **APN** — required; the access point name from your carrier (e.g. `internet`,
  `hologram`).
- **Username** / **Password** — carrier PPP credentials; leave empty if your SIM
  doesn't need them. The password has a Show/Hide toggle.
- **SIM PIN** — leave empty if the SIM has no PIN lock.
- **Modem type** — the AT-command dialect used to drive the modem. Supported:
  **SIM7600** (default), SIM7070, SIM7000, BG96, EC20, SIM800, and **Generic**
  (any AT-command modem). Pick the one matching your hardware.
- **Connectivity check host** — an IPv4 address the device pings after the
  cellular link comes up to confirm it actually has internet (e.g. `8.8.8.8`).
- **Wi-Fi debug window (seconds)** — see below.

### Wi-Fi debug window

Because cellular becomes the primary uplink, Wi-Fi would normally shut down after
boot. The debug window keeps Wi-Fi active alongside cellular for a set number of
seconds after each boot, so you still have a local way to reach the web interface
and check on the device. Set it to `0` to disable and go cellular-only. Values up
to 3600 seconds (one hour) are accepted.

### Automatic recovery

The modem link is self-healing: if the connection drops or the modem stops
responding, the device reconnects on its own in the background. Persistent
failures escalate gradually — a full modem re-init, then a hardware power-cycle
of the modem, and only as a last resort a device reboot — so a temporary loss of
signal or a stuck modem recovers without anyone touching the device. Carrier
search and weak-signal periods are tolerated and don't trigger unnecessary
power-cycling.

Wiring and hardware notes for the default SIM7600E module are in the firmware
docs.

---

## BLE advertising

Broadcasts the current sensor readings over Bluetooth Low Energy so nearby
devices — most notably **Home Assistant** — can pick them up locally, with no
cloud and no network round-trip. Off by default; a switch reveals the setting.

### What it does

When enabled, the device continuously broadcasts its latest sensor values as
**BTHome v2** advertisements. This is a passive, broadcast-only format:

- **No pairing and no connection** — any nearby BLE scanner just receives the
  data as it is broadcast.
- The advertisement always reflects the **last valid reading**, so values are
  available even when the device has no internet connection.
- Wi-Fi and BLE run at the same time; keeping BLE on has a negligible effect on
  uploads.

### Advertising interval

A dropdown sets how often the device broadcasts. Shorter intervals update faster
but draw more power:

| Interval | Good for |
|----------|----------|
| 100 ms | High-frequency scanning; highest power draw |
| 300 ms | Fast updates |
| **1 s (default)** | **Home Assistant and general use** |
| 3 s | Power-constrained deployments |

### Home Assistant

BTHome v2 is auto-detected by Home Assistant's built-in Bluetooth integration.
Once the device is advertising, it appears under **Settings → Devices &
Services → Bluetooth** with its sensor values — add it, and no further setup is
needed. The name shown matches the device's configured name.

---

## Firmware update

Updates the firmware straight from the browser over Wi-Fi — no USB cable and no
serial tool required. The card shows the version currently running and which slot
the next install will target.

### Which release file to use

Every release publishes more than one image. Pick the one that matches how you're
updating:

- **Over the web (this card): use the `ota` image.** It contains just the
  application and is written to the device's spare slot. This is the file to
  upload here.
- **Over USB with a flasher (e.g. ESP Flash): use the `full` image.** It is the
  complete merged image (bootloader + partition table + application) meant for
  first-time flashing and full recovery over a serial connection.

Using the `full` image in the web updater will not work — the web path expects
the application-only `ota` image.

### How it works

1. Choose the `ota` `.bin` file and press **Upload and install**. A progress bar
   shows the transfer.
2. The image is written to the **inactive slot** — the running firmware is left
   untouched until the new image is ready.
3. The device reboots into the new image.

### Safety and rollback

The update is fail-safe. The freshly installed image boots on trial first: only
once the device comes all the way up does it mark the new firmware as good. If
the new image fails to boot, the device **automatically rolls back** to the
previous, known-working firmware — so a bad update can't brick the device. While
an image is still on trial, the card shows a "pending verification" notice.

Because the web updater is part of the setup page, it's reachable even over the
setup access point — a device with broken Wi-Fi credentials can still be
recovered by flashing new firmware from the browser.






