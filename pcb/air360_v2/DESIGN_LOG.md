# Air360 V2 — Design Log

This is the single current record of stages, decisions, and defects required by `REQUIREMENTS.md` section 9. Requirements live in `REQUIREMENTS.md`; this file records how they are being met.

Evidence labels: **[repo]** read from this repository; **[calc]** calculated here; **[ds]** from a datasheet read in this work; **[assumed]** engineering estimate that stage 2 must confirm. Nothing here has been physically tested.

## Stage status

| Stage | State | Notes |
|---|---|---|
| 1. Requirements and architecture | Done | This document, 2026-11-04 |
| 2. Components and calculations | Done | Sections 12–16, 2026-11-04 |
| 3. Schematic | Done | Section 17, 2026-11-04 |
| 4. Rules and mechanics | Done | Section 18, 2026-11-04 |
| 5. Functional placement | Done | Section 19, 2026-11-04; accepted by the user after 3D review |
| 6. Routing | Done | Section 20, 2026-11-04; DRC returns no errors, pending user review |
| 7. Final verification | Done | Section 21, 2026-11-04; bench tests remain |
| 8. Manufacturing handoff | Done | `release/v2.2/`, 2026-11-07; not ordered |
| V2.2 cost-down revision | Done, pending review | Sections 23–25, `release/v2.2/`, 2026-11-04; not ordered |

## 1. Decisions

| ID | Date | Decision | Reason |
|---|---|---|---|
| D1 | 2026-11-04 | Working project is `Air360_V2.2`; `Air360_V2` is not a reference | User: unsuccessful attempt |
| D2 | 2026-11-04 | Battery is 2S only | User: simplifies the board |
| D3 | 2026-11-04 | Both Li-ion and LiFePO4 are supported through one on-board chemistry jumper | User asked for both; only the restart threshold differs |
| D4 | 2026-11-04 | Connector pinout in `REQUIREMENTS.md` section 7 is confirmed | User: taken from firmware; no re-check |
| D5 | 2026-11-04 | One switched +5V rail for everything; UVLO controls the DC path only | Loads and ESP32 lose power together, so no signal line can backfeed a dead load |
| D6 | 2026-11-04 | Buck rated for at least 4 A of output (budget peak 3.33 A) | Section 5 |
| D7 | 2026-11-04 | DC input survives at least 26 V | Solar module may present panel voltage, section 3; also covers a mistaken 3S pack |
| D8 | 2026-11-04 | DC has priority over USB when UVLO reports DC healthy | Programming from a computer must not load the port |
| D9 | 2026-11-04 | INA219 strapped to `0x41` by default | `0x40` collides with HTU2X; `0x44`/`0x45` collide with SHT3X, SHT4X, OPT3001 |
| D10 | 2026-11-04 | Firmware `power_gate` stays disabled on this board | Section 8: it would sleep on USB-only power |
| D11 | 2026-11-04 | No second USB connector and no USB-UART bridge | Section 9 mapping shows native USB plus J2 covers every DevKitC function |
| D12 | 2026-11-04 | Automatic restart after UVLO is required; a manual-reset latch is not acceptable | Solar station stays connected and must recover unattended, section 4 |
| D13 | 2026-11-04 | UVLO thresholds are set by fixed 0805 resistors, with a jumper for the two chemistry presets; no trimmers | User wants adjustable thresholds; trimmers drift outdoors; the monitor must work with the ESP32 off, so firmware cannot set them |
| D14 | 2026-11-04 | A 5 V charger at DC is a third position of the input-profile jumper: UVLO is overridden, buck EN is forced on, and the buck runs in dropout | User requirement. A charger and a discharged pack cannot be told apart by voltage. A direct bypass to +5V was rejected: a wrong setting on a battery would put 8.4 V on the rail, whereas the buck still regulates |
| D15 | 2026-11-04 | Sensor and modem connectors copy the shield v1.3: same types, pitch, pin order, and column spacing | User requirement; section 12 |
| D16 | 2026-11-04 | UVLO uses TLV3011 for thresholds, then TPS3808G01 for the restart delay; no single-chip solution | TPS3808 holds RESET low at power-up, which would block a first start below `V_ON` |
| D17 | 2026-11-04 | DC/USB switching is discrete: AO4407A P-FETs driven from the UVLO enable signal | Hand-solderable, carries 3.3 A; no QFN power mux required |
| D18 | 2026-11-04 | Correction: `V_OFF` tolerance is wider than the ±0.1 V first stated, so first power-up includes a threshold check | TLV3011 reference is 1.208–1.276 V; section 13 |
| D19 | 2026-11-04 | 3V3 LDO is SGM2212-3.3 in TO-252, not SOT-223 | Datasheet θJA 117 °C/W in SOT-223 against 56 °C/W; SOT-223 would rise about 50 °C at 0.43 W |
| D20 | 2026-11-04 | 3V3 output capacitance limited to 10 µF effective | SGM2212 datasheet range is 1–10 µF |
| D21 | 2026-11-04 | LED runs directly from +5V, no diode | WS2812B-2020 VIH is 2.7 V; supply range 3.7–5.3 V covers the rail |
| D22 | 2026-11-04 | Test points TP1 VREF, TP2 EN_NODE, TP3 GND, replacing header J6 | First-power-up threshold check needs VREF, which is otherwise only on SOT-23 pins. User chose pads over the unfitted header to save space. Pads are 1.5 mm, set on the PCB; the library part has 0.5 mm, too small for a probe |
| D23 | 2026-11-04 | JP1 is a plain 2×3 pin header `HDR-M_2.54_2x3`, not the KH boxed header | Both KH parts were 15.5 mm wide shrouded IDC headers |
| D24 | 2026-11-04 | Board height 85 mm | 75 mm left the power stage cramped; user allowed enlargement |
| D25 | 2026-11-04 | J5 removed; 13 spare ESP32 pins are No Connect | User: not needed here. Supersedes the spare-GPIO row in section 9 and D-level J5 references |
| D26 | 2026-11-04 | Mounting holes are 3.03 mm non-plated through holes H1–H6, not outline circles | Outline circles exported as a thin ring with the disc left in place: two cylinders per hole in the STEP model. The earlier note that a pad hole was stored incomplete was wrong. Confirmed by one cylinder r 1.516 mm per hole in the STEP model and six T02 3.03 mm hits in `Drill_NPTH_Through.DRL` |
| D27 | 2026-11-04 | LED1 is SK6812MINI-HS, 3.5 × 3.5 mm, on 3V3 like DevKitC, replacing WS2812B-2020 on +5V | User asked for the DevKitC-style ARGB LED. Supersedes D21. Addressable with the existing `LED_MODEL_WS2812` driver; on the real board its timing is to be confirmed |
| D28 | 2026-11-04 | Silkscreen rule: at least 0.3 mm between component outlines, and designators must not touch outlines, pads, holes, or each other | User review |
| D29 | 2026-11-04 | J4 stays | Confirmed by the user; silkscreen shows + and − |
| D30 | 2026-11-04 | Placement spacing target is 0.75 mm or more between outlines; the 0.3 mm in D28 is only the hard floor. The empty right area takes the interface block | User review: the board felt too dense, R3 and R6 touched. Exceptions are parts that must sit at their pins: C5, C6, C7 at U2 |
| D31 | 2026-11-04 | J3, J4 and F1 moved right: J3 and J4 by 1 mm, F1 by 0.3 mm | J3 was 2.05 mm from the centre of H1, under an M3 head. User chose moving J3. The jack opening no longer matches the shield position by 1 mm; check any enclosure cut-out |
| D32 | 2026-11-04 | Green LED2 with R39 4.7 kΩ from BUCK_5V to GND shows that the buck is running | User asked for a visible state indicator. `EN_NODE` is too high-impedance to drive it, and BUCK_5V draws nothing from a tripped pack |
| D33 | 2026-11-04 | Test pads are separate 1.5 mm pads TP1P–TP3P over TP1–TP3 | A pad size edited on a component is lost on reload, so Gerber kept 0.5 mm. Supersedes the pad note in D22 |
| D34 | 2026-11-04 | Cost-down V2.2 targets 1–1.5 A at +5V with all power parts sized to that | User decision after field data, 130–145 mA without modem and about 250–300 mA with it. Supersedes D6 |
| D35 | 2026-11-04 | V2.2 DC input is 2S only, 6–8.4 V | User: only 2S at the input. Supersedes for V2.2 the 5 V charger at DC (D14) and the 26 V tolerance; JP1 keeps only `Li` and `LFP` |
| D36 | 2026-11-04 | V2.2 buck is AP63205WU-7, fixed 5 V, 2 A, 32 V input | 2S input allows it, few external parts, datasheet Rev. Nov 2024. MP2315 rejected: marked not recommended for new designs. LMR33620 not needed |
| D37 | 2026-11-04 | UVLO block clears the left edge by 4.4 mm, matching the right side | User review of V2.2 |
| D38 | 2026-11-07 | V2.2 silkscreen shows `RESET` and `BOOT` in place of SW1 and SW2 | User: the button function must be readable on the board. The designators stay in the schematic, BOM and placement file; only their silkscreen text is hidden |

## 2. Architecture

```text
DC IN ── reverse protection ── TVS ── Rshunt 0.1 Ω ── input caps ── buck 5 V ── ideal-diode/priority ──┐
                  │                      INA219                       ▲ EN                             ├── +5V ──┬── modem (2 A limit)
                  │                                                   │                                │         ├── sensors (limited group)
                  └── UVLO monitor, always on from battery ───────────┘                                │         └── 3V3 regulator ── ESP32, 3V3 sensors
                         chemistry jumper                                                              │
USB-C VBUS ── ESD ── current limit ── reverse-blocking path ───────────────────────────────────────────┘
USB-C D+/D− ── ESD ── GPIO20/GPIO19
```

- UVLO acts on the buck EN rather than on a series switch: a series switch adds drop exactly where headroom is smallest (section 5).
- A disabled buck with +5V present from USB would backfeed through the high-side switch body diode into VIN, so the buck output must be blocked from the rail.
- UVLO sense point is the protected battery node, before the shunt, so load current through the shunt does not count as battery sag.

## 3. DC source facts

- Reference pack is 2S LiFePO4, charge voltage 7.2 V [repo: `docs/guide/solar-power-module.md`].
- CN3722 solar module does not cut off the load, and the BMS disconnects only at about 2.0–2.5 V/cell, so a pack can reach 4–5 V without Air360 protection [repo].
- **Documentation conflict:** the same guide says the device runs "from the panel while the sun shines" and that output K3 "follows the battery rail, roughly 6.4–7.2 V". Until measured, assume K3 can reach panel voltage. Air360's INA219 then reads panel voltage rather than battery voltage during the day.
- 3S is not supported, but must not destroy the board; UVLO does not protect such a pack.

## 4. UVLO behaviour

Implementation target: a battery-powered comparator or supervisor with wide hysteresis driving buck EN.

| Parameter | LiFePO4 | Li-ion | Basis |
|---|---|---|---|
| `V_OFF` falling | 6.2 V | 6.2 V | 3.1 V/cell; matches the 6200 mV in `power-gate.md` [repo] |
| `V_ON` rising after a trip | 6.8 V | 7.2 V | Above unloaded recovery, about 6.7 V LFP and 7.0 V Li-ion [assumed] |
| First connection | Starts if above `V_OFF` + delay | Same | A full LFP pack rests near 6.65 V, below `V_ON` |
| Trip filter | ≥ 100 ms | ≥ 100 ms | Must ride through modem bursts [assumed] |
| Restart qualification | ≥ 30 s above `V_ON` | ≥ 30 s | Rejects surface-charge recovery [assumed] |
| Monitor current | ≤ 100 µA | ≤ 100 µA | Negligible against pack capacity [assumed] |

This changes the LiFePO4 `V_ON` from the 6.6 V first estimated to 6.8 V. At 6.6 V a tripped pack could recover past `V_ON` by itself and repeat the start–sag–shutdown cycle. At 6.8 V restart needs charging or panel power, so a tripped pack stays off.

Hysteresis comparator power-up state is biased to ON, so the first-connection row needs no separate latch. Each physical connection of a weak pack gets one start attempt, then stays off until charged.

Wrong jumper position: LiFePO4 on the Li-ion profile restarts only when charged above 7.2 V; Li-ion on the LiFePO4 profile may restart at 6.8 V but still stops at 6.2 V. Neither over-discharges the pack.

### Setting the thresholds

Comparator with reference `Vref`: divider R1 (top) and R2 (bottom) from the battery node, output high enables the buck. `V_ON` feedback uses a resistor Rd in series with a diode from the sense node to the comparator output, so it conducts only while the output is low. `V_OFF` and `V_ON` can therefore be set independently; plain hysteresis would move both.

- `V_OFF = Vref × (1 + R1/R2)`
- `V_ON = V_OFF + R1 × (Vref − Vd) / Rd`
- The chemistry jumper adds a second resistor in parallel with Rd.

Final values are in section 13.

Stage 8 documentation must include a resistor table and the lower limit: `V_OFF` must stay above about 5.8 V (section 5).

**Risk to close in stage 2:** weak sun may charge an LFP pack without reaching 6.8 V until nearly full, delaying restart. Check against charge curves.

## 5. Power budget

Load currents at the +5V rail, heaviest supported configuration. The 3V3 rail is counted at its 5 V input current, which holds for a linear regulator.

| Load | Continuous mA | Peak mA | Source |
|---|---:|---:|---|
| 3V3 rail: ESP32-S3, LED, INA219, 3V3 sensors | 250 | 600 | [assumed] 500 mA ESP32 supply plus 100 mA external |
| Modem | 500 | 2000 | [assumed] SIM7600-class carrier |
| SPS30 | 65 | 80 | [repo] |
| SCD30 | 19 | 75 | [repo] |
| UART2 sensor, one of PMSX003, SDS011, MH-Z19B | 100 | 150 | [repo] MH-Z19B peak; PMSX003 [assumed] |
| GPIO1–3, three PPD42NS as the worst case | 270 | 270 | [repo] 90 mA each |
| I2C1–3 small sensors | 30 | 150 | [assumed] |
| **Total** | **1234** | **3325** | [calc] |

Peaks are summed as if simultaneous, which is a deliberate worst case.

Field data from the user's shield build, 2026-11-04: without modem 130–145 mA at about 5 V, 0.62–0.85 W, averaged over three days (chart points are smoothed, so short peaks are hidden); with modem about 250–300 mA, as reported by the user. Short modem bursts were not measured. U2 may move to LMR33630ADDAR (3 A) if that lowers cost; this would revisit D6, and L1 and output capacitors must be checked against its datasheet first.

### DC worst case

At 90 % efficiency [assumed]; drops: shunt 0.1 Ω, reverse protection about 30 mΩ [assumed], buck dropout about 0.4 V at 3 A [ds: LMR33630 Figure 8-10, read from a graph].

| Case | Pack V | Input A | Shunt W | Shunt drop V |
|---|---:|---:|---:|---:|
| Continuous, 1.23 A at 5 V | 6.2 | 1.11 | 0.12 | 0.11 |
| Continuous, 1.23 A at 5 V | 8.4 | 0.82 | 0.07 | 0.08 |
| Peak, 3.33 A at 5 V | 5.8 | 3.18 | 1.01 | 0.32 |

- Minimum pack voltage under peak load is about 5.8 V: 5.4 V for the buck plus 0.32 V shunt and 0.10 V protection. `V_OFF` of 6.2 V leaves about 0.4 V of margin for cable and connector drop.
- INA219 range is ±3.2 A at the firmware gain [repo]. The peak case sits at the limit, so bulk input capacitance goes after the shunt. Saturation would affect a reading, not the hardware.
- Shunt must tolerate about 1 W continuously at the current limit; use a part rated 2 W for derating.

### USB

Path resistance assumed at 0.25 Ω total for cable, connector, and protection [assumed]. Source 5.1 V.

| Rail current | +5V at board | Result |
|---|---:|---|
| 1.0 A | 4.85 V | In range |
| 2.0 A | 4.60 V | In range; MH-Z19B needs 4.5–5.5 V [repo] |
| 3.0 A | 4.35 V | Below 4.5 V sensor minimum |

- Rated USB load: **2 A total at the rail, source rated at least 2 A; 3 A source recommended with a modem**. The 3.33 A heaviest configuration is DC-only.
- Hardware needs a USB current limit; a charger rating is not protection. Target trip near 3 A, chosen in stage 2.
- 2G modem bursts cannot be fully absorbed by on-board bulk capacitance: 1.2 A for 577 µs on 1000 µF droops 0.69 V [calc].
- **Computer port:** without DC, supports the board and low-power I²C sensors only, no modem or fan sensors. A 500–900 mA port cannot cover the 600 mA 3V3 peak with a margin. With DC present, DC priority means the port carries data only.

### 5 V charger at DC

Source 5.1 V, cable 0.15 Ω [assumed], shunt 0.1 Ω, protection 0.03 Ω, buck dropout 0.15 V per ampere [ds: LMR33640 150 mV at 1 A, typical; dropout assumed linear].

| Rail current | +5V at board | Result |
|---|---:|---|
| 0.5 A | 4.88 V | In range |
| 1.0 A | 4.67 V | In range |
| 1.4 A | 4.50 V | Limit for 4.5 V sensors |
| 2.0 A | 4.24 V | Below sensor minimum |

Rated load in this mode is **1.4 A total at the rail**, which is below the 2 A USB rating. A modem is better served by USB-C.

### Output limits

| Output | Limit | Note |
|---|---|---|
| MODEM +5V | 2 A | 6-pin connector rating to be checked in stage 2 |
| Sensor +5V, all connectors combined | 1 A | Group limiter chosen in stage 2 |
| External 3V3, all connectors combined | 100 mA | |
| Allowed +5V ripple | ≤ 100 mV peak-to-peak | [assumed] Confirm in stage 2 |

## 6. Operating modes

| Mode | Source of +5V | INA219 reads | Expected behaviour |
|---|---|---|---|
| DC healthy | Buck | Pack voltage and current | Normal |
| USB only | USB | About 0 V, no USB current | Normal; report as "no DC", not as a fault |
| DC healthy and USB | Buck | Pack | USB draws no load current; data works |
| DC below `V_OFF`, USB present | USB | Pack, zero current | Switch without requiring DC; ride-through is a target, not a promise |
| DC below `V_OFF`, no USB | None | Off | Everything off, monitor alive |
| Reverse DC | USB if present | Off or 0 V | No damage |
| Dead or absent pack, USB present | USB | Near 0 V | Normal |
| 5 V charger at DC, profile set to 5 V | Buck in dropout | Input voltage and total current | Normal, reduced load, section 5 |
| 5 V charger at DC, profile set to a battery | None | Off | Stays off, no damage |
| Battery at DC, profile set to 5 V | Buck | Pack | Runs, but UVLO is disabled and only the BMS protects the pack |

## 7. Interfaces

**I²C** SDA 8, SCL 9. Pull-ups to 3V3 on the board. External modules with pull-ups to 5 V are not allowed.

Address limits that remain [repo: `sensor_registry.cpp`]:

| Addresses | Devices that cannot coexist |
|---|---|
| `0x40` | HTU2X and default INA219/INA226 |
| `0x44`, `0x45` | SHT3X, SHT4X, OPT3001, INA alternates |
| `0x62` | SCD40, SCD41 |
| `0x76`, `0x77` | BME280, BME680, BMP390 |

**INA219** at `0x41` needs its address set in the existing sensor configuration UI. Solder jumpers allow other firmware-supported addresses. Bus voltage is measured at VIN− referenced to GND [ds to confirm in stage 2]. Common-mode limit is 26 V, which sets D7.

**UART.** UART1 is modem by default; GPS needs moving to UART2 [repo]. UART2 has one connector, so one UART sensor on it.

**GPIO1–3** must tolerate PPD42NS output above 4.0 V [repo], since GPIOs are not 5 V tolerant. The protection must also pass 1-Wire/DHT bidirectional signals and ME3-NO2 analog input on the same pins. ME3-NO2 uses ADC with 12 dB attenuation and its analog front end stays external [repo].

**Modem.** GPIO12 PWRKEY and GPIO21 DTR are driven active-high directly at 3.3 V logic [repo: `modem_gpio.cpp`]. Compatibility is limited to carriers accepting 3.3 V logic and 5 V supply.

## 8. Firmware compatibility

No source change is required. Existing configuration needed:

- Enable INA219 and set address `0x41`.
- Keep `power_gate_enabled = 0`, the default. On USB-only power the INA reads about 0 V, so an enabled gate would sleep 5, 10, 20, then 30 minutes before its bypass [repo]. Its 4700 mV default also targets a 5 V rail.
- If an installation is DC-only and still wants the gate, use a threshold above hardware `V_OFF`, about 6400 mV.
- Firmware expects 16 MB flash and octal PSRAM, matching N16R8 [repo: `sdkconfig.defaults`].

## 9. DevKitC function mapping

| DevKitC function | Air360 V2 | Note |
|---|---|---|
| 5 V from USB | USB-C VBUS, section 5 | Hardware limit added |
| 3V3 regulator | On-board, rated at least 800 mA | Linear or buck decided in stage 2 |
| EN RC and RESET button | Same | EN to CHIP_PU |
| BOOT button | Same, GPIO0 | |
| USB-UART bridge port | Omitted, D11 | ROM flashing, reset, and console work through native USB; `firmware/sdkconfig` has primary console UART and secondary USB-Serial-JTAG [repo] |
| UART0 access | J2: U0TXD, U0RXD, GND | External adapter; manual BOOT+RESET since J2 has no control lines |
| Native USB | USB-C on GPIO19/20 | CC 5.1 kΩ pull-downs needed for C-to-C chargers |
| JTAG | Built-in USB-JTAG | External JTAG pins are not exposed |
| RGB LED | SK6812MINI-HS on GPIO48, per firmware `kRgbLedGpio` | D27 |
| GPIO headers | Connectors only; J5 removed, D25 | Spare GPIO are not exposed |

**Module restrictions.** GPIO26–32 serve internal flash and are not exposed; GPIO35–37 serve octal PSRAM on N16R8 and are unavailable. Strapping pins 0, 3, 45, 46 must not be loaded at boot. No assigned Air360 function uses 3, 45, or 46. These facts are from memory of Espressif documents; confirm against the module datasheet in stage 2. All assigned GPIOs in `REQUIREMENTS.md` section 7 avoid the restricted pins.

## 10. Acceptance criteria

1. Every pin in `REQUIREMENTS.md` section 7 matches the netlist.
2. With 3.33 A at +5V and 6.2 V pack, +5V stays in regulation and no part exceeds its rating [calc, then bench].
3. Pack falling slowly through `V_OFF` removes all +5V loads once, with no later restart unless charged above `V_ON`.
4. Load removal alone never causes a restart.
5. Dead, absent, or reversed pack does not stop USB operation.
6. USB never backfeeds DC, and buck never backfeeds VBUS.
7. Flashing, reset, and logs work over native USB with stock firmware.
8. INA219 reads pack voltage within its specification at `0x41` with stock firmware.
9. 26 V at DC input causes no damage; UVLO need not protect that pack.
10. A 5 V charger at DC runs the board with the 5 V profile, and leaves it off and undamaged with a battery profile.

## 11. Stage 1 open items, closed in stage 2

| ID | Item | Result |
|---|---|---|
| O1 | K3 voltage on the solar module | Still unknown; DC input tolerates 26 V regardless |
| O2 | Buck | LMR33640ADDAR, section 14 |
| O3 | DC/USB priority | Discrete, section 13 |
| O4 | UVLO | TLV3011 + TPS3808G01, section 13 |
| O5 | Limiters | PTCs, section 15 |
| O6 | 3V3 thermal | Section 14 |
| O7 | GPIO1–3 protection | Section 13 |
| O8 | DC connector | DC-005 and KF301 in parallel, as on the shield |
| O9 | [assumed] values | Remaining ones listed in section 16 |

## 12. Shield connector geometry

Source: `docs/hardware/Gerber_Air360-Shield_PCB_Air360_2026-06-05.zip` (drill file), shield v1.3 photos, and the shield BOM supplied by the user on 2026-11-04. The oshwlab page could not be opened from this environment.

| Connector | Type | Pitch | Pins as printed on the shield |
|---|---|---|---|
| MODEM/UART1 | 1×6 straight pin header | 2.54 mm | 5V, GND, RX, TX, PWRKEY, SLEEP |
| UART2 | 1×5 pin header | 2.54 mm | 5V, 3V, GND, RX, TX |
| GPIO 4, 5, 6 | 3 × 1×4 pin header | 2.54 mm | 5V, 3V, GND, SIG |
| I2C ×3 | 3 × 1×5 pin header | 2.54 mm | 5V, 3V, GND, SCL, SDA |
| SPS30 | JST ZH 5P, vertical | 1.5 mm | 5V, SDA, SCL, GND, GND |
| SCD30 | JST ZH 4P, vertical | 1.5 mm | SCL, SDA, 5V, GND |
| DC power | DC-005, 5.5×2.1 mm | | Centre positive to be confirmed on the shield |
| Screw terminal | KF301-5.0-2P | 5.0 mm | |

- These agree with `REQUIREMENTS.md` section 7. SPS30 and SCD30 are printed in the reverse order of the table's pin numbers, so pin 1 of the table is the printed right-hand pin. Physical order follows the shield.
- Column spacing: 3.556 mm within the GPIO and I2C groups; 7.112 mm from MODEM to UART2; 5.207 mm from UART2 to GPIO; 5.334 mm from GPIO to I2C. All header columns start on one line.
- Shield board is 50.8 × 81.3 mm with M3-size holes at 44.47 × 74.80 mm spacing. Whether the new board keeps these is decided in stage 4.

## 13. Control circuit calculations

### UVLO

```text
V_PROT ── R1 499k ──┬── R2 124k ── GND
                    ├── Cf 470n ── GND
                    ├── Cb 470n ── 3V3M      power-up bias to ON
                    ├── IN+ TLV3011 (IN− = REF 1.242 V)
                    └── Rd 787k ── 1N4148 ── OUT      V_ON feedback, conducts only when OUT is low
Li-ion jumper adds 1.13M in parallel with Rd
OUT ── SENSE TPS3808G01; CT 10 µF
RESET ── 1M pull-up to 3V3M ── 100k ── EN_NODE ── buck EN
5 V jumper ties EN_NODE to 3V3M
HT7533-1 from V_PROT makes 3V3M
```

| Result | Value | Basis |
|---|---|---|
| `V_OFF` | 6.24 V | `1.242 × (1 + 499/124)` |
| `V_ON` LiFePO4 | 6.81 V | Rd 787 kΩ, `Vd` 0.35 V at about 1 µA [assumed] |
| `V_ON` Li-ion | 7.20 V | Rd in parallel with 1.13 MΩ = 464 kΩ |
| Trip filter | 93 ms | 99 kΩ × 940 nF |
| Restart delay | 57 s nominal, about 34 s minimum | [ds] `(C nF + 0.5)/175` s; 10 µF; the 0.6 lower ratio is taken from the 180 nF table row |
| Monitor current, tripped | about 31 µA | 2.8 + 2.4 + 2.5 + 3.3 µA ICs and pull-up, 10 µA divider, 10 µA buck max; [ds] except HT7533 |

- First start also takes the 57 s delay, including a freshly connected battery.
- TLV3011 has no power-on-reset, so the Cb kick is what selects ON at first connection. TLV3011B has a POR and was not found in the library; do not substitute it without rechecking the kick.
- 26 V at DC drives IN+ above its supply; input current stays about 45 µA through R1, under the 10 mA limit [ds].

### Threshold accuracy

Datasheet limits: reference 1.208–1.276 V, offset ±15 mV, resistors ±1 % [ds]. Guaranteed `V_OFF` range is **5.90–6.59 V**. With typical values (±1 % reference, 0.5 mV offset) it is about **6.24 ± 0.12 V**.

The first-power-up procedure must measure `V_OFF` with a bench supply and change R2 if it falls outside 6.1–6.35 V. `V_ON` follows `V_OFF`, so the same trim applies to it.

### Source switching

```text
Buck output ── Q1 AO4407A (D at buck, S at +5V) ── +5V
VBUS ── PTC ── Q2 AO4407A (D at VBUS, S at +5V) ── +5V
EN_NODE ── 100k/100n ── N1 AO3400A ── pulls Q1 gate low; pull-up 100k to +5V
Q1 gate ── N2 AO3400A gate ── pulls Q2 gate low; pull-up 100k to +5V
```

| State | Q1 | Q2 | Result |
|---|---|---|---|
| EN high | On | Off | Buck feeds +5V. Q2 body diode stays off because VBUS is under +5V + 0.6 V, so USB carries no load |
| EN low, USB present | Off | On | USB feeds +5V, and Q1 blocks the buck output |
| EN low, no USB | Off | Off | No power |

- The 10 ms gate delay lets the buck finish soft-start before Q1 turns on. Until then Q1's body diode conducts.
- On a trip, Q2's body diode holds +5V at VBUS − 0.6 V until Q2 turns on, so USB takeover needs no ride-through capacitor.
- In the 5 V DC profile with USB also connected, the buck still has priority, so the rail is lower than USB alone would give. Remove the jumper or the charger; USB-only gives the better rail.

### GPIO1–3

Connector ── 470 Ω ── GPIO, with BAT54S clamp to 3V3 and GND on the GPIO side, and a 4.7 kΩ pull-up to 3V3 on the connector side through a solder jumper.

- PPD42NS at 5 V injects about 3 mA into the 3V3 rail, which is harmless.
- ESP32 driving low against the pull-up gives 0.30 V at the connector, under DS18B20's 0.8 V limit [assumed].
- The jumper must be opened for ME3-NO2, since a pull-up would corrupt the ADC reading. Default closed.

### Other interfaces

- I²C pull-ups 4.7 kΩ to 3V3.
- UART and modem lines: 100 Ω series.
- GPIO12 PWRKEY: 100 kΩ pull-down, so the modem key stays idle during boot.
- GPIO21 DTR: 100 kΩ pull-down.
- LED: firmware uses `LED_MODEL_WS2812`, so WS2812B-2020 is used directly from +5V with a 100 Ω series resistor, D21.

## 14. Power stage calculations

**Buck: LMR33640ADDAR**, 400 kHz, ESOP-8 with pad [ds, except as noted].

| Item | Value | Basis |
|---|---|---|
| FB | 100 kΩ / 24.9 kΩ | 5.02 V |
| Inductor | 6.8 µH, Isat ≥ 6.2 A | `K` 0.19 at 8.4 V and 4 A, 1.48 A ripple at 26 V; Isat above the 6.2 A maximum limit |
| Peak inductor at 3.33 A, 8.4 V | 3.7 A | Under the 4.8 A minimum limit |
| `CIN` | 2 × 10 µF 50 V + 220 nF 50 V + 100 µF 35 V polymer | Ceramic at pins; bulk after the shunt |
| `COUT` | 4 × 22 µF 25 V at the buck, + 470 µF polymer on +5V | Datasheet minimum 3 × 22 µF; total under the 1000 µF limit |
| `CBOOT`, `CVCC` | 100 nF, 1 µF | VCC is not loaded |
| Dissipation, 1.23 A | about 0.25 W | Conduction from RDSON plus switching [assumed] |
| Dissipation, 3.33 A at 6.2 V | about 1.2 W | Short duration; about 36 °C rise at 30 °C/W [assumed] if it were sustained |

**Reverse protection:** Q3 AO4407A, drain at input, source at V_PROT, gate to GND through 100 kΩ, 12 V zener gate to source.

**Input TVS:** SMAJ24A after Q3, so a reversed pack does not forward-bias it.

**Residual risk:** SMAJ24A clamps up to about 39 V, above the 26 V INA219 and 36 V buck limits. It covers ESD and short events, not a sustained surge.

**Updated minimum pack voltage at peak load:** 5.4 V + 3.18 A × (0.1 + 0.03 + 0.03 Ω, including PTC [assumed]) = 5.9 V.

**3V3: SGM2212-3.3 in TO-252.** Dropout 420 mV at 800 mA [ds]. Loss is 0.43 W at 250 mA and 1.0 W at 600 mA from 5 V, so rise is about 24 °C and 56 °C at datasheet θJA. At 4.6 V USB input the headroom is 1.3 V, well above dropout.

**INA219:** A0 to VS, A1 to GND for `0x41`, on solder jumpers.

## 15. Bill of materials

C numbers come from the EasyEDA library search on 2026-11-04. Stock and price were not checked.

| Ref | Part | Package | LCSC |
|---|---|---|---|
| U1 | ESP32-S3-WROOM-1-N16R8 | Module | C2913202 |
| U2 | LMR33640ADDAR | ESOP-8 | C2836966 |
| U3 | SGM2212-3.3XOB2G/TR | TO-252-2 | C5152863 |
| U4 | INA219AIDR | SOIC-8 | C138706 |
| U5 | TLV3011AIDBVR | SOT-23-6 | C2870632 |
| U6 | TPS3808G01DBVR | SOT-23-6 | C19653 |
| U7 | HT7533-1 | SOT-89 | C14289 |
| D1 | USBLC6-2SC6 | SOT-23-6 | C2827654 |
| D2 | SMF5.0A, VBUS | SOD-123 | C266673 |
| D3 | SMAJ24A, DC | SMA | C132586 |
| D4 | BZT52C12S | SOD-323 | C213232 |
| D5–7 | BAT54S, GPIO1–3 | SOT-23 | C268450 |
| D8 | 1N4148WS, UVLO | SOD-323 | C2128 |
| LED1 | SK6812MINI-HS | 3.5 × 3.5 mm | C2922787 |
| Q1–3 | AO4407A | SOIC-8 | C16072 |
| Q4–5 | AO3400A | SOT-23 | C20917 |
| L1 | SPM10065VT-6R8M-D, Isat 15.8 A | 10.5 × 10 mm | C2047831 |
| RS1 | 0.1 Ω 1 % 2 W alloy | 2512 | C42453896 |
| F1, F2 | PTC 3 A hold 33 V, DC and USB | 2920 | C2943634 |
| F3 | PTC 2 A hold 33 V, modem | 1812 | C42924294 |
| F4 | PTC 1.1 A hold 33 V, sensor 5 V | 1812 | C70119 |
| F5 | PTC 0.2 A hold 9 V, external 3V3 | 0805 | C20976 |
| C | 470 µF 10 V polymer | 6.3 mm SMD | C4747954 |
| C | 100 µF 35 V polymer | 6.3 × 8 mm TH | C48970595 |
| J1 | TYPE-C-31-M-12 | USB-C 16P | C165948 |
| J2 | Pin header 1×3, UART0 | 2.54 mm | Library footprint |
| TP1–TP3 | Test pads, not in BOM | 1.5 mm pad | Library `Test-Point` plus separate pads TP1P–TP3P, D33 |
| LED2 | KT-0805G green, buck running | 0805 | C2297 |
| R39 | 4.7 kΩ 1 %, LED2 | 0805 | C17673 |
| J3 | DC005, footprint `DC-IN-TH_DC005` as on the shield | TH | C431533 |
| J4 | KF301-5.0-2P | TH | C474881 |
| I2C1–3, UART2, MODEM, GPIO1–3 | HDR-M-2.54 1×6, 1×5, 1×4 per section 12 | 2.54 mm | Library footprints `HDR-TH_nP-P2.54-V-M` |
| SPS30 | SH-ZH1.5-5A, SPS30 (same footprint as HC-ZH1.5-5A, C2845553) | TH vertical | C54431921 |
| SCD30 | HC-ZH1.5-4A, SCD30 | TH vertical | C2845552 |
| JP1 | HDR-M_2.54_2x3, profile | 2.54 mm | Library footprint |
| SW1–2 | TS-1187A-B-A-B, BOOT and RESET | 5.1 mm | C318884 |

Resistors are UNI-ROYAL 0805 1 %. Capacitors are Samsung/Yageo/Murata 0805, with C77102 (10 µF 50 V 1210) and C12891 (22 µF 25 V 1206). All carry supplier numbers in the schematic.

## 16. Stage 2 remaining unknowns

| Item | Result |
|---|---|
| HT7533-1 | Closed: Iq 2.5 µA typical, 4 µA maximum [ds] |
| SGM2212 | Closed: D19, D20. Read from a distributor-hosted copy of the datasheet |
| Shunt | Closed: C42453896 explicitly lists 100 mΩ 2 W |
| WS2812B-2020 | Closed: D21 |
| ESP32-S3 GPIO restrictions | Closed: GPIO35–37 and strapping pins 0, 3, 45, 46 confirmed in the WROOM-1 datasheet. GPIO26–32 not re-read; they are not exposed on the symbol |
| Weak-sun restart on LFP | Open, bench test |
| USB and DC-5 V rail figures | Open, bench test |

## 17. Schematic

Five pages in `Air360_V2.2`: power, UVLO, core, interfaces, connectors. Every connected pin has a named net stub. Page uuids change only if pages are recreated: power `832322dde21e9463`, UVLO `7ca8a5ed4c92187a`, core `639e38c8049a27e4`, interfaces `c211ae6ae66dcfec`, connectors `225f56858bcee42c`.

### Verification, 2026-11-04

- **Netlist:** exported from EasyEDA and compared pin by pin with the design table: 112 components, 78 nets, 0 mismatches, no single-pin nets. Cross-page nets connect by name.
- **Visual:** all pages rendered and inspected. An earlier layout had vertical stubs touching neighbouring wires, which created real shorts; it was rebuilt and the netlist check above is from the final version.
- **ERC:** clean. The one warning listed 16 floating pins, all from the intended list below. They are now marked No Connect, the rerun returned no findings, and the netlist comparison still gives 0 mismatches.

### Pins marked No Connect

U2 PG; U1 IO3, IO45, IO46, IO35–37; JP1 3–4 (LiFePO4 parking position); J1 SBU1–2; LED1 DO; SW1 and SW2 pins 2–3.

Buttons use diagonal pins 1 and 4, which are across the switch whichever way the internal pairs run.

### To check in stage 4 (footprints)

- Polarity of D2, D3, D4 and C2, C13 at the pad level; the symbols are correct.
- J3 pin 1 is the centre contact.
- SPS30 and SCD30 pin 1 position against the shield.
- USB-C and module footprints.

## 18. Rules and mechanics

| Item | Value | Basis |
|---|---|---|
| Fabrication | JLCPCB, 4 layers, 1.6 mm, FR-4; 1 oz outer, 0.5 oz inner | Target; stack-up to be chosen at order time |
| Layers | Top parts and signals; Inner1 GND; Inner2 power; Bottom signals | |
| Minimum track / spacing | 0.15 mm track; spacing 0.10 mm track to track, 0.15 mm otherwise | Active EasyEDA JLCPCB rule set, as reported by DRC; corrected in stage 7 |
| Via | 0.3 mm drill, 0.6 mm pad | |
| Power track | 1.5 mm or copper pour | IPC-2221 at 3.3 A, 1 oz outer: 1.56 mm for 10 °C rise, 1.0 mm for 20 °C [calc] |
| USB | 0.3 mm track, 0.2 mm gap | Full speed only; length-matched and short rather than impedance-controlled |
| Net class | `POWER`: DC_IN, DC_F, V_PROT, V_BUCK, BUCK_5V, SW, +5V, VBUS, VBUS_F, +5V_M, +5V_S | Created in EasyEDA |
| Differential pair | `USB`: USB_DP, USB_DM | Created in EasyEDA |
| Board | 81.28 × 85 mm, corner radius 3 mm | Same width as the shield; 34.2 mm taller |
| Mounting holes | Four at the shield's 74.8 × 44.47 mm pattern, two added at the new top corners; 3.03 mm non-plated through holes | User: keep the shield holes; D26 |

Footprints audited on imported pads and in the editor: J3 pad 1 is the rear (centre) contact; cathode marks on D2, D3, D4 and D8 are at pad 1; plus marks on C2 and C13 are at pad 1; SGM2212 TO-252 pinout matches its datasheet (1 GND, 2 VOUT and tab, 3 VIN); LMR33640, INA219, TLV3011 and TPS3808 match their datasheets.

3D models have not been inspected yet; that belongs to stage 5.

Connector positions in millimetres, shield frame (origin at bottom left, connector edge at the bottom): MODEM x 31.115; UART2 x 38.227; GPIO x 43.434, 46.990, 50.546; I2C x 55.880, 59.436, 62.992. Pin 1 of each header is at y 16.51. SPS30 row y 14.986, x 68.628–74.628. SCD30 row y 7.366, x 68.616–73.116. DC jack J3 moved 1 mm right of the shield position so an M3 screw head clears it at H1 (user decision, D31), opening to the bottom edge at x about 10.3; KF301 at x 20.47 and 25.47, y 7.

## 19. Placement

Functional groups on the top side, millimetres:

| Group | Area |
|---|---|
| Connectors, shield positions | y < 18 |
| DC input: J3, J4, F1, Q3, D3, D4, R1 | x 5–29, y 1–26 |
| Current monitor: RS1, U4, R2, R3, C1, C2 | x 5–29, y 23–40 |
| Buck: U2, C3–C7, L1, C8–C11 | x 3–39, y 40–63 |
| UVLO: U5–U7 and passives | x 2–18, y 59–84 |
| USB: J1, D1, F2, Q2, Q5, R8, D2, R17, R18 | x 19–41, y 61–85 |
| Source switching Q1, Q4, R6, R7, C12 and bulk C13 | x 25–50, y 39–52 |
| 3V3: U3, C14–C16 | x 42–68, y 51–59 |
| ESP32 at the top edge | x 48.5–67.5, y 58.8–84.9 |
| LED1, C27, R19 | x 68–79, y 51–59 |
| Modem, UART2 resistors | x 30–43, y 24–40 |
| GPIO protection in rows; I2C pull-ups R20, R21 | x 43–71, y 23–41 |

Layout revised after the user's second review, keeping their moves of the buck and UVLO blocks. C5, C6 and C7 had drifted 2–4 mm from U2 in that edit and were put back at their pins: C5 at pins 1 and 2, C6 at pins 7 and 8, C7 at pin 6 (rotated 180°). JP1 labels moved with JP1.

All nine header and ZH connector positions were read back from pad coordinates and match section 12. Footprint bounding boxes, which include silkscreen, were checked for overlap.

Import does not replace a footprint when a symbol's device changes; JP1 and LED1 had to be placed on the PCB by hand with pad nets set through the API, and LED1 has no matching unique ID. TP1–TP3 were placed the same way, with unique IDs set; importing did not bring them over. Re-check these, and the 1.5 mm pads of TP1–TP3, after any later import, and use Update PCB in the EasyEDA UI if a sync is needed.

Verified after the second review, on read-back from EasyEDA: 111 components, pad nets identical to the previous state, connectors, U1, J1 and J2 not moved, outline gaps at least 0.5 mm by bounding box and at least 0.75 mm except C5–U2, C6–L1, C6–C7, C13–Q4 and Q1–R7, 111 designators with no conflict against outlines, holes or each other (by estimated text box). The schematic was not changed in this pass, so the earlier ERC and netlist results stand; Holes: see D26. A 3 mm radius around each hole is clear of component outlines for an M3 screw head, after D31. LED1, C27, R16 and J4 were afterwards moved by the user.

Not done in this stage: antenna keep-out regions, which are created with routing.

## 20. Routing

Layers: Top and Bottom carry signals and a GND pour; Inner1 is a GND pour; Inner2 holds V_BUCK (left), +5V (centre) and 3V3 (right) pours, plus a short VBUS track behind J1. `ANT_KEEPOUT` covers x 47.6–70.5, y 77.8–85 on all layers.

Routed by hand: DC_IN, DC_F, V_PROT main path, Kelvin sense from RS1 to U4, SW, BOOT, VCC_B, BUCK_5V copper, +5V and 3V3 fan-out to Inner2, VBUS, VBUS_F, USB_DP, USB_DM, connector rails. The remaining 49 signal nets used the EasyEDA autorouter on Top and Bottom. 51 GND vias were placed by script with a clearance check; U6 pin 2 got none and relies on the Top pour.

USB-C needs vias between 0.5 mm pads: `USB_DM` uses 0.3/0.5 mm vias at J1, and the A and B pads are tied there.

State on 2026-11-04: pours are filled in the EasyEDA UI, because the API call returns nothing, so any copper edit needs Shift+B there. Final DRC returned an empty list, including connection and netlist checks. ERC is clean; schematic netlist 115 components, 66 nets, 0 mismatches; Unique IDs match between schematic and PCB. 662 tracks, 161 vias, 6 pours. STEP export shows the six 3.03 mm holes. A test Gerber export has all layers.

Not physically verified: nothing here was built or measured. Stage 7 still has to check layer content, mechanics and antenna region independently of EasyEDA's DRC.

The autorouter's RPC reports a timeout but commits its tracks.

EN_NODE is a 100 kΩ high-impedance node, so it was moved off the path between the L1 pads. FB sense no longer starts at L1 pad 2 or runs under L1; it taps BUCK_5V at the output side. `I2C_SDA` still runs on Bottom under L1, with Inner1 GND between them.

Netlist Error: Import Changes offered to remove and re-add ten connectors because their schematic Unique IDs had changed to gge197–gge206 while the PCB kept the old ones. Applying it would have displaced the shield connectors. The PCB IDs were rewritten to match, without moving anything. Always read that dialog before applying it.

## 21. Final verification

Independent check: the Gerber copper layers are rasterized at 0.05 mm, joined through the drill data, and compared with pad nets, without using EasyEDA's DRC.

| Check | Result | State |
|---|---|---|
| ERC, schematic netlist against design model | Clean; 115 components, 66 nets, 0 mismatches | Verified |
| EasyEDA DRC | Empty list, but it missed the TP3 short below | Verified, not sufficient alone |
| Gerber connectivity, opens | 0 of 66 nets | Verified on the package Gerber |
| Gerber connectivity, shorts | 1 found: TP3 (GND) on EN_NODE; 0 after the fix | Verified on the package Gerber |
| Copper gaps by raster | About 0.14 mm minimum, at J1 vias; resolution 0.05 mm, rule 0.15 mm | Verified after the fix |
| Copper in antenna zone | None on four layers, x 49–69, y 78.3–85 | Verified |
| Connector pins and positions | Match the requirements pin table and shield positions | Verified |
| Outline and holes | 81.28 × 85 mm; six 3.03 mm NPTH | Verified |
| Silkscreen on exposed copper | 10 px in 6 specks, no text; macro pads not counted | Partial |

Inner2 narrowest widths: V_BUCK 4.2 mm, +5V 4.0 mm, 3V3 4.2 mm. For 0.5 oz inner copper, IPC-2221 gives about 2.1 mm for the 1.23 A continuous budget at 10 °C rise, and about 5.2 mm for 3.3 A at 20 °C rise [calc]. Continuous load has margin. The 3.3 A peak is short and is above the 10 °C figure at these necks, which is accepted because the budget sums peaks as simultaneous.

TP3 cause: its pad was restored to 1.5 mm after routing, overlapping EN_NODE, and EasyEDA DRC did not report it. TP3 moved to x 22.4, y 60. A later check found component pad edits are lost on reload, so the final 1.5 mm comes from separate pads (D33), confirmed in the Gerber raster.

LED1: pinout matches the SK6812MINI-HS datasheet numbering (1 GND, 2 DIN, 3 VDD, 4 DOUT). Its rated VDD is 3.7–5.5 V, so 3V3 is below rating. The ESP32-S3-DevKitC-1 v1.1 schematic also ties VDD to `VCC_3V3`, so D27 stands; confirm brightness and colour on hardware. Footprint orientation against the package drawing was not checked: the drawing is vector art and was not rendered.

Hardware-only, not verified: UVLO thresholds and restart, all power modes, buck stability and ripple, INA219 reading, thermal rise, Wi-Fi range near SW1, real cable fit.

## 23. V2.2 design

Project `Air360_V2.2`, UUID `03cfb60824c0433cafe2a8958e9efd4c`; PCB `93ecb0b02681a8ec`; schematic `cdf4394e2fb2e7ce`. Schematic sheets and routing were rebuilt from the earlier placement. Connector positions, outline and holes did not change.

| Item | V2.2 |
|---|---|
| U2 | AP63205WU-7, C2071056 |
| L1 | SWPA6045S4R7MT 4.7 µH, Isat 5.5 A, C78804 |
| Output caps | C8, C9, 2 × 22 µF, per AP63205 datasheet |
| Input | C3 10 µF 25 V 1206, C9807; C5 100 nF |
| Q1–Q3 | AO3401A, C15127 |
| RS1 | RL1206FR-070R1L 0.1 Ω 0.25 W, C364357 |
| F1, F2 | SMD1210P150TF/16, 1.5 A, C7529589 |
| F3, F4 | SMD1206P110TF/16, 1.1 A, C46970781 |
| U3 | AMS1117-3.3, C6186 |
| C2, C13 | RVT1C221M0607 220 µF 16 V, C3342 |
| D3 | SMAJ15A, C132582 |
| JP1 | 1 × 2: fitted Li-ion, open LiFePO4 |

Inner2 holds +5V and 3V3 pours. V_BUCK is a Top track. Thresholds follow section 13 because R9–R14 are unchanged.

[calc] RS1 dissipates 0.225 W at 1.5 A against a 0.25 W rating, which is little margin. L1 ripple is about 0.39 A at 8.4 V and 1.1 MHz; peak stays under the 2.8 A switch limit and the 5.5 A Isat. U3 loses 0.43 W at 250 mA from 5 V; SOT-223 thermal rise is to be measured. +5V current limits are set by F3 and F4 at 1.1 A hold and U2 at 2 A; a modem with high bursts may need its own supply.

U3 pin 2 needs a board connection to the tab. Signals run under the body, so pin 2 reaches the Inner2 3V3 pour through a via in its pad.

## 24. V2.2 verification

Run on the Gerber in `release/v2.2/`, after the final copper rebuild.

| Check | Result |
|---|---|
| ERC; schematic netlist against design model | Clean; 109 components, 64 nets, 0 mismatches |
| EasyEDA DRC | Empty list |
| Schematic against PCB | 0 Unique ID and 0 pin net differences |
| Gerber connectivity | 0 opens, 0 shorts, 64 nets; found the open on U3 pin 2 before the fix |
| Copper gaps by raster | About 0.14 mm minimum, at J1 vias; rule 0.15 mm |
| Copper in antenna zone | None on four layers |
| Left and right margins | 4.40 mm and 4.38 mm |
| TP1 pad in Gerber | 1.5 mm; TP2 and TP3 merge with their copper there |
| BOM and placement | 106 parts, 106 rows, TP1–TP3 excluded |
| Schematic PDF | Real PDF, five pages, no doubled headings |

Not verified: anything physical; 3D inspection by the user; plane necks and thermal rise on V2.2.

## 25. U5 output stage defect, 2026-11-04

U5 TLV3011 has an open-drain output and `UV_OK` had no pull-up, so `UV_OK` could not go high and the buck would never enable from DC. ERC, DRC and Gerber checks passed because they only confirm connections; the error was found when the user asked for a cheaper part. V2.2 now uses TLV3012AIDBVR, C62505, same pinout, push-pull output, rotated 90° on the PCB because the library footprint is numbered differently.

The same review also changed C16 to 22 µF, CL21A226MAQNNNE, C45783, same 0805 pads: the AMS1117 datasheet names 22 µF on the output for stability, though as solid tantalum, so ceramic stability needs a bench check.

Logic reviewed on paper for V2.2: U5 to U6 to `EN_NODE` to U2 EN, hysteresis through D8, source switching Q1, Q2, Q4, Q5 in both modes, Q3 gate limit, INA219 address, ESP32 strapping and reset, USB CC. No other logic fault found. Not checked on hardware.

Final state on the package Gerber: EasyEDA DRC empty, ERC clean, 0 opens and 0 shorts on 64 nets, 0 Unique ID and pin net differences, BOM 106 parts, placement 106 rows.

## 26. V2.2 button labels, 2026-11-07

SW1 connects to `CHIP_PU` and SW2 to `IO0`, so their silkscreen now reads `RESET` and `BOOT`. Each text sits at the old designator position, with the same 32 mil size and 6 mil line, on the top silkscreen. It keeps the D28 clearances: about 0.3 mm from the switch outline at `RESET`.

The package was exported again from the live project, which already held later moves of silkscreen designators (J1, J3, GPIO1–3 and others). The new top silkscreen therefore differs from the 2026-11-04 package in those positions as well.

Compared with the 2026-11-04 package: inner layers, bottom copper, outline and drill positions match; top copper differs only by rounding of about 0.1 mil on the pour outline; BOM unchanged; schematic text unchanged on all five sheets; placement unchanged apart from 0.001 mm rounding on U5 and C16. EasyEDA DRC is empty. The new Gerber shows `RESET` and `BOOT`, and they survive closing and reopening the PCB.

Renders in `docs/images/` come from the EasyEDA 3D Preview with purple board color. The EasyEDA background stays black because the setting did not reach the 3D scene.
