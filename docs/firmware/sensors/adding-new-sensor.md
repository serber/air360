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

## Step 3 — Add UART defaults (UART sensors only)

UART0 is reserved for the console. Sensor UART ports use the fixed map in `SensorRegistry`:

| Port | RX | TX |
|------|----|----|
| UART1 | GPIO18 | GPIO17 |
| UART2 | GPIO16 | GPIO15 |

Set the sensor's default UART and allowed UART list in its descriptor. Add a Kconfig value only for parameters that really remain build-time defaults, such as a sensor-specific baud rate.

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
- **On read error**: keep short glitches local to the driver. Increment a per-driver poll failure counter and set `initialized_ = false` only after `kSensorPollFailureReinitThreshold` consecutive poll failures so the manager can re-initialise with exponential backoff.
- **Type safety**: check the library header for exact parameter types (`int16_t` vs `uint16_t`, etc.) before writing casts.

---

## Step 7 — Register the sensor

File: `firmware/main/src/sensors/sensor_registry.cpp`

**a) Add the include:**
```cpp
#include "air360/sensors/drivers/mynewsensor_sensor.hpp"
```

**b) Add a validate function** before `kDescriptors[]`. Follow the pattern of the nearest transport type. Validate transport kind and sensor-specific constraints such as UART baud rate. I2C address validation and GPIO/analog pin validation are shared by `SensorRegistry::validateRecord()` and use the descriptor fields below.

**c) Add a descriptor entry** to `kDescriptors[]`:

```cpp
{
    .type                      = SensorType::kMyNewSensor,
    .type_key                  = "my_new_sensor",
    .display_name              = "My New Sensor",
    .supports_i2c              = false,
    .supports_analog           = false,
    .supports_uart             = true,
    .supports_gpio             = false,
    .driver_implemented        = true,
    .default_poll_interval_ms  = 10000U,
    .default_i2c_bus_id        = 0U,
    .default_i2c_address       = 0x00U,
    .allowed_i2c_addresses     = {},
    .allowed_i2c_address_count = 0U,
    .default_uart_port_id      = 2U,
    .allowed_uart_ports        = {1U, 2U},
    .allowed_uart_port_count   = 2U,
    .default_uart_rx_gpio_pin  = 16,
    .default_uart_tx_gpio_pin  = 15,
    .default_uart_baud_rate    = 9600U,
    .allowed_gpio_pins         = {},
    .allowed_gpio_pin_count    = 0U,
    .validate                  = &validateMyNewSensorRecord,
    .create_driver             = &createMyNewSensor,
},
```

For an I2C sensor, set `.default_i2c_address` to the address used when a new record is created and list every accepted address in `.allowed_i2c_addresses`, for example `{0x76U, 0x77U}` with `.allowed_i2c_address_count = 2U`.

For a UART sensor, set `.default_uart_port_id` to `1U` or `2U` and list every selectable port in `.allowed_uart_ports`. RX/TX pins must match the selected UART port binding: UART1 uses RX=`GPIO18`, TX=`GPIO17`; UART2 uses RX=`GPIO16`, TX=`GPIO15`.

For a GPIO or analog sensor, leave UART fields empty and list every selectable GPIO in `.allowed_gpio_pins`. There is no separate default GPIO field; the web UI and add route use the first allowed pin as the initial selection.

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

**c) Review `sensorDefaultsHint()` output.** I2C, UART, and GPIO/analog defaults are generated from descriptor fields. Add custom text only for a new transport or for a sensor-specific exception.

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
