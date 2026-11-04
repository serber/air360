# Air360 V2.1 manufacturing package

> **Do not order assembly or parts from this BOM yet.** U5 TLV3011 is open-drain with no pull-up on `UV_OK`, so the board would not start from DC. See `../DESIGN_LOG.md` section 25. The Gerber files are not affected.

Prototype package exported from EasyEDA Pro project `Air360_V2.1` on 2026-11-04, all from one revision, including the buck indicator LED2. Nothing in this package has been built or measured. `../DESIGN_LOG.md` is the design record; section 21 holds the verification results.

| File | Content |
|---|---|
| `Air360_V2.1_Gerber.zip` | Gerber and drill files, 4 layers |
| `Air360_V2.1_BOM.csv` | 112 parts in 56 lines, tab-separated UTF-16 |
| `Air360_V2.1_PickAndPlace.csv` | 112 placements, top side, TP1–TP3 removed |
| `Air360_V2.1_Schematic.pdf` | All five schematic sheets |
| `Air360_V2.1.epro2` | EasyEDA Pro project |
| `Air360_V2.1.step` | Board and component 3D models. Not in git because of its size; export it from EasyEDA |
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
| Surface finish | ENIG preferred, for the ESOP-8 and USB-C pads |
| Assembly | Top side only |

## Before ordering

- **Stock and price:** LCSC numbers were not checked for stock or price.
- **Stack-up:** no specific JLCPCB stack-up has been chosen. Pick one that matches the copper weights above.
- **Headers:** GPIO1–3, I2C1–3, UART2, MODEM, J2 and JP1 have no LCSC number in the BOM. Choose 2.54 mm headers or solder them by hand. JP1 also needs one 2.54 mm jumper cap.
- **Placement review:** check rotations in the assembler's preview, especially U1, J1, LED1 and all polarized parts. LED1 footprint orientation was not compared with the package drawing.
- **Through-hole parts:** C2, J1, J3, J4, SPS30 and SCD30 are not plain SMD. Confirm how the assembler handles them.

## Keep in mind

- J3 is 1 mm right of the shield position (D31). Check any enclosure cut-out.
- Connect J3 or J4, not both. They are in parallel with no isolation between them.
- In EasyEDA, read the Import Changes dialog before applying it. It can offer to remove and re-add placed connectors.
- After any copper edit in EasyEDA, rebuild copper with Shift+B and export again.
- The 1.5 mm test pads are separate pads `TP1P`–`TP3P` over TP1–TP3. Editing a component pad does not survive a reload, so do not rely on that.
- The schematic PDF still shows doubled block headings and an old `Debug and spare GPIO` heading. These are text only; nets are not affected.
