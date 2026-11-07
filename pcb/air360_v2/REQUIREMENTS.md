# Air360 V2 — Board Requirements and Workflow for Claude

Date: 2026-11-04. This document replaces the previous plans, reports, and conflicting interpretations of requirements in `pcb/air360_v2`. The user's latest explicit instructions take precedence. This is a design specification, not a declaration that the existing board is ready for manufacture.

## 1. Objective and scope

Create a standalone ESP32-S3-based Air360 board to replace the shield that carried an ESP32-S3 DevKit. The board must provide complete ESP32-S3-DevKitC-equivalent support circuitry and functionality, power external sensors and a modem, work with the existing Air360 firmware, and be prepared for manufacture.

Previous shield: https://oshwlab.com/serber2006/air360 . Repository: `/Users/albert/Documents/sources/air360`.

**Do not change sensor pin assignments to simplify routing.** Preserve GPIO assignments, connector functions and pin order, existing cable orientation, and firmware compatibility. Do not require firmware rewrites for the new board. Documented changes through existing configuration interfaces are acceptable; an undisclosed requirement to modify source code is not.

Required deliverables: a consistent schematic and PCB, BOM, fabrication and assembly files, connection documentation, and first-power-up instructions. Do not order fabrication or purchase parts without a separate instruction.

## 2. ESP32-S3 and DevKitC functionality

- Install the ESP32-S3 on the custom board itself; do not mount a separate DevKit on top.
- ESP32-S3-WROOM-1-N16R8 was previously selected. Use it as the starting point and change it only for a specific reason. Account for memory, available GPIOs, strapping pins, and module restrictions.
- Provide complete power and decoupling circuitry, startup, reset, BOOT, programming, debugging, and access to the intended GPIOs, using DevKitC as the functional reference.
- During schematic design, create an explicit DevKitC function mapping: power, EN/RESET, BOOT, native USB, UART, programming/automatic-reset circuitry, and GPIO access. The latest requirement for DevKitC-equivalent functionality takes precedence over the earlier idea of simply omitting a separate USB-to-UART bridge without assessing functionality.
- Native USB, BOOT/RESET buttons, and debug signals must work with the existing firmware and programming tools. Do not add a second USB connector or bridge without explaining its purpose in the function mapping.
- Follow the ESP32 module antenna requirements: placement, copper/component keepouts on the relevant layers, and clearances. The modem antenna is separate from the ESP32 antenna.
- Complete GPIO access does not mean exposing pins occupied by the selected module's internal memory. Document these restrictions separately.

## 3. Two power sources

### DC from a 2S battery

The DC input accepts a 2S battery pack; 3S support was dropped by the user on 2026-11-04 to simplify the board. A buck converter produces **regulated 5 V for the rest of the board**, sensors, and modem. Generate 3.3 V from this rail for the ESP32 and other applicable loads.

**Do not replace this requirement with DC=5 V, or redesign the main buck as a buck-boost converter based on that mistaken interpretation.**

Both 2S Li-ion (8.4 V fully charged at 4.2 V/cell) and 2S LiFePO4 (7.3 V fully charged at 3.65 V/cell) must be supported. Do not transfer thresholds between chemistries automatically. Although 3S is no longer a supported operating mode, component ratings must still survive a mistakenly connected 12.6 V pack plus justified tolerances/transients; UVLO is not expected to protect such a pack. Establish the lower operating thresholds during power design.

Provide reverse-polarity protection, appropriate connection/disconnection behavior, and adequate current and thermal capability throughout the power path, not just at the converter IC. Onboard battery charging and balancing have not been requested.

### 5 V at the DC input

The DC input must also operate from an ordinary 5 V phone charger (user requirement, 2026-11-04). This is an additional mode, not a replacement for the 2S requirement, and the main converter stays a buck. A 5 V charger and a deeply discharged pack look the same by voltage, so this mode must be selected explicitly on the board. A wrong selection must not put more than 5 V on the +5V rail.

### USB, 5 V only

The second way to power the intended assembly is USB supplied with **5 V only from an ordinary phone charger**. Do not require higher USB PD/QC voltages, a particular charger model, or charger modifications.

- USB-powered operation must not depend on DC/battery presence.
- Define correct behavior with USB and DC connected simultaneously. Prevent source backfeeding and invalid operating conditions for an unpowered converter.
- Account for cable/connector losses, startup load, sensor and modem currents, ripple, and transients.
- A source rated **at least 2 A** was discussed previously. This is an initial power-budget target, not proof of sufficient current and not an automatically approved hardware limit of 2 A. Calculate demand and state the required source rating, total board current, and current available at each output. A charger rating is not a hardware protection circuit.
- Define operation when connected to a computer USB port for programming separately. Do not equate a computer port's capabilities with those of a powerful charger or hide the limitations of this mode.

### Power budget

Before completing the schematic, tabulate continuous loads, simultaneous peaks, startup currents, conversion losses, margins, and connector/shunt/protection/trace/via limits. Evaluate the heaviest supported load configuration at the minimum operating battery voltage and under USB power. State connector voltage limits and allowable ripple.

Do not rate the whole assembly at “3 A” merely because the converter carries that rating. Do not promise an arbitrary number of loads without a total current limit.

## 4. Hardware protection against endless restart loops

The user's problem: as the battery discharges, the ESP32 starts booting and brings up peripherals, the supply sags, a reset occurs, and the sequence repeats indefinitely.

Provide hardware that inhibits or disconnects load power when input voltage is insufficient. Select the implementation during schematic design: a supervisor/comparator with hysteresis, a power switch, converter EN control with the required logic, or an equivalent solution.

Required behavior:

1. Do not repeatedly attempt to start the ESP32, sensors, and modem when voltage is insufficient.
2. Correctly disconnect the battery-powered load when voltage falls during operation.
3. Use different turn-off and restart conditions: `V_ON > V_OFF`, with calculated allowance for startup sag, tolerances, and unloaded battery voltage recovery.
4. Small hysteresis alone may be insufficient. Add delay, stable-voltage qualification, or a latched-off state where necessary. Verify that battery recovery after load removal does not trigger another start–sag–shutdown cycle.
5. Provide thresholds for 2S only; no 2S/3S profile selection is required. Select the input profile (Li-ion, LiFePO4, or 5 V charger) explicitly on the board, for example with a jumper; do not infer it from voltage. A wrong profile must not over-discharge the pack or damage the board.
6. The monitoring circuit must remain able to decide when to disconnect or restore power while the main load is off. Account for its own consumption.
7. A discharged or absent battery must not prevent normal operation from a healthy USB source. Prevent parasitic powering of disconnected loads through signal lines.
8. Air360's protection prevents unstable load operation; it does not replace necessary battery cell protection or balancing.

The repository already contains a firmware `power_gate`, but it does not implement complete hardware load disconnection: it runs at boot, uses ESP32 deep sleep, does not automatically remove power from all external loads, and includes a bypass after several attempts. Do not present that existing firmware mechanism as fulfillment of this requirement. Preserve compatibility with it and document settings appropriate to measuring the DC input voltage.

## 5. Integrated INA219

- Integrate the INA219 on Air360 to monitor **DC/battery input voltage before conversion to 5 V**.
- Its logic supply and I²C interface must be compatible with the ESP32. Specify the voltage measurement reference, VIN+/VIN− connections, and shunt location.
- The existing driver calculates current for a **0.1 Ω shunt**. Preserve this value for compatibility without firmware changes; select adequate power rating and accuracy, and use Kelvin connections. Do not silently change shunt resistance while retaining the previous calibration.
- The default I²C address is `0x40`; current firmware supports `0x40`, `0x41`, `0x44`, and `0x45`. Provide a documented solution for conflicts with external sensors, such as hardware address selection paired with configuration through the existing interface.
- Under USB-only power, correctly interpret absent DC and the resulting INA219 reading. Do not claim USB voltage or total USB current measurement if the selected circuit location does not measure them.
- Do not automatically reuse the firmware power-gate threshold intended for a 5 V rail as a battery-voltage threshold. Document the necessary existing settings without changing GPIO assignments or firmware algorithms to accommodate the board.

## 6. Sensors and modem

Support connections for the sensors in the current Air360 project. They are external devices and do not all need to be mounted directly on the PCB. Use `firmware/` and the project documents listed below as the source for supported devices and constraints.

Currently documented support matrix:

| Interface | Supported sensors |
|---|---|
| I²C | AHT30, BME280, BME680, BMP390, SCD30, SCD40, SCD41, SPS30, VEML7700, OPT3001, HTU2X, SHT3X, SHT4X, INA219, INA226 |
| UART | SDS011, PMSX003, MH-Z19B, GPS NMEA |
| GPIO / 1-Wire | PPD42NS, DHT11, DHT22, DS18B20 |
| ADC | ME3-NO2, with its required analog interface |

Supporting all types does not mean devices with the same fixed address or shared UART can operate simultaneously without additional provisions. Document existing restrictions, preserve pin assignments, and do not claim unsupported capabilities. Provide level translation or a clearly specified compatible interface for modules with incompatible signal levels; a 5 V supply does not make ESP32 GPIOs 5 V tolerant.

The modem is external and supported by Air360 firmware. The original hardware family is SIM7600/SIM7600E carrier boards with their own power/support circuitry and separate antenna. The firmware also includes SIM7070, SIM7000, BG96, EC20, SIM800, and Generic dialects; AT-command support alone does not establish identical electrical characteristics for every bare module.

Provide universal **5 V** power and the existing modem signal connector with a defined current budget and logic levels. Do not tie Air360 power design to one modem carrier model or require its photograph to continue. The antenna connects to the modem board; integrating modem RF circuitry onto Air360 is not required.

## 7. Fixed pin assignments and firmware compatibility

### GPIO assignments

RX/TX are named from the ESP32's perspective.

| Function | GPIO |
|---|---|
| I²C SDA / SCL | 8 / 9 |
| Sensor UART2 RX / TX | 16 / 15 |
| Modem UART1 RX / TX | 18 / 17 |
| Modem PWRKEY / SLEEP(DTR) | 12 / 21 |
| GPIO1 / GPIO2 / GPIO3 external sensor signals | 4 / 5 / 6 |
| Native USB D− / D+ | 19 / 20 |
| RGB LED | 48 |
| BOOT | 0 |
| RESET | CHIP_PU / EN, not an arbitrary GPIO |

Preserve these assignments. GPS and the modem use UART1 by default; account for existing configuration options without rearranging connector pins. Verify pin availability on the selected N16R8 module before completing the schematic.

### Connector pin order

The following pin numbers are the confirmed connector pinout, taken from the firmware; the user confirmed them on 2026-11-04 and they do not need re-verification. These are electrical pin numbers, not a view of the cable plug. Preserve them and reconcile physical orientation with the original shield/existing cables before release. If the old shield differs from this table, do not silently change the interface: establish the original compatible pinout from project files.

| Connector | Pins in order |
|---|---|
| I2C1, I2C2, I2C3 | 1: +5V; 2: 3V3; 3: GND; 4: SCL; 5: SDA |
| SPS30 | 1: GND; 2: GND; 3: SCL; 4: SDA; 5: +5V |
| SCD30 | 1: GND; 2: +5V; 3: SDA; 4: SCL |
| UART2 | 1: +5V; 2: 3V3; 3: GND; 4: RX2 (GPIO16); 5: TX2 (GPIO15) |
| MODEM | 1: +5V; 2: GND; 3: ESP32 RX (GPIO18); 4: ESP32 TX (GPIO17); 5: PWRKEY (GPIO12); 6: SLEEP/DTR (GPIO21) |
| GPIO1 | 1: +5V; 2: 3V3; 3: GND; 4: GPIO4 |
| GPIO2 | 1: +5V; 2: 3V3; 3: GND; 4: GPIO5 |
| GPIO3 | 1: +5V; 2: 3V3; 3: GND; 4: GPIO6 |
| Debug J2 | 1: U0TXD; 2: U0RXD; 3: GND |

Connector types follow shield v1.3: 2.54 mm straight pin headers for MODEM, UART2, GPIO, and I2C; JST ZH 1.5 mm vertical for SPS30 and SCD30. Keep them identical to the shield (user requirement, 2026-11-04). Compatibility includes connector housing/keying and numbering at both cable ends. Do not reverse power and ground when changing footprints.

## 8. Mandatory design stages

Work sequentially. Do not begin routing before completing schematic design and component selection.

| Stage | Work | Deliverable / completion criterion |
|---|---|---|
| 1. Requirements and architecture | Establish both power inputs, interfaces, battery profiles, current budgets, UVLO behavior, DevKitC functionality, and firmware compatibility | Requirements, operating modes, and acceptance criteria; distinguish unknowns from assumptions |
| 2. Components and calculations | Select converters, protection, supervisors/switches, INA219/shunt, connectors, and passives; account for startup, ripple, temperature, and all load currents | BOM with exact parts/packages, calculations, and saved sources; no critical decisions deferred until after routing |
| 3. Schematic | Organize functional sheets, power/ground, UVLO, interfaces, and control; verify pin numbers, polarity, addresses, and startup logic | Reviewed schematic, resolved ERC findings, pin-compatibility table; analyze USB/DC/discharge/restart cases |
| 4. Rules and mechanics | Select manufacturer/stack-up/copper, trace/via/clearance rules, board dimensions, holes, and cable access; verify footprints/models | PCB rules and mechanical constraints consistent with actual parts and fabrication technology |
| 5. Functional placement | Group DC protection/monitoring; buck with capacitors/inductor/FB; 3V3; USB/ESD; ESP32; sensor/modem connectors | Functional grouping, no body/pad/silkscreen overlaps, soldering/cable access, and actual 2D/3D inspection |
| 6. Routing | Route critical power loops and return paths first, then sensitive circuits/USB and remaining signals; use calculated widths/vias | Complete connectivity, correct current paths, continuous reference ground where required, and actual filled copper |
| 7. Final verification | ERC/DRC, netlist comparison, polarity/pins, mechanics/silkscreen/3D, fabrication rules, power and UVLO modes | Concrete verification results and corrected defects; do not present calculations/simulation/inspection as physical testing |
| 8. Manufacturing handoff | Export project, schematic PDF, BOM, Gerber/drill, placement, ordering parameters, and first-power-up instructions from one revision | A consistent prototype manufacturing package and post-assembly test plan |

Placement must follow electrical functions: input capacitors at converter power pins, short switching loops, quiet FB, Kelvin shunt connections, close ESP32 decoupling, and ESD protection near USB. Account for bodies, pads, silkscreen, and manufacturing clearances rather than component centers alone. Move designators with components. Inspect actual editor views and real 3D models. A clean electrical DRC does not prove mechanical clearance.

## V2.2 revision, 2026-11-04

These points override the earlier sections for project `Air360_V2.2`, UUID `03cfb60824c0433cafe2a8958e9efd4c`.

- DC input is a 2S battery only, Li-ion or LiFePO4, about 6–8.4 V. A 5 V supply goes through USB-C. 5 V at DC must not start the board and must not damage it.
- There is no 5 V profile at DC. JP1 selects Li-ion or LiFePO4 only.
- +5V target is 1–1.5 A, based on field data: about 140 mA without a modem, 250–300 mA with one. High modem bursts may need a separate supply.
- A 12 V adapter plugged into DC by mistake should not cause damage. It is not an operating mode.
- Connectors, shield positions, board outline, mounting holes and USB-C do not change in this revision.

## 9. Working without repetition or loops

- Maintain one current record of stages, decisions, specific defects, and correction results. Do not reopen closed issues without new evidence; identify the reason and affected area when revisiting one.
- Do not start each response with another general audit, the same datasheet searches, or a complete BOM reselection. Save and reuse results.
- Create one initial backup before actual changes and checkpoints at substantial completed stages. Do not create a backup for every read or message.
- Do not rerun old one-shot mutation scripts. They are not a reproducible board-generation procedure.
- Consult an external source only for a specific missing parameter or selection. Do not replace implementation with endless rechecking of established facts.
- Do not make questions about an external modem a prerequisite for all work. Design a defined Air360 interface and power budget, and document actual compatibility limits.
- Do not adopt a new architecture because of an ambiguous phrase while ignoring the core requirements. Do not undo a change merely because the user expresses frustration, without an instruction to undo it.
- Do not leave a partially changed schematic and mismatched PCB without explicitly recording that state. Finish logical blocks and preserve the actual result.
- The user has requested end-to-end execution: do not seek approval for every routine step. Ask only for genuinely indispensable missing inputs, and continue independent work.
- After saying that work is underway, perform it in the current session. Do not promise background continuation after ending a response. Distinguish “researched,” “changed,” “verified,” and “unfinished.”
- Do not declare the existing board ready based solely on DRC, or discard existing work without established reasons. Rework necessary blocks or restart implementation if justified, while following the stages and preserving interface compatibility.

## 10. EasyEDA and skills

Work in **EasyEDA Pro** using the **easyeda-api** skill. It is installed at `/Users/albert/.agents/skills/easyeda-api/SKILL.md`. Claude must read it and use documented methods. If the path has changed, locate the installed skill rather than inventing API calls.

The electrical-design methodology skill is available at `/Users/albert/.codex/skills/power-pcb-design/SKILL.md`. Apply it consistently with the user's explicit instruction to complete the work: do not turn internal review gates into repeated requests for permission for every action.

The working project is `Air360_V2.2`. The earlier `Air360_V2` project is an unsuccessful attempt: do not use it as a design reference. Live project details, provided as connection references rather than guarantees of current state:

- Project: `Air360_V2.2`, UUID `03cfb60824c0433cafe2a8958e9efd4c`.
- PCB: `PCB1`, UUID `93ecb0b02681a8ec`.
- Schematic: `schematic1`, UUID `cdf4394e2fb2e7ce`; pages `power` `832322dde21e9463`, `connectors` `225f56858bcee42c`, `uvlo` `7ca8a5ed4c92187a`, `core` `639e38c8049a27e4`, `interfaces` `c211ae6ae66dcfec`.
- The local API Gateway previously ran at `127.0.0.1:49620`; verify connectivity and the active document before operating.
- PCB API units are mil; schematic units and individual data formats can differ. Check documented units/scaling rather than copying coordinates blindly.
- The abandoned board was 90×70 mm with four layers. This is not a target; derive stack-up and mechanics during design.

## 11. Source documents outside the cleaned directory

These files remain in the repository. Links are relative to this document:

- [Assembly and connections](../../docs/guide/assembly.md).
- [Supported sensor matrix](../../docs/firmware/sensors/supported-sensors.md).
- [Sensor documentation](../../docs/firmware/sensors/README.md).
- [INA219 addresses and calibration](../../docs/firmware/sensors/ina219.md).
- [Existing firmware power gate and limitations](../../docs/firmware/power-gate.md).
- [Startup sequence](../../docs/firmware/startup-pipeline.md).
- [Modem and GPIOs](../../docs/firmware/sensors/sim7600e.md).
- [Modem management](../../docs/firmware/cellular-manager.md).
- [Firmware configuration](../../docs/firmware/configuration-reference.md).
- [Kconfig](../../firmware/main/Kconfig.projbuild), [sdkconfig.defaults](../../firmware/sdkconfig.defaults).
- [Sensor registry](../../firmware/main/src/sensors/sensor_registry.cpp), [sensor types](../../firmware/main/include/air360/sensors/sensor_types.hpp), [INA219 driver](../../firmware/main/src/sensors/drivers/ina219_sensor.cpp).

When documentation and implementation disagree, check the firmware code. Correct the documentation rather than silently breaking board compatibility.

## 12. Handoff state

Old files, local exports/backups, images, and scripts in this directory were deleted at the user's explicit request after transferring the requirements into this document. That cleanup does not modify the live EasyEDA project. Do not treat previous reports as proof of readiness.

The latest unfinished work considered USB/DC isolation, but no implemented correction remains. An unconnected U6 LM66200 was added and then removed; that attempt was reverted and did not change the PCB. Previously discussed TPS63070/TPS2121/AP63203 parts are not mandatory BOM items for this specification. Parts used in `Air360_V2`, including the LMR33630, are candidates only; select components on their merits and record the result once.

The prototype test plan must include: 2S DC operation, 5 V charger at the DC input, USB alone, simultaneous sources and switchover, startup at the maximum intended load, sag/slow discharge, voltage recovery after shutdown, absence of repeated startup cycles, temperature, INA219 readings, programming/reset, and operation of existing sensors/modems. Perform physical tests after assembly; do not present prepared files as an already tested board.
