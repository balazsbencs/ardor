# Generate and verify T1 hand-assembly projects

Use KiCad 9.0.2, Python 3 with `pcbnew`, `wx`, `sexpdata`, `shapely`, `cairosvg`, `pymupdf`, and `xvfb-run`. Routing additionally uses Freerouting 2.1.0 and Java 21. Run in a copy: generation replaces the five T1 projects under this directory's parent. It never writes the M1 projects or synced `sources/` files.

```sh
python3 modules.py
python3 prepare_libraries.py
python3 check_wiring.py
python3 export_schematics.py
xvfb-run -a python3 build_pcbs.py
# After circuit or placement changes; otherwise reuse frozen routing/board.ses:
python3 route.py /absolute/path/to/freerouting-2.1.0.jar
python3 finish_routes.py
python3 verify.py
python3 verify_hand_assembly.py
python3 verify_headphones.py
python3 export_manufacturing.py
python3 generate_guides.py
python3 render_review.py
python3 verify_release.py
```

Native ERC/DRC include all severities, unconnected items and schematic parity, with no exclusions. Independent drawn-wire checks, netlist-to-pad checks, connector comparison against M1, strict SMD-exception checks (only headphone U601 SOIC-8), BOM checks and Gerber/drill/archive manifests support review. Frozen footprints and symbols make the generated projects self-contained.

The hand-assembly check also verifies all six DC Components 1.5KE6.8CA replacements against their purchasing specification, 15.24 mm pad spacing, minimum 1.3 mm holes and maximum 9.5 × 5.6 mm body envelope. MIDI D203/D204 must retain their SA24CA voltage rating and original package.

`smd_circuits.py` and `part-catalog.json` retain the M1 circuit/value source for the four reused circuits. `modules.py` replaces their packages, purchasing metadata and relay transistor. The new headphone circuit is specified directly there. M1 routing and manufacturing data are not reused. IC replacements and the ADC breakout pin numbering are documented in `../SOURCES.md`.

Run `finish_routes.py` on freshly generated placement boards: it imports the candidate routes, fills/stitches ground, removes only DRC-proven dangling leaves, restores uniform track widths and adds both component and connector legends. Repeatedly adding legends to an already labelled board is not supported. Inspect layout and prototype behavior after any change; CAD checks do not establish electrical performance.

For the release regeneration review, run this workflow in a temporary copy using the frozen route sessions. Then, from the original `design/` directory, run `python3 verify_regeneration.py /absolute/path/to/copied/ardor-modules-tht`, followed by `python3 verify_release.py` to refresh the manifests. The comparison covers exact schematic/BOM/guide files, PCB geometry and nets, and filled copper shapes while allowing new board UUIDs and equivalent polygon vertex ordering.
