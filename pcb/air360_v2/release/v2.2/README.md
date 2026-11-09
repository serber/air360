# Release v2.2

Prototype, exported from EasyEDA Pro project `Air360_V2.2` on 2026-11-07. BOM, placement, Gerber and project exported again on 2026-11-09 after the passives change; Gerber content unchanged. **Not validated and not ordered.**

| File | Content |
|---|---|
| `gerber-v2.2.zip` | Gerber and drill, 4 layers. Upload this to the fab |
| `BOM.md` | Parts list with order links |
| `bom-v2.2.csv` | EasyEDA BOM export, 106 parts, UTF-16 |
| `placement-v2.2.csv` | Part positions, 106 parts, top side, TP1–TP3 removed |
| `schematic-v2.2.pdf` | Schematic, five sheets |
| `project-v2.2.epro2` | EasyEDA Pro project |
| `SHA256SUMS` | Checksums |
| `assembly-v2.2.step` | 3D assembly, local only; export it from EasyEDA if missing |

The order instruction is in the [board README](../../README.md#ordering).

## Checks on these files

- ERC clean; schematic netlist against the design model: 109 components, 64 nets, 0 mismatches.
- EasyEDA DRC empty. Gerber connectivity: 0 opens, 0 shorts on 64 nets.
- Gerber and drill identical to the 2026-11-07 package within rounding.

## Not verified

- Placement rotations in the assembler's preview, especially U1, J1, LED1 and polarized parts. LED1 footprint orientation was not compared with the package drawing.
- Purple solder mask availability for 4 layers with ENIG.
- Everything physical: [`docs/DESIGN-RECORD.md`](../../docs/DESIGN-RECORD.md#bring-up).
