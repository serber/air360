# Air360 Firmware Configuration

## Configuration Layers

The firmware uses four distinct configuration layers:

- compile-time project options in [`../../firmware/main/Kconfig.projbuild`](../../firmware/main/Kconfig.projbuild)
- repository defaults in [`../../firmware/sdkconfig.defaults`](../../firmware/sdkconfig.defaults)
- the generated effective build config in [`../../firmware/sdkconfig`](../../firmware/sdkconfig)
- persisted runtime state in NVS through `DeviceConfig` and `SensorConfigList`

The important distinction is that `sdkconfig` controls how the firmware image is built, while NVS controls what a device remembers across reboots.

## Project Kconfig Surface

The project-defined `CONFIG_AIR360_*` options currently cover four groups.

### Device and network defaults

- `CONFIG_AIR360_BOARD_NAME`
- `CONFIG_AIR360_DEVICE_NAME`
- `CONFIG_AIR360_HTTP_PORT`
- `CONFIG_AIR360_ENABLE_LAB_AP`
- `CONFIG_AIR360_LAB_AP_SSID`
- `CONFIG_AIR360_LAB_AP_PASSWORD`
- `CONFIG_AIR360_LAB_AP_CHANNEL`
- `CONFIG_AIR360_LAB_AP_MAX_CONNECTIONS`

These become build-time defaults for `DeviceConfig` and AP behavior.

### Board I2C wiring

- `CONFIG_AIR360_I2C0_SDA_GPIO`
- `CONFIG_AIR360_I2C0_SCL_GPIO`

These define the board-level mapping for I2C bus 0. The current defaults are GPIO8 for SDA and GPIO9 for SCL.

### Board UART defaults for GPS and SDS011

- `CONFIG_AIR360_GPS_DEFAULT_UART_PORT`
- `CONFIG_AIR360_GPS_DEFAULT_RX_GPIO`
- `CONFIG_AIR360_GPS_DEFAULT_TX_GPIO`
- `CONFIG_AIR360_GPS_DEFAULT_BAUD_RATE`
- `CONFIG_AIR360_SDS011_DEFAULT_UART_PORT`
- `CONFIG_AIR360_SDS011_DEFAULT_RX_GPIO`
- `CONFIG_AIR360_SDS011_DEFAULT_TX_GPIO`
- `CONFIG_AIR360_SDS011_DEFAULT_BAUD_RATE`

These define the fixed board wiring assumed for the two UART sensor paths. The current repository defaults are UART1, RX GPIO44, TX GPIO43, baud 9600 for both `GPS (NMEA)` and `SDS011`.

### Board GPIO sensor slots

- `CONFIG_AIR360_GPIO_SENSOR_PIN_0`
- `CONFIG_AIR360_GPIO_SENSOR_PIN_1`
- `CONFIG_AIR360_GPIO_SENSOR_PIN_2`

These define the shared board sensor pins used by GPIO-backed and analog-backed sensors. The current defaults are GPIO4, GPIO5, and GPIO6.

## Repository Defaults

[`../../firmware/sdkconfig.defaults`](../../firmware/sdkconfig.defaults) currently fixes these important defaults:

- flash size `4MB`
- custom partition table `partitions.csv`
- C++ exceptions disabled
- C++ RTTI disabled
- main task stack size `6144`
- board and sensor wiring defaults listed above

The main task stack override matters because the runtime holds several long-lived service objects during startup.

## Runtime Device Configuration

The device-level persisted config lives in [`DeviceConfig`](../../firmware/main/include/air360/config_repository.hpp).

Stored fields:

- `http_port`
- `lab_ap_enabled`
- `local_auth_enabled`
- `device_name`
- `wifi_sta_ssid`
- `wifi_sta_password`
- `lab_ap_ssid`
- `lab_ap_password`

Storage details:

- NVS namespace: `air360`
- blob key: `device_cfg`
- boot counter key: `boot_count`

Important behavior:

- defaults come from compile-time `CONFIG_AIR360_*`
- once written, device config becomes runtime state in NVS
- changing `sdkconfig.defaults` does not retroactively rewrite an already provisioned device

## Runtime Sensor Configuration

The sensor inventory lives in [`SensorConfigList`](../../firmware/main/include/air360/sensors/sensor_config.hpp).

Stored fields per sensor record include:

- logical id
- enable flag
- sensor type
- transport kind
- poll interval
- display name
- I2C bus and address
- GPIO pin
- UART port, RX/TX pins, and baud rate

Storage details:

- NVS namespace: `air360`
- blob key: `sensor_cfg`

Current schema version:

- `SensorConfigList`: v2

Important current behavior:

- the persisted schema still stores transport-specific fields directly in `SensorRecord`
- the `/sensors` UI infers transport from sensor type rather than letting the user choose arbitrary transport combinations
- GPS and SDS011 records are validated against fixed board UART wiring from the registry defaults
- GPIO-backed and analog-backed sensors are constrained to the configured board sensor pins
- sensor edits in `/sensors` are staged in memory and only persisted when the user explicitly applies them and reboots
- stored sensor config with an older or incompatible layout is currently replaced with defaults rather than migrated

## Partition Table

The project uses a custom partition table in [`../../firmware/partitions.csv`](../../firmware/partitions.csv).

Current partitions:

- `nvs`
  Stores `device_cfg`, `sensor_cfg`, and `boot_count`.
- `otadata`
  Reserved for OTA metadata. OTA runtime logic is not implemented yet.
- `phy_init`
  Standard ESP-IDF PHY data.
- `factory`
  The current application slot.
- `storage`
  `spiffs` partition reserved for future file-backed storage.

The current runtime depends on NVS and does not mount or use the `storage` partition.

## Build And Flash Workflow

Typical terminal workflow:

```bash
cd firmware
. "$HOME/.espressif/v6.0/esp-idf/export.sh"
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/tty.usbserial-0001 flash monitor
```

Typical editor workflow:

- open [`../../firmware/`](../../firmware/) directly in VS Code
- or open [`../../firmware/firmware.code-workspace`](../../firmware/firmware.code-workspace)
- let the ESP-IDF extension manage the environment and target

## Practical Notes

- If CMake complains about `/tools/cmake/project.cmake`, `IDF_PATH` was not available when CMake parsed the project.
- If the editor cannot resolve ESP-IDF headers, make sure `build/compile_commands.json` exists from a successful configure/build.
- `sdkconfig` is a generated local file; treat `sdkconfig.defaults` and `Kconfig.projbuild` as the concise project-facing config surface.
