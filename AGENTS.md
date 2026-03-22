# Repository Guidelines

## Project Structure & Module Organization

The repository now has two active areas. `docs/` contains architecture, compatibility, and planning material for the replacement firmware. `firmware/` is the Phase 1 ESP-IDF application. Core runtime headers live in `firmware/main/include/air360/`; implementations live in `firmware/main/src/`. Keep new firmware modules small and single-purpose, and place project-level build files such as `sdkconfig.defaults` and `partitions.csv` at the `firmware/` root.

## Build, Test, and Development Commands

Run all firmware commands from `firmware/`. The active local workflow uses the ESP-IDF environment plus direct CMake/Ninja builds, including from VS Code. If `idf.py` is already available in the current shell, use it directly. Otherwise source the local ESP-IDF export script from the path used during installation before running configure, build, or flash commands:

- `. /path/to/esp-idf/export.sh` loads `idf.py`, toolchains, and Python deps into the current shell. A common default is `. $HOME/esp/esp-idf/export.sh`, but do not assume that path exists on every machine.
- `idf.py --version` verifies that the ESP-IDF environment is active in the current shell.
- `idf.py set-target esp32s3` selects the Phase 1 target and regenerates `sdkconfig` for this SoC. Run this once for a new build directory or after removing `sdkconfig`.
- `cmake -G Ninja -DPYTHON_DEPS_CHECKED=1 -DESP_PLATFORM=1 -B build -S . -DSDKCONFIG="$PWD/sdkconfig"` configures the firmware build. This matches the VS Code workflow.
- `cmake --build build` compiles the firmware.
- `idf.py -p /dev/PORT flash` writes the firmware to the board.
- `idf.py -p /dev/PORT monitor` opens the serial monitor.
- `idf.py -p /dev/PORT flash monitor` flashes the board and immediately opens the serial monitor.
- `ls /dev/cu.*` helps find the serial port on macOS before flashing.
- `git diff --check` catches whitespace and merge-marker issues before commit.
- `rg --files docs firmware` is the fastest way to inspect tracked source and docs files.

Do not claim that ESP-IDF build dependencies are unavailable unless you verified it in the current shell, for example with `idf.py --version`, by running the CMake configure step, or by confirming that the configured `export.sh` path is missing.

## Coding Style & Naming Conventions

Use C++17 for firmware code and keep interfaces explicit. Match the current layout: `.hpp` for public headers, `.cpp` for implementations, and lowercase kebab-case for Markdown filenames. Prefer short classes around one concern, for example `ConfigRepository` or `WebServer`, rather than growing `app.cpp` into a monolith. Use 4-space indentation, avoid exceptions and RTTI, and keep logging tags scoped per module.

## Testing Guidelines

At minimum, every firmware change should pass the CMake configure and build steps: `cmake -G Ninja -DPYTHON_DEPS_CHECKED=1 -DESP_PLATFORM=1 -B build -S . -DSDKCONFIG="$PWD/sdkconfig"` and `cmake --build build`. For hardware-facing changes, verify boot logs and HTTP behavior with `idf.py -p /dev/PORT flash monitor`; Phase 1 should expose `/` and `/status` on the device. For docs-only changes, check Markdown rendering and cross-document consistency. Add host-side unit tests when introducing pure logic that does not require ESP32 hardware.

## Commit & Pull Request Guidelines

History currently uses short imperative subjects such as `Initial commit`; keep that style and stay under roughly 72 characters. Pull requests should state whether they affect `docs/`, `firmware/`, or both, summarize hardware impact, list validation performed, and include serial logs or screenshots when behavior changes are user-visible.

## Configuration & Environment Notes

Do not commit `firmware/build/`, generated `sdkconfig`, or local machine paths. Treat `sdkconfig.defaults` as the committed baseline and document any new Kconfig options in the same change that introduces them.
