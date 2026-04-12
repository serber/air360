# Transport Binding

This document covers `I2cBusManager` and `UartPortManager` — the two hardware transport abstractions shared across all sensor drivers. Both classes are owned by `SensorManager` and passed to each driver via `SensorDriverContext` at init time.

---

## `SensorDriverContext`

Every driver receives a `SensorDriverContext` when `init()` is called:

```cpp
struct SensorDriverContext {
    I2cBusManager*  i2c_bus_manager  = nullptr;
    UartPortManager* uart_port_manager = nullptr;
};
```

Drivers access hardware only through these pointers. Direct ESP-IDF I2C or UART calls are not made from driver code.

---

## `I2cBusManager`

Manages the ESP32-S3's I2C master buses. The current board has one bus (bus 0). The implementation supports up to 2 buses (`std::array<BusState, 2>`), though only bus 0 is wired.

### Bus configuration

| Parameter | Value | Source |
|-----------|-------|--------|
| Bus | I2C_NUM_0 | `resolveBusPins(bus_id=0)` |
| SDA pin | GPIO 8 | `CONFIG_AIR360_I2C0_SDA_GPIO` |
| SCL pin | GPIO 9 | `CONFIG_AIR360_I2C0_SCL_GPIO` |
| Clock speed | 100 000 Hz | `kI2cClockHz` |
| Internal pullups | enabled | `flags.enable_internal_pullup = 1` |
| Glitch filter | 7 (cycles) | `glitch_ignore_cnt = 7` |
| Transfer timeout | 200 ms | `kI2cTransferTimeoutMs` |

The ESP-IDF new `i2c_master` driver API is used (`i2c_new_master_bus`, `i2c_master_bus_add_device`).

### Lazy initialisation

Neither the bus nor individual device handles are created at startup. `ensureBus()` initialises the bus on first use; `ensureDevice()` registers the device handle on first access. Once registered, both handles persist for the lifetime of the manager.

Each bus holds up to **8** registered device handles (`std::array<DeviceState, 8>`). If all slots are occupied and a new device is requested, `ESP_ERR_NO_MEM` is returned.

### Mutex

A static `StaticSemaphore_t` buffer backs a `SemaphoreHandle_t` mutex, created lazily on the first call. Every public method acquires the mutex before touching bus or device state. The lock is held for the full duration of each I2C transfer.

### Public API

| Method | Description |
|--------|-------------|
| `probe(bus_id, address)` | Sends an address probe (`i2c_master_probe`). Returns `ESP_OK` if a device ACKs at that address. Used by some drivers during `init()` to detect hardware presence. |
| `writeRegister(bus_id, address, reg, value)` | Writes a single register: transmits `[reg, value]`. |
| `write(bus_id, address, reg, buffer, size)` | Writes multiple bytes to a register: transmits `[reg, buf[0], ..., buf[n-1]]`. Max payload including register byte is 32 bytes (`sizeof(staging)`). |
| `writeRaw(bus_id, address, buffer, size)` | Transmits raw bytes with no register prefix. Used by drivers that compose their own command sequences (e.g., SPS30). |
| `readRaw(bus_id, address, buffer, size)` | Receives raw bytes (`i2c_master_receive`). |
| `readRegister(bus_id, address, reg, buffer, size)` | Write-then-read: transmits `[reg]`, then receives N bytes (`i2c_master_transmit_receive`). Standard register read for most sensors. |
| `getComponentBus(bus_id, out_handle)` | Returns an `i2c_bus_handle_t` for use with the legacy `i2c_bus` ESP-IDF component. Used by SPS30, which relies on the Sensirion I2C HAL adapter that requires this handle type. |
| `shutdown()` | Releases all device handles and deletes the master bus. Called from the destructor. |

### Write staging buffer

`write()` pre-assembles the register byte and payload into a 32-byte stack buffer before calling `i2c_master_transmit`. Callers must not exceed 31 bytes of payload (`buffer_size + 1 ≤ 32`); exceeding this returns `ESP_ERR_INVALID_SIZE`.

---

## `UartPortManager`

Manages UART ports for sensors that use serial communication. Currently only GPS (NMEA) uses UART transport.

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

### Console pin conflict warning

If the requested RX/TX pins match GPIO 44/43 (the default ESP32-S3 console UART pins) and the configured UART port is not the console port, a warning is logged:

```
UART1 is being mapped to GPIO 18/17, which overlap the default console pins;
serial logs may disappear after sensor init
```

This applies when GPS is initialised with non-default GPIO pins that happen to overlap the console.

### Public API

| Method | Description |
|--------|-------------|
| `open(port_id, rx_pin, tx_pin, baud_rate)` | Installs the UART driver and sets pins. Idempotent if called again with identical parameters. |
| `read(port_id, buffer, size, timeout_ticks)` | Reads up to `size` bytes from the RX ring buffer. Blocks up to `timeout_ticks`. Returns byte count or `-1` on error. |
| `flush(port_id)` | Clears the RX ring buffer (`uart_flush_input`). Called by the GPS driver before starting a new parse cycle. |
| `shutdown()` | Deletes all UART drivers and resets all port states. Called from the destructor. |

---

## Lifetime

Both managers are owned as members of `SensorManager` (static allocation inside `App::run()`). They are created before any sensor is initialised and live for the duration of the application. `shutdown()` is called from their destructors, which run when `SensorManager` is destroyed — in practice, never during normal operation.

For the list of all sensor drivers that use these managers, see [sensors/README.md](sensors/README.md).

---

## Constants summary

| Constant | Value | Applies to |
|----------|-------|-----------|
| I2C clock | 100 000 Hz | All I2C sensors |
| I2C transfer timeout | 200 ms | All I2C operations |
| I2C internal pullup | enabled | Bus 0 |
| I2C max devices per bus | 8 | `DeviceState` array |
| I2C write staging buffer | 32 bytes | `write()` method |
| UART RX buffer | 4 096 bytes | All UART ports |
| UART TX buffer | 0 (blocking) | All UART ports |
| Log tag | `air360.transport` | Both managers |
