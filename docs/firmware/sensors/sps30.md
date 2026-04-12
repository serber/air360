# SPS30

Laser particulate matter (PM) sensor from Sensirion. Measures mass and number concentrations across multiple size fractions, plus typical particle size.

## Transport

- I2C, bus 0 (SDA=GPIO8, SCL=GPIO9)
- Address: `0x69` (fixed)

## Initialization

1. Verify I2C bus availability via `I2cBusManager`
2. Probe the I2C address via `i2c_bus_manager->probe()`
3. Set up the HAL context via `prepareContext()` — configures the global bus pointer used by the Sensirion HAL
4. Initialize the Sensirion I2C HAL via `sensirion_i2c_hal_init()`
5. Initialize the SPS30 library via `sps30_init()`
6. Start continuous measurement via `sps30_start_measurement(SPS30_OUTPUT_FORMAT_OUTPUT_FORMAT_FLOAT)`
7. If measurement start fails: execute `sps30_wake_up_sequence()` and retry

## Polling

Each poll cycle:
1. Refresh the HAL context via `prepareContext()` — required before every Sensirion library call
2. Read 10 float values via `sps30_read_measurement_values_float()`

## Measurements

| Measurement | ValueKind | Unit |
|-------------|-----------|------|
| PM1.0 mass concentration | `kPm1_0UgM3` | µg/m³ |
| PM2.5 mass concentration | `kPm2_5UgM3` | µg/m³ |
| PM4.0 mass concentration | `kPm4_0UgM3` | µg/m³ |
| PM10.0 mass concentration | `kPm10_0UgM3` | µg/m³ |
| NC0.5 number concentration | `kNc0_5PerCm3` | #/cm³ |
| NC1.0 number concentration | `kNc1_0PerCm3` | #/cm³ |
| NC2.5 number concentration | `kNc2_5PerCm3` | #/cm³ |
| NC4.0 number concentration | `kNc4_0PerCm3` | #/cm³ |
| NC10.0 number concentration | `kNc10_0PerCm3` | #/cm³ |
| Typical particle size | `kTypicalParticleSizeUm` | µm |

## Notes

- The sensor runs in **continuous measurement mode** — it generates data autonomously. The driver only reads the latest available values on each poll.
- `prepareContext()` must be called before every Sensirion library operation because the HAL uses global state. This is necessary when the I2C bus may be shared with other components between polls.
- **Wake-up sequence** — if the first measurement start fails (sensor may be in sleep mode after a reset), the driver automatically executes a wake-up sequence and retries. This is handled only during initialization.
- Sensirion error codes are mapped to ESP error codes: `CRC_ERROR` → `ESP_ERR_INVALID_RESPONSE`, `I2C_BUS_ERROR` → `ESP_FAIL`.
- Output format is hardcoded to float.

## HAL adapter

SPS30 communicates through the Sensirion vendor I2C HAL, adapted in `sensirion_i2c_hal.cpp`. The HAL translates `sensirion_i2c_hal_read/write` calls into `I2cBusManager::readRaw/writeRaw`. Delays use `vTaskDelay` for ≥ 1 ms and `esp_rom_delay_us` for < 1 ms. The `getComponentBus()` method is used to obtain the legacy `i2c_bus_handle_t` required by the Sensirion HAL — see [transport-binding.md](../transport-binding.md) for details.

## Recommended poll interval

Minimum 5 seconds. The SPS30 updates its data every second in continuous mode — more frequent polling is possible but constrained to 5 s by the registry.

## Component

Vendored: `firmware/main/third_party/sps30/`

## Source files

- `firmware/main/src/sensors/drivers/sps30_sensor.cpp`
- `firmware/main/src/sensors/drivers/sensirion_i2c_hal.cpp`
- `firmware/main/include/air360/sensors/drivers/sps30_sensor.hpp`
