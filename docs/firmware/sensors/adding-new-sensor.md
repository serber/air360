# Adding A New Sensor

## Status

Implemented workflow. Keep this checklist aligned with the current sensor registry, runtime model, and documentation set.

## Scope

Complete step-by-step guide for adding a new sensor driver. Follow every section in order; each step is required unless marked optional.

## Source of truth in code

- `firmware/main/include/air360/sensors/sensor_types.hpp`
- `firmware/main/src/sensors/sensor_registry.cpp`
- `firmware/main/src/web_server.cpp`
- `firmware/main/src/uploads/adapters/air360_api_uploader.cpp`
- `backend/src/contracts/sensor-type.ts`

## Read next

- [supported-sensors.md](supported-sensors.md)
- [README.md](README.md)
- [../transport-binding.md](../transport-binding.md)

---

## Step 1 — Add the SensorType enum value

File: `firmware/main/include/air360/sensors/sensor_types.hpp`

Append a new entry to `SensorType`. Values are assigned sequentially; never reuse a retired value (value `7` is permanently reserved for the removed SDS011).

```cpp
kMyNewSensor = 16U,
```

---

## Step 2 — Add a managed component (if needed)

If the driver uses a third-party esp-idf-lib component:

**`firmware/main/idf_component.yml`** — add the dependency:
```yaml
esp-idf-lib/mycomponent: "1.0.0"
```

**`firmware/main/CMakeLists.txt`** — add to `REQUIRES`:
```cmake
esp-idf-lib__mycomponent
```

Run `idf.py build` once to fetch the component and update `dependencies.lock`.

---

## Step 3 — Add Kconfig defaults (UART sensors only)

File: `firmware/main/Kconfig.projbuild`

Add default port, RX/TX GPIO, and baud rate inside `menu "air360 firmware"`. Follow the existing GPS and MH-Z19B blocks as a template. Choose a port and GPIO pair that does not conflict with existing assignments:

| Sensor | Port | RX | TX |
|--------|------|----|----|
| GPS (NMEA) | UART1 | GPIO18 | GPIO17 |
| MH-Z19B | UART2 | GPIO16 | GPIO15 |

Do **not** add `#ifndef` fallback guards for these values anywhere in `.cpp` files — Kconfig always provides them in an ESP-IDF build.

---

## Step 4 — Register new source files in CMake

File: `firmware/main/CMakeLists.txt`

Add to `SRCS`:
```cmake
"src/sensors/drivers/mynewsensor_sensor.cpp"
```

---

## Step 5 — Implement the driver header

File: `firmware/main/include/air360/sensors/drivers/mynewsensor_sensor.hpp`

Implement `SensorDriver`. Required overrides:

```cpp
SensorType type() const override;
esp_err_t init(const SensorRecord& record, const SensorDriverContext& context) override;
esp_err_t poll() override;
SensorMeasurement latestMeasurement() const override;
std::string lastError() const override;
```

Also declare the factory function:
```cpp
std::unique_ptr<SensorDriver> createMyNewSensor();
```

Reference drivers by transport type:

| Transport | Reference |
|-----------|-----------|
| I2C | `bme280_sensor`, `ina219_sensor` |
| UART (library-managed) | `mhz19b_sensor` |
| UART (via UartPortManager) | `gps_nmea_sensor` |
| GPIO / 1-Wire | `dht_sensor`, `ds18b20_sensor` |
| Analog / ADC | `me3_no2_sensor` |

---

## Step 6 — Implement the driver

File: `firmware/main/src/sensors/drivers/mynewsensor_sensor.cpp`

Key rules:

- **I2C**: resolve bus pins via `context.i2c_bus_manager->resolvePins()` in `init()`.
- **UART via library**: call the library's `xxx_init()` directly — do not go through `UartPortManager` if the library manages UART internally (as with `mhz19b`).
- **UART via UartPortManager**: call `context.uart_port_manager->open()` in `init()` and `close()` in `reset()`.
- **Warmup / not-ready states**: return `ESP_OK` with `last_error_` set to a short message — do not return an error code. Do not store a measurement during warmup.
- **Sentinel values**: if the device returns a known out-of-range sentinel on failure (e.g. MH-Z19B returns `5000` when not ready), detect and discard it — set `last_error_` and return `ESP_OK` without updating `measurement_`.
- **On read error**: set `initialized_ = false` so the next poll cycle re-initialises the device.
- **Type safety**: check the library header for exact parameter types (`int16_t` vs `uint16_t`, etc.) before writing casts.

---

## Step 7 — Register the sensor

File: `firmware/main/src/sensors/sensor_registry.cpp`

**a) Add the include:**
```cpp
#include "air360/sensors/drivers/mynewsensor_sensor.hpp"
```

**b) Add a validate function** before `kDescriptors[]`. Follow the pattern of the nearest transport type. Validate transport kind, I2C address range, UART baud rate, or GPIO pin as appropriate.

**c) Add a descriptor entry** to `kDescriptors[]`:

```cpp
{
    SensorType::kMyNewSensor,
    "my_new_sensor",          // type_key — must match backend and uploader
    "My New Sensor",          // display_name shown in UI
    false,                    // supports_i2c
    false,                    // supports_analog
    true,                     // supports_uart
    false,                    // supports_gpio
    true,                     // driver_implemented
    10000U,                   // default_poll_interval_ms
    0U,                       // default_i2c_bus_id
    0x00U,                    // default_i2c_address
    CONFIG_AIR360_MYNEWSENSOR_DEFAULT_UART_PORT,
    CONFIG_AIR360_MYNEWSENSOR_DEFAULT_RX_GPIO,
    CONFIG_AIR360_MYNEWSENSOR_DEFAULT_TX_GPIO,
    9600U,                    // default_uart_baud_rate
    &validateMyNewSensorRecord,
    &createMyNewSensor,
},
```

---

## Step 8 — Wire up the web UI

File: `firmware/main/src/web_server.cpp`

**a) Add to the category array.** Find the relevant category array (`kClimateSensorTypes`, `kGasSensorTypes`, `kParticulateMatterSensorTypes`, `kLightSensorTypes`, `kLocationSensorTypes`, `kPowerSensorTypes`) and append:
```cpp
SensorType::kMyNewSensor,
```

**b) Add to `sensorCategoryForType()`:**
```cpp
case SensorType::kMyNewSensor:
    return SensorCategory::kGas;  // or whichever category
```

**c) Add to `sensorDefaultsHint()`:**
```cpp
case SensorType::kMyNewSensor:
    return "Defaults: UART 2 RX16 TX15 @ 9600 baud.";
```
For UART sensors with Kconfig constants, build the string dynamically as GPS and MH-Z19B do.

---

## Step 9 — Wire up the backend uploader

File: `firmware/main/src/uploads/adapters/air360_api_uploader.cpp`

Add a case to `sensorTypeKey()`:
```cpp
case SensorType::kMyNewSensor:
    return "my_new_sensor";
```

The string must exactly match the `type_key` in the registry descriptor.

---

## Step 10 — Update backend contracts

**`backend/src/contracts/sensor-type.ts`** — add the type string:
```typescript
"my_new_sensor",
```

**`backend/src/contracts/measurement-kind.ts`** — add any new `SensorValueKind` keys if the sensor introduces measurement kinds not yet in the list. The key string comes from `sensorValueKindKey()` in `sensor_types.hpp`.

---

## Step 11 — Build and verify

```bash
source ~/.espressif/v6.0/esp-idf/export.sh && idf.py build
```

Run from `firmware/`. Fix all errors before proceeding to documentation.

---

## Step 12 — Create the per-driver doc

File: `docs/firmware/sensors/<sensor>.md`

Use [ina219.md](ina219.md) (I2C) or [mhz19b.md](mhz19b.md) (UART) as a template. Required sections:

- Status
- Scope
- Source of truth in code
- Read next
- Transport (binding, address / port, clock / baud)
- Hardware (wiring diagram if non-obvious)
- Initialization (step-by-step)
- Polling (what happens each cycle, including warmup / not-ready handling)
- Measurements table (`ValueKind` → unit)
- Notes (gotchas, power requirements, known limitations)
- Recommended poll interval
- Component (if a managed component is used)

---

## Step 13 — Update documentation

All six locations are required:

| File | What to add |
|------|-------------|
| `docs/firmware/sensors/README.md` | Row in **Sensor Index** table + `### MySensor` block in **Sensor Hardware Reference** |
| `docs/firmware/sensors/supported-sensors.md` | Row in the support matrix |
| `docs/firmware/README.md` | Row in the sensor drivers table |
| `docs/firmware/nvs.md` | Row in the `SensorType` enum values table |
| `docs/firmware/transport-binding.md` | Row in the UART assignments table (UART sensors only) |
| `docs/firmware/configuration-reference.md` | Row in **Per-sensor constraints** |
| `docs/firmware/user-guide.md` | Row in the sensor category table + row in the wiring table |

---

## Design constraints

- Never reuse a retired `SensorType` enum value.
- Do not add `#ifndef CONFIG_AIR360_*` fallback guards in `.cpp` files — Kconfig always provides these values.
- Do not hard-code board pins inside the driver; use `SensorRecord` fields populated from Kconfig defaults.
- If a managed component manages UART internally, do not also go through `UartPortManager` — pick one owner.
- Keep `type_key` identical across: registry descriptor, `sensorTypeKey()`, and `backend/src/contracts/sensor-type.ts`.
