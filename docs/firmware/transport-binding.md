# Transport Binding

## Status

Implemented. Keep this document aligned with the current transport abstractions and board-level pin defaults.

## Scope

This document covers the shared transport layer that sensor drivers use for I2C and UART access, including board-default pins and ownership boundaries.

## Source of truth in code

- `firmware/main/src/sensors/transport_binding.cpp`
- `firmware/main/include/air360/sensors/transport_binding.hpp`
- `firmware/main/include/air360/sensors/sensor_driver.hpp`

## Read next

- [sensors/README.md](sensors/README.md)
- [sensors/adding-new-sensor.md](sensors/adding-new-sensor.md)
- [ARCHITECTURE.md](ARCHITECTURE.md)

This document covers `I2cBusManager` and `UartPortManager` — the two hardware transport abstractions shared across all sensor drivers. Both classes are owned by `SensorManager` and passed to each driver via `SensorDriverContext` at init time.

---

## `SensorDriverContext`

Every driver receives a `SensorDriverContext` when `init()` is called:

```cpp
struct SensorDriverContext {
    I2cBusManager*   i2c_bus_manager  = nullptr;
    UartPortManager* uart_port_manager = nullptr;
};
```

Drivers access hardware only through these pointers. Direct ESP-IDF I2C or UART calls are not made from driver code.

---

## `I2cBusManager`

A thin coordination layer that owns the `i2cdev` subsystem lifecycle and centralises I2C pin configuration. `i2cdev` (from `esp-idf-lib`) is the component that actually owns and manages the I2C master bus handles.

### Responsibilities

- Call `i2cdev_init()` once before any driver initialises.
- Map logical bus IDs to ESP-IDF port numbers and GPIO pin numbers — the single authoritative source for `CONFIG_AIR360_I2C0_SDA/SCL_GPIO`.
- Prepare `i2c_dev_t` descriptors for drivers that use `i2cdev` directly.
- Supply an `i2c_bus_handle_t` for drivers that require the `espressif__i2c_bus` component.

### Bus configuration

| Parameter | Value | Source |
|-----------|-------|--------|
| Bus | `I2C_NUM_0` | bus id 0 |
| SDA pin | GPIO 8 | `CONFIG_AIR360_I2C0_SDA_GPIO` |
| SCL pin | GPIO 9 | `CONFIG_AIR360_I2C0_SCL_GPIO` |
| Default clock | 100 000 Hz | `kDefaultI2cClockHz` |
| Pull-ups | enabled | set in `setupDevice()` |

Pin constants are defined once in `transport_binding.cpp`. No driver duplicates them.

### Public API

| Method | Description |
|--------|-------------|
| `init()` | Calls `i2cdev_init()`. Idempotent — safe to call on every `applyConfig()`. Must be called before any driver's `init()`. |
| `resolvePins(bus_id, out_port, out_sda, out_scl)` | Fills `out_port`, `out_sda`, and `out_scl` for a given logical bus id. Returns `false` if the id is unknown. Used by drivers whose underlying component library manages its own `i2c_dev_t` (e.g., VEML7700, SCD30, SHT4X). |
| `setupDevice(record, speed_hz, out_dev)` | Resolves pins for `record.i2c_bus_id`, fills all fields of `out_dev` (`port`, `addr`, `cfg`), and calls `i2c_dev_create_mutex()`. Used by drivers that manage a bare `i2c_dev_t` directly (SPS30, BME280). |
| `getComponentBus(bus_id, out_handle)` | Returns an `i2c_bus_handle_t` that borrows the bus already initialised by `i2cdev`. Internally calls `i2c_bus_create()`, which detects the existing bus handle via `i2c_master_get_bus_handle()` and avoids creating a second master. Used by BME280, which relies on the `espressif__bme280` component. |

### Bus ownership

`i2cdev` owns the I2C master bus. It creates the bus on the first `i2c_dev_create_mutex()` call and reference-counts usage per port — the bus is torn down only when the last `i2c_dev_delete_mutex()` drops the ref-count to zero.

`getComponentBus()` produces a _borrowed_ handle: the `espressif__i2c_bus` component's `i2c_bus_create()` detects that the port is already acquired and returns a handle without calling `i2c_new_master_bus()` again. Callers must **not** call `i2c_bus_delete()` on this handle — doing so would destroy `i2cdev`'s bus.

---

## `UartPortManager`

Manages UART ports for sensors that use serial communication. Current UART sensor assignments:

| Sensor | Port | RX | TX | Baud |
|--------|------|----|----|------|
| GPS (NMEA) | UART1 | GPIO18 | GPIO17 | 9600 |

### Port mapping

| `port_id` | ESP-IDF port |
|-----------|-------------|
| 1 | `UART_NUM_1` |
| 2 | `UART_NUM_2` |

Port 0 is rejected (`ESP_ERR_INVALID_ARG`). Port IDs map to the 1-based `uart_port_id` field in `SensorRecord`.

### Port configuration

Fixed for all UART sensors:

| Parameter | Value |
|-----------|-------|
| Data bits | 8 |
| Parity | None |
| Stop bits | 1 |
| Flow control | None |
| RX buffer size | 4 096 bytes |
| TX buffer size | 0 (blocking TX) |
| Clock source | `UART_SCLK_DEFAULT` |

Baud rate and pin assignment are taken from the `SensorRecord` fields (`uart_baud_rate`, `uart_rx_gpio_pin`, `uart_tx_gpio_pin`).

### Lazy initialisation

`open()` calls `ensurePort()` which installs the UART driver and configures pins on first call. If the port is already initialised with the **same** baud rate and pins, subsequent calls return `ESP_OK` immediately. If the port is already initialised but with **different** parameters, `ESP_ERR_INVALID_STATE` is returned — re-configuration is not supported.

### Public API

| Method | Description |
|--------|-------------|
| `open(port_id, rx_pin, tx_pin, baud_rate)` | Installs the UART driver and sets pins. Idempotent if called again with identical parameters. |
| `read(port_id, buffer, size, timeout_ticks)` | Reads up to `size` bytes from the RX ring buffer. Blocks up to `timeout_ticks`. Returns byte count or `-1` on error. |
| `flush(port_id)` | Clears the RX ring buffer (`uart_flush_input`). Called by the GPS driver before starting a new parse cycle. |
| `shutdown()` | Deletes all UART drivers and resets all port states. Called from the destructor. |

---

## Lifetime

Both managers are owned as members of `SensorManager`. `I2cBusManager::init()` is called in `SensorManager::applyConfig()` before any driver is constructed. Both managers live for the duration of the application. `UartPortManager::shutdown()` is called from its destructor and from `SensorManager::stop()`.

For the list of all sensor drivers that use these managers, see [sensors/README.md](sensors/README.md).

---

## Constants summary

| Constant | Value | Applies to |
|----------|-------|-----------|
| I2C default clock | 100 000 Hz | All I2C sensors |
| I2C pull-ups | enabled | Bus 0 |
| UART RX buffer | 4 096 bytes | All UART ports |
| UART TX buffer | 0 (blocking) | All UART ports |
| Log tag | `air360.transport` | Both managers |
