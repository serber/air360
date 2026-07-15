# Air360 Firmware Features — Device Page

The **Device** page in the firmware web interface is where all network and device
settings live. It is organised into cards, each covering one feature. This guide
describes what every card does and how to use it.

The Device page is reachable in every network mode, including setup AP — so even
a device with broken Wi-Fi credentials can always be reconfigured or reflashed.

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
