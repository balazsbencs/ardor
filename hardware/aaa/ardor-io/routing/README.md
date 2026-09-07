# Compact routed PCB

Open `../Ardor_IO.kicad_pro` and `../Ardor_IO.kicad_pcb` in KiCad 9.

- Board outline: **90 × 64 mm**, two copper layers; **28% less area** than the original 100 × 80 mm placement.
- All 95 schematic components and their electrical connections are retained. The four M3 enclosure holes now have centers at (4, 4), (86, 4), (4, 60), (86, 60) mm from the upper-left corner. These are provisional enclosure mounts, not a verified Pi/HAT hole pattern.
- Pi header orientation/pin order and panel harness definitions are retained. Check Pi/Codec Zero clearance, header stack height, enclosure and harness access against actual hardware before manufacture.
- Short local op-amp feedback and headphone charge-pump connections precede local Freerouting 2.4.1 routing. Both copper layers have GND pours, with stitching vias. The MIDI input island excludes GND pour and retains the original optocoupler routing isolation corridor.
- Signal routing is nominally 0.20 mm with local 0.15 mm neckdowns; selected power/ground routes are 0.40 mm, headphone/pump routes up to 0.30 mm, and CHASSIS routes up to 0.60 mm. Widths narrow where needed to maintain clearance. Minimum clearance is 0.15 mm. Routing vias are 0.60/0.30 mm; the original U601 footprint also has 0.20 mm thermal drills.

## Labels and wiring

Front silkscreen identifies connector functions and physical pin order. References remain on F.Fab for assembly. The pin numbers below refer to the board headers, not unspecified panel jack footprints.

| Header | Pin order | Board legend |
|---|---|---|
| J101 | Original 40-pin Pi interface | PI / HOST, pins 1 and 40 |
| J102 | 1 AUX L, 2 GND, 3 AUX R | CODEC AUX, L G R |
| J202 | 1 DIN contact 4, 2 DIN contact 5 | MIDI IN, 4 / 5 |
| J302 | 1 tip, 2 ring, 3 sleeve/GND | EXPRESSION, T / R / S |
| J503 | 1 tip/signal, 2 sleeve/GND | LINE OUT, T / S |
| J502 | 1 amp signal, 2 GND | AMP, SIG / G |
| J602 | 1 left/tip, 2 right/ring, 3 GND/sleeve | PHONES, L / R / G |

JP301 has a POLARITY label and a pin-1 marker. Back silkscreen states both shunt settings: normal 1–3 and 2–4; reversed 3–5 and 4–6.

Testpoints are labeled 5V A (TP101), 3V3 ADC (TP102), GND (TP103), MIDI RX (TP201), EXP ADC (TP301), VREF (TP401), MONO (TP402), LINE (TP501), and HP L (TP601).

## Review and verification

- `front.png` / `front.svg`: top copper and silkscreen preview.
- `back.png` / `back.svg`: mirrored bottom copper and silkscreen preview.
- `placement.csv`: component positions measured from the upper-left outline corner.
- `drc.json`: KiCad 9.0.2 check with all track errors, schematic parity and all severities enabled. **Zero violations, zero unconnected items, zero schematic-parity issues.** No DRC exclusions were added.
- `connectivity-audit.json`: independent comparison of all **276 numbered pad/net assignments** against the supplied schematic netlist, including all 95 electrical components.
- `compact.dsn` / `compact.ses`: initial local routing exchange files. The final KiCad board additionally contains ground pours, accepted stitching vias, widened traces and final legends.

The board is electrically checked in CAD, not bench-qualified. The existing analog/audio, MIDI, ESD and assembly release checks in `../DESIGN_NOTES.md` still apply.

## Rebuilding (overwrites board routing)

The scripts require KiCad 9's Python `pcbnew` and wx modules. A headless machine can use `xvfb-run -a python3`. Keep a copy of the board before rebuilding.

1. `design/compact_pcb.py` resets placement, drawings, zones and routing, using the existing embedded footprints.
2. `design/prepare_routing.py` adds critical local routes and exports `compact.dsn`. It omits the pour-only keepout from DSN because KiCad 9 exports it as a routing keepout; the actual board retains it.
3. Run a local Freerouting 2.4.1 installation: `freerouting --gui.enabled=false -de routing/compact.dsn -do routing/compact.ses -mt 4 -mp 40` from the project directory.
4. `design/finish_routing.py` imports the session and fills front/back GND pours.
5. `design/refine_copper.py` widens candidate traces, adds candidate stitching vias and snapshots package libraries. Run KiCad DRC to `routing/refine-drc.json`, then `design/check_copper.py` to reject conflicting candidates. Recheck; repeat only if needed, using the latest report.
6. Run the final check below, then `design/verify_pcb.py`. Regenerate previews after changes.

```sh
kicad-cli pcb drc --schematic-parity --all-track-errors --severity-all \
  --exit-code-violations --format json -o routing/drc.json Ardor_IO.kicad_pcb
python3 design/verify_pcb.py
```
