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
- ESP-IDF 5.x

## Build and deploy

Run all commands from `firmware/`. The normal local workflow is CMake configure/build, including when VS Code triggers the build.

If `idf.py` is not already available in your shell, first activate the ESP-IDF environment from the path used during installation:

```bash
. /path/to/esp-idf/export.sh
```

Common default install path:

```bash
. $HOME/esp/esp-idf/export.sh
```

Optional sanity check:

```bash
idf.py --version
```

Initialize or change the target before the first build, or after removing `sdkconfig`:

```bash
idf.py set-target esp32s3
```

If you previously configured the project for plain `esp32`, clear the old target-specific state first:

```bash
rm -rf build sdkconfig
idf.py set-target esp32s3
```

Configure the build. This is the same shape of command that VS Code runs:

```bash
cmake -G Ninja -DPYTHON_DEPS_CHECKED=1 -DESP_PLATFORM=1 -B build -S . -DSDKCONFIG="$PWD/sdkconfig"
```

Build the firmware:

```bash
cmake --build build
```

Find the serial port on macOS before flashing:

```bash
ls /dev/cu.*
```

Flash the board and open the serial monitor:

```bash
idf.py -p /dev/cu.usbserial-0001 flash monitor
```

If you use VS Code, its build task may show an equivalent configure command with absolute paths, for example `cmake ... -B /abs/path/to/build -S /abs/path/to/firmware -DSDKCONFIG='/abs/path/to/sdkconfig'`.

You can also run the steps separately:

```bash
idf.py -p /dev/cu.usbserial-0001 flash
idf.py -p /dev/cu.usbserial-0001 monitor
```

Phase 1 should expose:

- `/`
- `/status`
