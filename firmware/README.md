# air360 Phase 1 Firmware

This directory contains the Phase 1 ESP-IDF firmware foundation described in `docs/firmware-iterative-implementation-plan.md`.

## Scope

- ESP-IDF CMake project for one ESP32 target
- C++17 application skeleton with explicit modules
- NVS-backed config repository
- minimal HTTP server with `/` and `/status`
- lab-only SoftAP hook for local bring-up before the real onboarding flow

## Intended target

- `ESP32-S3-DevKitC-1`
- ESP-IDF 6.x

## Development environment

Recommended setup on macOS:

1. Install ESP-IDF by following the official Espressif macOS guide:
   https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/macos-setup.html
2. Install the ESP-IDF extension in VS Code.
3. In VS Code, point the extension to your local ESP-IDF installation.
4. Open this repository and work from `firmware/`.

First-time initialization for this repository:

1. Open the repository in VS Code.
2. Open the `firmware/` project.
3. In the ESP-IDF extension, select target `esp32s3`.
4. Let the extension create or update `sdkconfig` for this target.

If you already had this project configured for plain `esp32`, reset the local target-specific files first:

```bash
cd firmware
rm -rf build sdkconfig
```

Then reopen the project in VS Code and select target `esp32s3` again in the extension.

## Build and deploy

The usual workflow is to build from VS Code through the ESP-IDF extension. The extension configures the project through CMake and uses the local `sdkconfig`.

Minimal flow:

1. Make sure the target is set to `esp32s3` in the ESP-IDF extension.
2. Start the build from VS Code.
3. Flash and monitor from VS Code.

If you need the terminal equivalent of the VS Code build:

```bash
cd firmware
cmake -G Ninja -DPYTHON_DEPS_CHECKED=1 -DESP_PLATFORM=1 -B build -S . -DSDKCONFIG="$PWD/sdkconfig"
cmake --build build
```

Find the serial port on macOS before flashing:

```bash
ls /dev/cu.*
```

Phase 1 should expose:

- `/`
- `/status`
