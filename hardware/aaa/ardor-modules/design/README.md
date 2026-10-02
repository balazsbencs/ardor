# CAD generation and release checks

Use **KiCad 9.0.2**, Python 3 with `pcbnew`, `wx`, `sexpdata`, `shapely`, and `xvfb-run`. Host rendering also needs `cairosvg` and `pymupdf`. The checked-in projects, libraries and fabrication files are usable directly; generation does not require another Ardor project. Run in a copy: generation overwrites these five module directories. Scripts discover them via their own parent path.

## Verify existing CAD

From this directory, with KiCad on PATH:

```sh
python3 check_wiring.py
python3 export_schematics.py
python3 drc.py
python3 verify.py
python3 export_assembly.py
python3 export_manufacturing.py
python3 render_review.py
python3 verify_packages.py
```

The schematic check independently walks the drawn wires and labels, then compares pin partitions with intent. Native XML export also compares every pin and explicit NC. Native ERC/DRC includes all severities, opens and schematic parity, with no exclusions. Physical verification checks every numbered pad/net, mounting holes, board size, local libraries, two filled GND planes and every track/via width. Package checks compare native placement exports with BOM/CPL, Gerber/drill coordinates and archive contents. SHA256 manifests tie CAD and manufacturing artifacts together.

## Rebuild without changing approved routing

```sh
python3 modules.py
python3 check_wiring.py
python3 export_schematics.py
xvfb-run -a python3 build_pcbs.py
python3 finish_routes.py
python3 verify.py
python3 export_assembly.py
python3 export_manufacturing.py
python3 render_review.py
python3 verify_packages.py
```

This imports the frozen `routing/board.ses` candidates into newly generated placement boards. The importer preserves the manual SOIC feedback and headphone charge-pump loops, fills native GND zones, reinstates the MIDI keepout/rules, connects ground islands with independently checked stitching, trims only DRC-reported dangling leaves while requiring all real connections intact, removes width neckdowns, and adds back connector legends. Final native DRC is mandatory. Do not run `label_boards.py` twice on an already labelled board; it adds text to a fresh generated board.

`build_pcbs.py` creates board-only NPTH mounts and copies local library footprints. KiCad can overwrite project settings during native saves, so every mutation restores `verification/project-config.json`. Each pcbnew mutation uses a fresh wx-initialized process; batch native object deletion is avoided.

## Reroute after a circuit or placement change

Insert this after `build_pcbs.py` and before `finish_routes.py`:

```sh
python3 route.py /absolute/path/to/freerouting-2.1.0.jar
```

Freerouting 2.1.0 with Java 21 produces candidates at 0.2 mm ordinary / 0.6 mm CHASSIS widths and skips ground routing. The native KiCad DSN exporter in 9.0.2 asserts on a DIP silkscreen notch arc, so only non-copper arcs are removed in its temporary export copy; final footprint geometry is unchanged. DSN quoted-string and padstack bracket syntax is preserved when setting classes. Router completion is not evidence of a passing board: native checks decide release.

After edits, review analog return paths, input isolation, feedback/pump geometry and manufacturer polarity in addition to the automated checks. The snapshot files are dated evidence, not live API clients; refresh stock and price directly before ordering. See each module’s commissioning guide.
