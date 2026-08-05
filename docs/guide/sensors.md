# Air360 Firmware Features — Sensors Page

The **Sensors** page is where you tell the firmware which sensors are physically
connected and how they're wired. For the hardware side — what each sensor
measures, wiring pinout, and default I2C addresses — see
[Hardware Assembly](assembly.md#choose-your-sensors).

---

## Categories

Sensors are organised into categories. **Every category allows only one
configured sensor at a time, except Gas**, which allows more than one.

| Category | Models |
|----------|--------|
| Climate | BME280, BME680, BMP390 |
| Temperature & Humidity | AHT30, SHT3X, SHT4X, HTU2X, DHT11, DHT22 |
| Temperature | DS18B20 |
| CO₂ | SCD30, SCD40, SCD41 |
| Light | VEML7700, OPT3001 |
| Particulate Matter | SPS30, SDS011, PMSX003 |
| Dust Count | PPD42NS |
| Location | GPS (NMEA) |
| Gas | MH-Z19B |
| Power Monitoring | INA219, INA226 |

## Connection per sensor

When you add a sensor, how you connect it depends on its interface:

- **I2C sensors** — share one bus (`SDA=GPIO8`, `SCL=GPIO9`). You only need to
  change the **I2C address** if your module uses a non-default one; the UI offers
  the descriptor-supported addresses. Addresses must not collide on the shared
  bus.
- **UART sensors** (SDS011, PMSX003, MH-Z19B, GPS) — pick the **UART port**; the
  UI shows the RX/TX pins for each. Note GPS and the cellular modem share UART1
  by default and can't both use it — move GPS to UART2 if you need both.
- **GPIO / analog sensors** (DHT11/22, DS18B20, PPD42NS) — select one of the
  available board pins (`GPIO4`, `GPIO5`, `GPIO6`) from the dropdown.

### Default I2C addresses

All I2C sensors share the same bus, so each must have a unique address. These are
the firmware defaults — pick another descriptor-supported address in the sensor
settings if your module differs.

| Sensor | I2C address |
|--------|-------------|
| AHT30 | `0x38` |
| BME280 | `0x76` |
| BME680 | `0x77` |
| BMP390 | `0x77` |
| SHT3X | `0x44` |
| SHT4X | `0x44` |
| OPT3001 | `0x44` |
| HTU2X | `0x40` |
| INA219 | `0x40` |
| INA226 | `0x40` |
| SCD30 | `0x61` |
| SCD40 / SCD41 | `0x62` |
| SPS30 | `0x69` |
| VEML7700 | `0x10` |

## Poll interval

Each sensor has a **Poll interval (ms)** — how often it's read. The allowed range
is **30 000–1 800 000 ms** (30 seconds to 30 minutes).

---

## Adding a sensor

1. Open **Sensors**.
2. Find the category you want.
3. Select the sensor model.
4. Set the **Poll interval (ms)**.
5. For I2C sensors: adjust the **I2C address** only if needed. For UART sensors:
   choose the port. For GPIO/analog sensors: choose the board pin.
6. Make sure the sensor is enabled.
7. Press **Stage sensor changes**.

## Removing or updating a sensor

Open the existing sensor card, change its settings and press **Stage sensor
changes**, or press **Stage sensor deletion**.

## Staging and applying changes

Sensor edits are **staged in memory first** — they are not saved until you
explicitly apply them. This lets you make several changes and commit them
together.

- **Apply now** — persists the staged sensor list and rebuilds the sensor runtime
  **without rebooting** the device.
- **Discard pending changes** — throws away all staged edits and returns to the
  last saved state.

After **Apply now**, readings should appear on the [Overview](monitoring.md)
page within the first configured poll interval.
