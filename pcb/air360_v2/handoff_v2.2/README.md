# Air360 V2.2 manufacturing package


Cost-down prototype package exported from EasyEDA Pro project `Air360_V2.2` on 2026-11-04, all from one revision. Nothing in this package has been built or measured. `../DESIGN_LOG.md` sections 23–24 hold the design notes and checks. `../handoff_v2.1/` is the fallback design.

| File | Content |
|---|---|
| `Air360_V2.2_Gerber.zip` | Gerber and drill files, 4 layers |
| `Air360_V2.2_BOM.csv` | 106 parts in 54 lines, tab-separated UTF-16 |
| `Air360_V2.2_PickAndPlace.csv` | 106 placements, top side, TP1–TP3 removed |
| `Air360_V2.2_Schematic.pdf` | All five schematic sheets |
| `Air360_V2.2.epro2` | EasyEDA Pro project |
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

Order line:

```
5PCS, 4 Layers, FR4, 1.6MM, Outer 1OZ, Inner 0.5OZ, Purple Soldermask, White Legend, ENIG, Tented Vias, No Impedance Control, 81.28x85MM, order number will be added on bottom side.
```

## Before ordering

- **Stock and price:** LCSC numbers were not checked for stock or price.
- **Stack-up:** no specific stack-up has been chosen. Pick one that matches the copper weights above.
- **Headers:** GPIO1–3, I2C1–3, UART2, MODEM, J2 and JP1 have no LCSC number in the BOM. JP1 is 1×2 and needs one 2.54 mm jumper cap.
- **Placement review:** check rotations in the assembler's preview, especially U1, J1, LED1 and all polarized parts. LED1 footprint orientation was not compared with the package drawing.
- **Via in pad:** U3 pin 2 has a via inside its pad. Tell the assembler, and check the joint if soldering by hand.
- **Through-hole parts:** J1, J3, J4, SPS30 and SCD30 are not plain SMD.

## Keep in mind

- DC takes a 2S battery only. 5 V goes through USB-C. 5 V at DC does not start the board.
- J3 is 1 mm right of the shield position. Check any enclosure cut-out.
- Connect J3 or J4, not both.
- +5V is designed for 1–1.5 A. A modem with high transmit bursts may need its own supply.
- In EasyEDA, read the Import Changes dialog before applying it.
- After any copper edit, rebuild copper with Shift+B and export again. Separate pads `TP1P`–`TP3P` give the test points their 1.5 mm size.
