# Air360 Firmware Features — Backends

The **Backends** page is where you choose where the device sends its
measurements. You can turn on as many targets as you like: the device sends the
same readings to every enabled one, each on its own schedule, and if one target
is down the others keep working.

This guide explains each backend, what every field is for, and what a first-time
user needs to fill in.

---

## Before anything uploads

Two conditions apply to every backend, regardless of which you enable:

- **The clock must be set.** Uploads only start once the device has synced time
  over the network (SNTP). Until then readings show up live in the web interface
  but are not sent. See [Time (SNTP)](device-features.md#time-sntp).
- **The device must be connected** (Wi-Fi station or cellular). In setup AP mode
  nothing is uploaded.

## Shared settings

- **Upload interval** — one setting at the top of the page controls how often the
  device uploads, for all backends. The range is **30 seconds to 1 hour**.
  Shorter intervals give fresher data but use more power and bandwidth; 1–5
  minutes is a sensible default.
- **Enabled** — each backend card has its own enable switch. A disabled card is
  dimmed and its fields are inactive.
- **Use HTTPS** — on by default. Leave it on unless you have a specific reason
  to use plain HTTP (e.g. a local server without TLS).
- **Status summary** — every card shows the last upload result and timestamp, so
  you can confirm it's working.

---

## Air360 API

**What it is:** the project's own service. It accepts readings from every sensor
and puts your station on the [Air360 map](https://air360.ru/map) right away.

**No account needed** — `air360.ru` requires no registration. The device
authenticates itself with a secret key you create during setup.

Fields to fill:

- **Upload secret** — press **Generate** to create a secret key, then **save it
  somewhere safe**. It's what links your measurement history to this device. If
  you ever reflash the device, paste the same secret back in (press **Change**)
  so your history stays connected. On first setup the field is empty with a
  Generate button; once stored it shows `Configured` with a masked preview.
- **Latitude** and **Longitude** — **required.** Uploads will not start until
  both are set. Click on the embedded map to drop a marker, or type the
  coordinates directly — the map and the fields stay in sync. If a GPS sensor
  has a fix, the fields are filled automatically (or a **Use GPS** button
  appears).
- **Altitude (m above sea level)** — optional but recommended. It improves
  atmospheric-pressure accuracy for stations at elevation. Leave empty or `0` if
  you don't know it.

---

## Sensor.Community

**What it is:** a worldwide network of volunteer air-quality stations. Your
readings join a public map anyone can use. Air360 keeps full compatibility with
it, so your device can contribute here at the same time as Air360.

**Heads up:** only sensor types that Sensor.Community understands are sent
(temperature/humidity, pressure, PM sensors, SCD30, GPS, and similar). Readings
from other sensors are simply skipped for this target. See the
[compatibility table](firmware-and-backends.md#sensorcommunity-compatibility).

Fields to fill:

- **Sensor ID** — this is **read-only**; the device shows you its own ID. Copy
  it and register it as your sensor ID in your personal account at
  [devices.sensor.community/sensors](https://devices.sensor.community/sensors).
  Once registered, your station appears on the Sensor.Community map.

There's no host or path to configure — the endpoint is fixed.

---

## openSenseMap

**What it is:** an open platform for environmental data. Unlike Sensor.Community
there's no fixed list of measurements — you decide what your box publishes,
including CO₂, light, and GPS.

You first create a **senseBox** on [opensensemap.org](https://opensensemap.org/)
and add the sensors you want to publish. Then configure the device:

Fields to fill:

- **Platform** — a dropdown. Pick **Classic** (`api.opensensemap.org`) for the
  normal public platform, or **Next-gen** (`staging.opensensemap.org`) only if
  you're specifically using the newer staging platform. Classic is the usual
  choice. This sets the host and path for you — there are no manual URL fields.
- **senseBox ID** — **required.** The 16–31 character ID from your box's URL on
  openSenseMap.
- **Access token** — optional; required only if your box has authentication
  enabled.

### Sensor mapping

openSenseMap needs to know which box sensor each device reading belongs to, so
this card has a **Sensor mapping** section:

- It lists each live reading your device is producing, with a box for the
  matching **24-character sensor ID** from openSenseMap.
- Press **Fetch sensors from openSenseMap** to pull your box's sensor list — the
  device matches them up and fills in the IDs automatically. Adjust any it
  couldn't match.
- Any reading you leave blank is simply not uploaded.

> A reading only appears in this list after its sensor has produced at least one
> sample, so give the device a moment after adding a sensor before mapping.

---

## InfluxDB

**What it is:** a time-series database you run yourself. Pick it if you want to
keep the raw readings on your own machine and build dashboards in Grafana. Every
sensor value is sent, nothing is filtered.

Fields to fill:

- **Host** — the address of your InfluxDB server.
- **Port** — defaults to the HTTPS/HTTP port; change it if your server uses a
  custom port.
- **Path** — the write path/endpoint on your server.
- **Use HTTPS** — on/off depending on your server's setup.
- **User** and **Password** — optional; fill them in if your server requires
  authentication, otherwise leave empty.
- **Measurement** — the InfluxDB measurement name the readings are written under.

---

## Custom Upload

**What it is:** sends the same data to any HTTP address you enter, so you can
collect readings on a server of your own. Every sensor value is sent.

Fields to fill:

- **Host** — your server's address.
- **Port** — defaults from the protocol; change for a custom port.
- **Path** — the endpoint that receives the data.
- **Use HTTPS** — on/off for your server.

**No authentication is added** to these requests — secure the endpoint yourself
(for example with a hard-to-guess path, a reverse proxy, or network rules). The
exact JSON format the device sends is documented in the firmware
[data format reference](https://github.com/serber/air360/blob/main/docs/firmware/upload-adapters.md).

---

Saving the Backends page normally takes effect immediately, without a reboot.
