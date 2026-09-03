# Air360 Firmware — Troubleshooting & Reference

Help for when a device isn't behaving as expected: finding it on the network,
understanding upload timing, fixing common problems, and the current known
limitations.

---

## Finding the device in station mode

After a successful join, the device is reachable two ways.

### By name (recommended)

Open `http://{device-name}.local/` in a browser on the same network. The
`{device-name}` part is the configured device name, lowercased with spaces
replaced by `-`. The default is `air360`, so the default address is
`http://air360.local/`.

mDNS works natively on macOS, iOS, Android, and most Linux systems. On Windows it
needs the Bonjour service (installed with iTunes, Apple devices, or some printer
drivers). If `.local` doesn't resolve, use the IP method.

### By IP address

- Check the connected-client list in your router admin panel — look for the
  device name as the DHCP hostname.
- Use a network-scanner app on your phone.
- Check the serial monitor output during boot — the assigned IP is logged.

Then open `http://<device-ip>/`.

---

## Time synchronization and upload timing

Uploads require valid UTC time, which the firmware gets over SNTP after the
uplink is ready (Wi-Fi station or cellular PPP). In practice:

- It's normal for uploads **not** to start the moment the web UI first becomes
  reachable.
- The Overview Connection block shows the current UTC date once time is synced;
  before sync it shows `1970-01-01`.
- The Health pill stays at `Starting up` while SNTP and sensors warm up, then
  goes to `Healthy` once checks pass.
- SNTP is retried continuously while the uplink is active, and works over both
  Wi-Fi and cellular — no separate cellular configuration is needed.

---

## Troubleshooting

### The device stays in AP mode after reboot

The Wi-Fi SSID or password is wrong, or the network is out of range.
**Fix:** reconnect to the setup AP, open `/config`, correct the credentials, and
save again.

### The device is dark and unreachable in the morning, then comes back later

On a solar build with the [Power gate](device-features.md#power-gate-ina)
enabled this is the gate doing its job: the INA bus voltage was below the
threshold at boot, so the device deep-slept instead of powering the radios. The
LED stays off during the sleep. Once it boots, the Overview page's `Power gate`
row shows how many low-voltage sleeps preceded that boot. If it sleeps too
eagerly, lower the *Start threshold*; if it brown-out cycles instead of
sleeping, the threshold is too low for your wiring, or the INA is not in the
sensor list.

### The UI opens but uploads do not start

Check that:

- The mode on Overview is `station`, not `setup AP`.
- The UTC date on Overview is valid, not `1970` — time must sync first.
- Sensor cards show actual readings.
- Backend cards show no transport or HTTP errors.

### A sensor shows no data or an error

Check that:

- The sensor is enabled.
- The wiring matches the configured transport (correct I2C address or GPIO pin).
- The sensor card's runtime state and error message on Overview.
- For I2C sensors: the sensor is powered and correctly connected to SDA/SCL.

### The sensor queue count keeps growing

If a sensor's queued sample count only increases:

- Backend uploads are probably failing — check backend cards for errors.
- The device may have lost its station uplink.
- UTC time may not be synced — check the date on Overview.

### Air360 API uploads fail or the secret is rejected

The stored upload secret doesn't match the backend's device record — usually
because the device was erased and a **new** secret was generated instead of the
original being re-entered.

- **If you saved the original secret:** open **Backends → Air360 API → Change**,
  select **I already have an upload secret**, paste the original, and save.
- **If you lost it:** the existing backend record can't be recovered without a
  backend-side reset — contact the backend operator, then generate a new secret.

### Moving the device to a different Wi-Fi network

Open **Device**, update the SSID and password, and press **Save and reboot**.

### Cellular uplink stays at `cellular (connecting)`

The modem isn't reaching the network. Check:

- The SIM is inserted correctly and the carrier has signal at that location.
- The APN is correct for your carrier.
- The SIM PIN is set if the SIM has a PIN lock.
- PAP username/password are correct if your carrier requires them.
- Serial logs from the modem task (`air360.cellular`) show the exact failure.

The firmware retries automatically with backoff. If the modem reports
"searching", it keeps polling without power-cycling, so you normally don't need
to reboot manually.

### Cellular connected but uploads are not going through

Check:

- The connectivity-check result in the Connection block (`ping ok` /
  `ping failed`). A failed ping means PPP is up but there's no working route —
  verify the APN and your data plan.
- Backend cards show the last HTTP status and error — look for DNS or connection
  errors.
- The UTC date on Overview must not show `1970` — SNTP must sync first.

---

## Current limitations

- Device name, network, and cellular changes require a reboot.
- Sensor changes require pressing **Apply now** — staging alone doesn't persist.
- Setup AP mode exposes only the Device page.
- The upload interval is global — it applies to all enabled backends.
- Sensor poll interval must be between 30 000 ms and 1 800 000 ms (30 s–30 min).
- When cellular is enabled, the web UI is only reachable during the Wi-Fi debug
  window after boot; set a non-zero window to keep web access for configuration.
- The `storage` partition is reserved but not currently used.
