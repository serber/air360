# Transport Binding

## Status

Implemented. Keep this document aligned with the current transport abstractions and board-level pin defaults.

## Scope

This document covers the shared transport layer that sensor drivers use for I2C and UART access, including board-default pins and ownership boundaries.

## Source of truth in code

- `firmware/main/src/sensors/transport_binding.cpp`
- `firmware/main/include/air360/sensors/transport_binding.hpp`
- `firmware/main/include/air360/sensors/bus_config.hpp`
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

The bus list is a `constexpr BusConfig[]` array defined in the anonymous namespace of `transport_binding.cpp`. `init()` stores a `std::span` over it before calling `i2cdev_init()`. All `resolvePins()` calls search that span by `BusConfig::id`.

| Parameter | Value | Source |
|-----------|-------|--------|
| Bus | `I2C_NUM_0` | bus id 0 (`kPrimaryI2cBus`) |
| SDA pin | GPIO 8 | `CONFIG_AIR360_I2C0_SDA_GPIO` |
| SCL pin | GPIO 9 | `CONFIG_AIR360_I2C0_SCL_GPIO` |
| Default clock | 100 000 Hz | defined in `transport_binding.cpp` |
| Pull-ups | enabled | set in `setupDevice()` |

`kPrimaryI2cBus = 0U` is defined in `bus_config.hpp` and used by sensor descriptors instead of the literal `0` so that a future bus renumbering produces a compile-time error rather than a silent mismatch.

### `BusConfig`

```cpp
struct BusConfig {
    std::uint8_t  id;
    gpio_num_t    sda;
    gpio_num_t    scl;
    std::uint32_t clock_hz;
};
```

Adding a second bus requires only adding an entry to `kBuses[]` in `transport_binding.cpp` and a new symbolic ID in `bus_config.hpp`.

### Public API

| Method | Description |
|--------|-------------|
| `init()` | Stores the bus list span and calls `i2cdev_init()`. Idempotent — safe to call on every `applyConfig()`. Must be called before any driver's `init()`. |
| `resolvePins(bus_id, out_port, out_sda, out_scl)` | Searches the stored bus list for `bus_id`. Returns `false` if the id is not configured on this build. |
| `setupDevice(record, speed_hz, out_dev)` | Resolves pins for `record.i2c_bus_id`, fills all fields of `out_dev` (`port`, `addr`, `cfg`), and calls `i2c_dev_create_mutex()`. Used by drivers that manage a bare `i2c_dev_t` directly (SPS30, BME280). |
| `getComponentBus(bus_id, out_handle)` | Returns an `i2c_bus_handle_t` that borrows the bus already initialised by `i2cdev`. Internally calls `i2c_bus_create()`, which detects the existing bus handle via `i2c_master_get_bus_handle()` and avoids creating a second master. Used by BME280. |

### Bus ownership

`i2cdev` owns the I2C master bus. It creates the bus on the first `i2c_dev_create_mutex()` call and reference-counts usage per port — the bus is torn down only when the last `i2c_dev_delete_mutex()` drops the ref-count to zero.

`getComponentBus()` produces a _borrowed_ handle: the `espressif__i2c_bus` component's `i2c_bus_create()` detects that the port is already acquired and returns a handle without calling `i2c_new_master_bus()` again. Callers must **not** call `i2c_bus_delete()` on this handle — doing so would destroy `i2cdev`'s bus.

---

## `UartPortManager`

Manages UART ports for sensors that use serial communication. Current UART sensor assignments:

| Sensor | Port | RX | TX | Baud |
|--------|------|----|----|------|
| GPS (NMEA) | UART1 | GPIO18 | GPIO17 | 9600 |
| MH-Z19B | UART2 | GPIO16 | GPIO15 | 9600 |

### Port mapping

`port_id` maps directly to `uart_port_t`. Port 0 is rejected (`ESP_ERR_INVALID_ARG`) because it is the console UART. Any port in `[1, UART_NUM_MAX)` is accepted; the runtime rejects values outside that range. Kconfig defaults for GPS and MH-Z19B use `range 1 9` to match.

`ports_` is `std::array<PortState, UART_NUM_MAX>` indexed directly by `port_id` — slot 0 is always empty.

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
| `kPrimaryI2cBus` | `0U` | All I2C sensor descriptors |
| I2C default clock | 100 000 Hz | All I2C sensors |
| I2C pull-ups | enabled | All configured buses |
| UART RX buffer | 4 096 bytes | All UART ports |
| UART TX buffer | 0 (blocking) | All UART ports |
| Log tag | `air360.transport` | Both managers |
