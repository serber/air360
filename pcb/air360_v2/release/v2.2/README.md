# Release v2.2


Cost-down prototype package exported from EasyEDA Pro project `Air360_V2.2` on 2026-11-07, all from one revision. Nothing in this package has been built or measured. `../../DESIGN_LOG.md` sections 23–26 hold the design notes and checks. `../release/v2.1/` is the fallback design.

| File | Content |
|---|---|
| `gerber-v2.2.zip` | Gerber and drill files, 4 layers |
| `BOM.md` | Parts list with order links |
| `bom-v2.2.csv` | EasyEDA BOM export, 106 parts in 54 lines, tab-separated UTF-16 |
| `placement-v2.2.csv` | 106 placements, top side, TP1–TP3 removed |
| `schematic-v2.2.pdf` | All five schematic sheets |
| `project-v2.2.epro2` | EasyEDA Pro project |
| `Air360_V2.2.step` | Board and component 3D models. Not in git because of its size; export it from EasyEDA |
| `FIRST_POWER_UP.md` | First power-up procedure and threshold table |
| `TEST_PLAN.md` | Post-assembly test plan |
| `SHA256SUMS` | Checksums |

## Ordering parameters

| Parameter | Value |
|---|---|
| Size | 81.28 × 85 mm, corner radius 3 mm |
| Layers | 4: Top, Inner1 GND, Inner2 power, Bottom |
| Thickness, material | 1.6 mm, FR-4 |
| Copper | 1 oz outer, 0.5 oz inner |
| Minimum track / space | 0.15 mm / 0.10 mm |
| Via | 0.3 mm drill, 0.5–0.6 mm pad, tented |
| Non-plated holes | Six 3.03 mm mounting, two 0.6 mm USB-C pegs |
| Impedance control | Not required; USB is full speed |
| Surface finish | ENIG preferred, for the USB-C pads |
| Assembly | Top side only |

The order instruction is in the [board README](../../README.md#ordering). It covers bare boards ordered by email.

- **Order number:** the bottom silkscreen layer is empty and the top one is dense, so ask for the number on the bottom side.
- **Finish:** ENIG is preferred, not required. HASL is cheaper but leaves the fine USB-C and U1 pads uneven, which makes J1 harder to solder.
- **Stack-up:** any standard one with the copper weights above fits, because impedance is not controlled.
- **Soldermask:** confirm that purple is available for 4 layers with ENIG. This was not checked.
- **Stencil:** for hot-air or oven soldering, add `+ SMT stencil (top side only)`. Paste data exists for the top side only.
- **Assembly:** a factory assembly order also needs the BOM, the pick-and-place file and the items under "Before ordering".

## Before ordering

- **Stock and price:** LCSC numbers were not checked for stock or price.
- **Headers:** GPIO1–3, I2C1–3, UART2, MODEM, J2 and JP1 have no LCSC number in the BOM. JP1 is 1×2 and needs one 2.54 mm jumper cap.
- **Placement review:** check rotations in the assembler's preview, especially U1, J1, LED1 and all polarized parts. LED1 footprint orientation was not compared with the package drawing.
- **Via in pad:** U3 pin 2 has a via inside its pad. Tell the assembler, and check the joint if soldering by hand.
- **Through-hole parts:** J1, J3, J4, SPS30 and SCD30 are not plain SMD.

## Keep in mind

- DC takes a 2S battery only. 5 V goes through USB-C. 5 V at DC does not start the board.
- SW1 and SW2 are marked `RESET` and `BOOT` on the silkscreen. The BOM, placement file and schematic keep the designators SW1 and SW2.
- J3 is 1 mm right of the shield position. Check any enclosure cut-out.
- Connect J3 or J4, not both.
- +5V is designed for 1–1.5 A. A modem with high transmit bursts may need its own supply.
- In EasyEDA, read the Import Changes dialog before applying it.
- After any copper edit, rebuild copper with Shift+B and export again. Separate pads `TP1P`–`TP3P` give the test points their 1.5 mm size.
