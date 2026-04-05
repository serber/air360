# Air360 Firmware Architecture

## Purpose

Describe the firmware purpose and the system problem it solves.

Include:

- what the device is expected to do
- main operating scenario
- major external dependencies such as backend, UI, sensors, or local peripherals

---

## Repository context

This architecture document describes the firmware implementation located in `firmware/`.

Related design and planning context may exist in `../docs/`, but current implementation details must be verified against the firmware source tree.

---

## Firmware project layout

```text
firmware/
├── CMakeLists.txt
├── main/
│   ├── CMakeLists.txt
│   ├── Kconfig.projbuild
│   ├── include/
│   └── src/
├── partitions.csv
├── sdkconfig
└── sdkconfig.defaults
```

Explain which parts are most important for implementation understanding.

---

## Startup sequence

Document the startup flow in the order it actually happens.

Suggested structure:

1. boot and ESP-IDF runtime initialization
2. `app_main`
3. configuration initialization
4. hardware/peripheral initialization
5. storage initialization
6. network initialization
7. application services startup
8. runtime loop or task scheduling

Only include steps confirmed by the source code.

---

## Runtime model

Describe how the firmware runs after startup.

Suggested questions:

- Is the firmware mostly event-driven or task-driven?
- Are there dedicated FreeRTOS tasks?
- Are timers, callbacks, or queues used?
- Which modules are long-lived services?
- Which modules are initialized once and then remain passive?

---

## Components and responsibilities

Document each major module.

Suggested format:

### `<Component name>`

**Responsibility**  
What the component is responsible for.

**Key files**  
Relevant headers and source files.

**Inputs**  
What data, events, or configuration it consumes.

**Outputs**  
What it produces, publishes, stores, or transmits.

**Dependencies**  
Other modules, drivers, or services it relies on.

**Concurrency model**  
Whether it runs in startup code, a task, a callback, ISR-adjacent logic, or synchronous flow.

---

## Configuration model

Describe how behavior is configured.

Suggested sources to inspect:

- `sdkconfig.defaults`
- `sdkconfig`
- `main/Kconfig.projbuild`

Document:

- project-defined config options
- important feature toggles
- logging-related configuration
- environment-specific settings
- config values that affect startup or hardware behavior

---

## Storage and partitions

Explain how storage is organized.

Suggested topics:

- partition table overview
- NVS usage
- OTA-related partitions if present
- data persistence strategy
- any assumptions visible in code

Do not assume OTA logic exists only because multiple app partitions exist.

---

## Hardware integration

Document actual hardware-related responsibilities visible in the code.

Suggested topics:

- board or target assumptions
- GPIO ownership
- buses such as I2C, SPI, UART
- sensors, displays, radios, or other peripherals
- boot-sensitive pin considerations

Do not invent pin mappings or devices.

---

## External interfaces

Describe how the firmware interacts with the outside world.

Examples:

- backend/server communication
- local UI
- BLE or Wi-Fi interfaces
- serial debug interface
- data upload contract

Use implementation evidence first and `docs/` as supporting context only.

---

## Logging and error handling

Document:

- main logging strategy
- key log tags if useful
- startup failure handling
- retry or recovery behavior
- fail-fast versus degraded-mode behavior

Only describe mechanisms visible in the code or confirmed by docs.

---

## Implemented vs planned

This section is important for this repository.

Use it to distinguish:

### Confirmed in implementation

List behavior directly supported by the source tree.

### Inferred from structure

List behavior that is likely but not fully explicit.

### Planned or described in docs

List architecture items that appear in `docs/` but are not yet clearly implemented.

---

## Open questions

List uncertainties that should be verified in code, hardware notes, or future work.

Examples:

- exact hardware revision assumptions
- incomplete module responsibilities
- unclear config interactions
- roadmap items not yet reflected in source
