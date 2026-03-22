# Repository Guidelines

## Project Structure & Module Organization

The repository now has two active areas. `docs/` contains architecture, compatibility, and planning material for the replacement firmware. `firmware/` is the Phase 1 ESP-IDF application. Core runtime headers live in `firmware/main/include/air360/`; implementations live in `firmware/main/src/`. Keep new firmware modules small and single-purpose, and place project-level build files such as `sdkconfig.defaults` and `partitions.csv` at the `firmware/` root.

## Build, Test, and Development Commands

Run all firmware work from `firmware/`. The active local workflow uses the VS Code ESP-IDF extension for target selection, build, flash, and monitor. Use direct CMake/Ninja commands only as the terminal equivalent for local verification.

- In the VS Code ESP-IDF extension, select target `esp32s3` for this project and let the extension manage `sdkconfig`.
- `cmake -G Ninja -DPYTHON_DEPS_CHECKED=1 -DESP_PLATFORM=1 -B build -S . -DSDKCONFIG="$PWD/sdkconfig"` configures the firmware build. This matches the VS Code workflow.
- `cmake --build build` compiles the firmware.
- Use the VS Code ESP-IDF extension for flashing the board and opening the serial monitor.
- `ls /dev/cu.*` helps find the serial port on macOS before flashing.
- `git diff --check` catches whitespace and merge-marker issues before commit.
- `rg --files docs firmware` is the fastest way to inspect tracked source and docs files.

Do not claim that ESP-IDF build dependencies are unavailable unless you verified it in the current shell by running the CMake configure step or by checking that the VS Code ESP-IDF extension is not configured for the local installation.

## Coding Style & Naming Conventions

Use C++17 for firmware code and keep interfaces explicit. Match the current layout: `.hpp` for public headers, `.cpp` for implementations, and lowercase kebab-case for Markdown filenames. Prefer short classes around one concern, for example `ConfigRepository` or `WebServer`, rather than growing `app.cpp` into a monolith. Use 4-space indentation, avoid exceptions and RTTI, and keep logging tags scoped per module.

## Testing Guidelines

At minimum, every firmware change should pass the CMake configure and build steps: `cmake -G Ninja -DPYTHON_DEPS_CHECKED=1 -DESP_PLATFORM=1 -B build -S . -DSDKCONFIG="$PWD/sdkconfig"` and `cmake --build build`. For hardware-facing changes, verify boot logs and HTTP behavior through the VS Code ESP-IDF extension; Phase 1 should expose `/` and `/status` on the device. For docs-only changes, check Markdown rendering and cross-document consistency. Add host-side unit tests when introducing pure logic that does not require ESP32 hardware.

## Commit & Pull Request Guidelines

History currently uses short imperative subjects such as `Initial commit`; keep that style and stay under roughly 72 characters. Pull requests should state whether they affect `docs/`, `firmware/`, or both, summarize hardware impact, list validation performed, and include serial logs or screenshots when behavior changes are user-visible.

## Configuration & Environment Notes

Do not commit `firmware/build/`, generated `sdkconfig`, or local machine paths. Treat `sdkconfig.defaults` as the committed baseline and document any new Kconfig options in the same change that introduces them.
