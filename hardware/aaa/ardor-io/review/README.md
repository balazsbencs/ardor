# Review deliverables

- **[Connected schematic — PDF](Ardor_IO_connected.pdf)**: one A1 landscape page, all 95 PCB components and four panel connectors. Vector output stays sharp when zoomed; A1 printing is recommended for comfortable component-value reading.
- **[Connected schematic — SVG](Ardor_IO_connected.svg)**: editable vector drawing, opens in a browser or vector editor.
- [PNG preview](Ardor_IO_connected.png).
- **[Implemented fixes and verification](FIXES.md)**.
- **[JLCPCB assembly package](../assembly/README.md)**: exact stocked SMT parts, BOM/CPL, Gerbers and manual-parts list for two boards.
- **[Track width cleanup](TRACK_WIDTH_REVIEW.md)** and [before/after close-ups](track-width-cleanup.png): unnecessary steps removed and constrained neckdowns shortened; full DRC and connectivity checks pass.
- **[Comprehensive review](REVIEW.md)**: prioritized findings, circuit calculations, PCB observations, validation evidence and bring-up actions.

Signal connections use continuous wires. Power and ground use conventional repeated symbols. J101 and a few IC power/ground groups are distributed for readability and retain their physical pin numbers. Dashed JP301 lines show removable shunts, not permanent copper. NC pins are listed in the footer. The review drawing represents the corrected design and is **not an editable KiCad replacement schematic**; use the original six sheets for PCB changes.

The renderer checks all connected source pins and infers connectivity from its wire geometry to detect drawing shorts/opens. The PDF was also visually inspected. The source schematic, PCB, BOM and documentation include the user-authorized fixes. The pre-fix drawing and reports are archived under `baseline/`.

## Reproduce

Use KiCad 9.0.2 and its standard footprint libraries, plus the project's vendored package libraries. Run from the project directory, writing only into `review/`:

```sh
kicad-cli sch export netlist --format kicadxml -o review/fresh-netlist.xml Ardor_IO.kicad_sch
kicad-cli sch erc --severity-all --exit-code-violations --format json -o review/fresh-erc.json Ardor_IO.kicad_sch
kicad-cli pcb drc --schematic-parity --all-track-errors --severity-all --exit-code-violations --format json -o review/fresh-drc.json Ardor_IO.kicad_pcb
python3 design/verify_review_fixes.py
python3 design/verify_track_width_cleanup.py
python3 review/audit_source.py
python3 review/draw_connected.py
```

Python dependencies: KiCad's `pcbnew` for the part/land verification; `sexpdata` for the source audit; `cairosvg` for drawing/PDF generation. A system Cairo library is required. In a standalone CLI environment, ensure the footprint table resolves `Package_SO`, `Package_DFN_QFN`, and `Package_TO_SOT_SMD`, and `Ardor_Capacitor` to this project's local snapshots rather than the global libraries. No rule suppression is required.

`source-sha256.txt` identifies the reviewed design inputs. Fresh verification files contain their actual run timestamps. Regenerating this drawing checks connectivity but does not automatically re-layout it or rewrite the engineering review for a changed design.

`track-width-audit.json` and `track-width-examples.*` preserve the original width inspection. `track-width-cleanup-verification.json` and `track-width-cleanup.*` describe the completed cleanup. `width-baseline/` contains its archived input board and passing pre-cleanup DRC.
