# CLAUDE.md

## Project

Air360 V2.2: a 4-layer, 81.28 × 85 mm ESP32-S3 board that replaces the shield plus DevKit. It must run the existing firmware unchanged: GPIO assignments and connector pin order in `docs/SPEC.md` section 7 are fixed.

## Where things live

The design is in EasyEDA Pro, project `Air360_V2.2`. `Air360_V2` is a failed attempt, not a reference.

| Document | UUID |
|---|---|
| Project `Air360_V2.2` | `03cfb60824c0433cafe2a8958e9efd4c` |
| PCB `PCB1` | `93ecb0b02681a8ec` |
| Schematic `schematic1` | `cdf4394e2fb2e7ce` |
| Pages power, connectors, uvlo, core, interfaces | `832322dde21e9463`, `225f56858bcee42c`, `7ca8a5ed4c92187a`, `639e38c8049a27e4`, `c211ae6ae66dcfec` |

- `docs/SPEC.md`: requirements.
- `docs/DESIGN-RECORD.md`: decisions, calculations, verification, bring-up.
- `release/v2.2/`: ordering package, plus the schematic PDF and the `.epro2` project.

When a record and the CAD disagree, read the CAD. Update the records after any board change.

## Working rules

- Reply in Russian. Write all files and EasyEDA content in English.
- Follow the `power-pcb-design` skill. Log decisions in `docs/DESIGN-RECORD.md`.
- Never order or buy without explicit permission.
- State whether a result was calculated, checked in CAD or measured. Nothing has been measured.

## EasyEDA

Use the `easyeda-api` skill; the bridge runs on `127.0.0.1:49620`. PCB units are mil.

- Read the Import Changes dialog before applying it. It once offered to remove and re-add ten connectors because their schematic Unique IDs had changed.
- `rebuildCopperRegion()` returns nothing here: rebuild pours with Shift+B in the UI after any copper edit, then export again.
- Pad sizes edited on a component are lost on reload. TP1–TP3 get their 1.5 mm size from separate pads `TP1P`–`TP3P`; check them after any import.
- Import does not replace a footprint when a symbol's device changes; place it by hand and set pad nets.
- The autorouter's RPC reports a timeout but commits its tracks.
- EasyEDA DRC once missed a short (TP3 on `EN_NODE`). Check Gerber connectivity independently before release.
