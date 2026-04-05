# Platform Selection Notes

Decision-oriented notes for selecting a hardware baseline for the current Air360 firmware.

This file is intentionally partly planning-oriented. It uses the current `firmware/` tree as implementation evidence, then compares how well that implementation fits `ESP32-S3`, `ESP32-C3`, `ESP32-C6`, and `ESP8266`.

It is not a statement that multiple targets are implemented today. The current firmware remains an `esp32s3` project unless the `firmware/` source tree changes.

## Current Firmware Baseline

The current firmware is explicitly built around `esp32s3` on ESP-IDF 6.x.

Implementation evidence from `firmware/`:

- [`../../firmware/README.md`](../../firmware/README.md)
  The operational firmware README describes the current implementation as an `esp32s3` runtime on ESP-IDF 6.x.
- [`../../firmware/sdkconfig`](../../firmware/sdkconfig)
  The current local build config selects `CONFIG_IDF_TARGET="esp32s3"`.
- [`../../firmware/sdkconfig.defaults`](../../firmware/sdkconfig.defaults)
  The repository defaults assume a 16 MB flash device and board-level defaults that match an ESP32-S3 class board.
- [`../../firmware/main/src/app.cpp`](../../firmware/main/src/app.cpp)
  The boot path initializes `esp_netif`, the default event loop, NVS, and board-specific boot LEDs.
- [`../../firmware/main/src/network_manager.cpp`](../../firmware/main/src/network_manager.cpp)
  The runtime uses `esp_netif`, default Wi-Fi station and AP netifs, and `esp_netif_sntp`.
- [`../../firmware/main/include/air360/sensors/transport_binding.hpp`](../../firmware/main/include/air360/sensors/transport_binding.hpp)
  The transport layer includes the modern `driver/i2c_master.h` API.
- [`../../firmware/main/src/sensors/drivers/me3_no2_sensor.cpp`](../../firmware/main/src/sensors/drivers/me3_no2_sensor.cpp)
  The analog driver uses `adc_oneshot` and ADC calibration APIs.

Taken together, the current codebase is not a generic "Espressif firmware". It is a modern ESP-IDF application already shaped around the ESP32 family and around ESP32-S3 board assumptions.

## Why ESP8266 Is Not A Small Addition

Adding ESP8266 support would not be a normal secondary target addition. It would be a separate port.

### Different SDK generation

The current firmware targets ESP-IDF 6.x semantics. ESP8266 uses the separate `ESP8266_RTOS_SDK`, with different project assumptions and older API surfaces.

Official references:

- ESP32-S3 get started:
  https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/index.html
- ESP8266 RTOS SDK get started:
  https://docs.espressif.com/projects/esp8266-rtos-sdk/en/latest/get-started/index.html

### Network stack mismatch

The current firmware depends on `esp_netif` and the default event loop. The ESP8266 RTOS SDK documents `tcpip_adapter` rather than `esp_netif` as the TCP/IP integration layer.

Implementation evidence:

- [`../../firmware/main/src/app.cpp`](../../firmware/main/src/app.cpp)
- [`../../firmware/main/src/network_manager.cpp`](../../firmware/main/src/network_manager.cpp)

Official references:

- ESP-NETIF:
  https://docs.espressif.com/projects/esp-idf/en/v5.3/esp32s3/api-reference/network/esp_netif.html
- ESP8266 TCP/IP adapter:
  https://docs.espressif.com/projects/esp8266-rtos-sdk/en/latest/api-reference/tcpip/tcpip_adapter.html

### Peripheral API mismatch

The current sensor transport and analog drivers rely on modern ESP-IDF APIs:

- `driver/i2c_master.h`
- `adc_oneshot`
- ADC calibration helpers

ESP8266 documents a different set of peripheral APIs:

- legacy I2C driver APIs
- `adc_read()`-style ADC access
- limited UART model

Implementation evidence:

- [`../../firmware/main/include/air360/sensors/transport_binding.hpp`](../../firmware/main/include/air360/sensors/transport_binding.hpp)
- [`../../firmware/main/src/sensors/transport_binding.cpp`](../../firmware/main/src/sensors/transport_binding.cpp)
- [`../../firmware/main/src/sensors/drivers/me3_no2_sensor.cpp`](../../firmware/main/src/sensors/drivers/me3_no2_sensor.cpp)

Official references:

- ESP8266 I2C:
  https://docs.espressif.com/projects/esp8266-rtos-sdk/en/latest/api-reference/peripherals/i2c.html
- ESP8266 ADC:
  https://docs.espressif.com/projects/esp8266-rtos-sdk/en/latest/api-reference/peripherals/adc.html
- ESP8266 UART:
  https://docs.espressif.com/projects/esp8266-rtos-sdk/en/latest/api-reference/peripherals/uart.html

### Board-level assumptions do not transfer

The current firmware assumes ESP32-S3-class GPIO numbering and board wiring:

- boot LEDs on GPIO10 and GPIO11
- GPS defaults on GPIO43 and GPIO44
- Kconfig GPIO ranges up to 48
- default flash size set to 16 MB

Implementation evidence:

- [`../../firmware/main/src/app.cpp`](../../firmware/main/src/app.cpp)
- [`../../firmware/main/Kconfig.projbuild`](../../firmware/main/Kconfig.projbuild)
- [`../../firmware/sdkconfig.defaults`](../../firmware/sdkconfig.defaults)

For ESP8266 this is not a remap-only exercise. The hardware limits are different enough that the board abstraction, Kconfig defaults, and some supported sensor combinations would need to be redesigned.

### Resource pressure would likely force feature cuts

The current runtime uses:

- an HTTP server stack size of 10240 bytes
- a sensor manager task stack size of 6144 bytes
- an upload manager task stack size of 7168 bytes
- a large amount of `std::string` and `std::vector` usage in the web, sensor, and upload layers

Implementation evidence:

- [`../../firmware/main/src/web_server.cpp`](../../firmware/main/src/web_server.cpp)
- [`../../firmware/main/src/sensors/sensor_manager.cpp`](../../firmware/main/src/sensors/sensor_manager.cpp)
- [`../../firmware/main/src/uploads/upload_manager.cpp`](../../firmware/main/src/uploads/upload_manager.cpp)

This is an engineering inference from the current codebase: an ESP8266 port would likely require feature reductions, not just compatibility work.

## Why It Is Better To Stay On One ESP32 Board

For this firmware, standardizing on one ESP32 board is the lowest-risk and lowest-cost path.

### One platform keeps the codebase coherent

One board means:

- one SDK family
- one target configuration
- one pin map
- one flash layout
- one test matrix
- one recovery and manufacturing flow

This matters because the firmware already spans boot, Wi-Fi, HTTP, local UI, NVS persistence, sensor polling, and backend uploads. Duplicating platform logic across chips would multiply validation work.

### The current implementation already matches ESP32 well

The present architecture uses exactly the capabilities that a modern ESP32 target gives you:

- native ESP-IDF networking through `esp_netif`
- current HTTP server integration
- modern ADC oneshot and calibration APIs
- modern I2C controller APIs
- enough GPIO flexibility for mixed sensor configurations
- enough runtime headroom for the local web UI and background tasks

In other words, staying on ESP32 preserves the architecture you already have instead of forcing a downgrade.

### Standardizing on one board reduces operational risk

Using one board family reduces:

- board-specific bugs
- "works on one target only" regressions
- documentation drift
- manufacturing and support confusion
- time spent chasing peripheral differences instead of adding product features

For a sensor hub firmware, that simplification is usually worth more than modest BOM savings on the MCU.

## Platform Comparison For Air360

This section compares the four most relevant options for the current codebase.

## ESP32-S3

Best fit for the current firmware.

Why it fits:

- it is already the implemented target
- it supports the modern ESP-IDF APIs used by the current code
- it provides strong GPIO headroom for board routing and sensor expansion
- it provides three UART controllers, which is useful for console plus GPS plus future serial sensors
- it provides two I2C controllers
- it provides ADC support aligned with the current `adc_oneshot`-based driver model
- it has USB OTG and USB Serial/JTAG, which are useful for development and manufacturing

Official references:

- ESP32-S3 overview:
  https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/index.html
- ESP32-S3 GPIO summary:
  https://docs.espressif.com/projects/esp-idf/en/v5.2.1/esp32s3/api-reference/peripherals/gpio.html
- ESP32-S3 UART:
  https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/uart.html
- ESP32-S3 I2C:
  https://docs.espressif.com/projects/esp-idf/en/v4.4.4/esp32s3/api-reference/peripherals/i2c.html

Air360-specific conclusion:

- lowest migration risk
- no architectural downgrade
- best board flexibility
- best option when future sensor growth matters

## ESP32-C3

Reasonable cost-down alternative if the goal is still to stay within the modern ESP32 family.

Why it is interesting:

- it still uses modern ESP-IDF APIs
- it supports `esp_netif`, `esp_http_server`, ADC oneshot, and current-style UART/I2C drivers
- it is much closer to the current firmware model than ESP8266 is

Official references:

- ESP32-C3 overview:
  https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/get-started/index.html
- ESP32-C3 API reference:
  https://docs.espressif.com/projects/esp-idf/en/v5.1.2/esp32c3/api-reference/index.html
- ESP32-C3 GPIO summary:
  https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-reference/peripherals/gpio.html
- ESP32-C3 UART:
  https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-reference/peripherals/uart.html
- ESP32-C3 I2C:
  https://docs.espressif.com/projects/esp-idf/en/v5.2/esp32c3/api-reference/peripherals/i2c.html
- ESP32-C3 ADC:
  https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/api-reference/peripherals/adc/index.html

What would still need work:

- the current board pin defaults do not fit C3 directly
- ESP32-C3 has fewer GPIOs than ESP32-S3
- ESP32-C3 has two UART controllers instead of three
- ESP32-C3 has one I2C controller instead of two
- the firmware should be reviewed for stack, heap, and concurrency headroom on a single-core target

Air360-specific conclusion:

- a plausible secondary option if board cost matters
- much easier than ESP8266 because the software model remains modern ESP-IDF
- still a real port, but a manageable one

## ESP32-C6

Reasonable alternative if the project wants to stay on a modern ESP32 platform and may later benefit from `802.15.4`, `Thread`, or `Zigbee`.

Why it is interesting:

- it remains inside the modern ESP-IDF family used by the current firmware
- it supports the same class of software building blocks the current runtime already depends on
- it adds radios and protocol options that the current S3 and C3 baseline do not provide in the same way

Official references:

- ESP32-C6 overview:
  https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/get-started/index.html
- ESP32-C6 API reference:
  https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-reference/index.html
- ESP32-C6 GPIO summary:
  https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/api-reference/peripherals/gpio.html
- ESP32-C6 UART:
  https://docs.espressif.com/projects/esp-idf/en/v5.2/esp32c6/api-reference/peripherals/uart.html
- ESP32-C6 I2C:
  https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-reference/peripherals/i2c.html
- ESP32-C6 ADC:
  https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/api-reference/peripherals/adc/index.html

What would still need work:

- the current board pin defaults do not fit C6 directly
- ESP32-C6 is a single-core target, so runtime headroom should be validated under Wi-Fi, HTTP, sensor polling, and uploads running together
- ESP32-C6 has two UART controllers rather than the current ESP32-S3-class headroom
- ESP32-C6 has one I2C controller rather than two
- the extra `802.15.4` capabilities add strategic value only if the product actually plans to use them

Air360-specific conclusion:

- technically viable for the current firmware architecture
- roughly in the same migration class as ESP32-C3, not in the ESP8266 class
- strongest choice if future `Thread` or `Zigbee` support matters
- weaker than ESP32-S3 as a default if the only goal is to preserve the current board flexibility

## ESP8266

Poor fit for the current firmware baseline.

Why it is weak for this project:

- separate SDK family
- older network integration model
- different peripheral APIs
- much tighter hardware limits
- incompatible pin assumptions
- higher probability of feature cuts in the local UI, sensor layer, or upload layer

Official references:

- ESP8266 RTOS SDK:
  https://docs.espressif.com/projects/esp8266-rtos-sdk/en/latest/get-started/index.html
- ESP8266 API index:
  https://docs.espressif.com/projects/esp8266-rtos-sdk/en/latest/api-reference/index.html
- ESP8266 TCP/IP adapter:
  https://docs.espressif.com/projects/esp8266-rtos-sdk/en/latest/api-reference/tcpip/tcpip_adapter.html
- ESP8266 UART:
  https://docs.espressif.com/projects/esp8266-rtos-sdk/en/latest/api-reference/peripherals/uart.html
- ESP8266 I2C:
  https://docs.espressif.com/projects/esp8266-rtos-sdk/en/latest/api-reference/peripherals/i2c.html
- ESP8266 ADC:
  https://docs.espressif.com/projects/esp8266-rtos-sdk/en/latest/api-reference/peripherals/adc.html

Air360-specific conclusion:

- not recommended as a target for the current firmware architecture
- only makes sense if Air360 is intentionally split into a reduced-function ESP8266 firmware line

## Practical Recommendation

If the goal is the fastest and safest path forward:

1. Standardize the current firmware on one ESP32-S3 board.
2. Treat that board as the reference hardware for firmware, docs, test, and manufacturing.
3. If a lower-cost variant is later needed, evaluate ESP32-C3 first.
4. If future `Thread`, `Zigbee`, or broader radio strategy matters, evaluate ESP32-C6 alongside ESP32-C3.
5. Keep ESP8266 out of scope unless Air360 intentionally becomes a reduced-function separate firmware line.

This recommendation follows directly from the current implementation:

- the code is already aligned with ESP32-S3
- ESP32-C3 is still inside the same modern software family
- ESP32-C6 is also inside the same modern software family, but its value depends more on future radio requirements than on immediate migration simplicity
- ESP8266 would introduce a second embedded platform strategy rather than a normal target variant

## Decision Summary

Short version:

- `ESP32-S3`: best default platform for Air360 now
- `ESP32-C3`: best fallback if a cheaper ESP32-family board is required later
- `ESP32-C6`: best fallback if future `Thread` or `Zigbee` support is strategically important
- `ESP8266`: not a good fit for the current firmware; only consider it for a deliberately reduced separate product line
