# Ardor Codec Zero I/O

Open **Ardor_IO.kicad_pro** in KiCad 9. The root schematic opens five functional sheets. **Ardor_IO.kicad_pcb** is the routed 68 × 46 mm compact board (45.7% less area than the previous 90 × 64 mm board; 60.9% less than the original 100 × 80 mm placement).

- **Ardor_IO.pdf** — printable six-sheet schematic.
- **BOM.csv** — component values, critical part numbers and footprints.
- **DESIGN_NOTES.md** — wiring, GPIOs, signal levels, firmware sequence, layout guidance and production-release tests.
- **SOURCES.md** — primary data sheets and interface references.
- **verification/** — schematic ERC, exported netlist and connectivity audit.
- **routing/** — current board previews, placement coordinates, zero-violation DRC/parity report, pad/net audit and routing source/session. See [routing/README.md](routing/README.md).
- **footprints/** — project-local snapshots of the original package footprints, preserving their geometry across KiCad 9 library versions.
- **pcb-plan/** — historical, unrouted 100 × 80 mm placement; superseded by the current board.

Uses 5 V from the Pi, a 100 kΩ passive expression pedal, mono 6.3 mm line out, stereo headphones, DIN MIDI in and an internal feed to the separate amp circuit.

Rev A is a checked engineering prototype schematic, not a bench-qualified production release. Read the integration and release notes before building. Panel connector models and stack height are mechanical selections to be finalized with your PCB/enclosure.

The compact layout retains all 95 schematic components and the existing panel-harness interfaces. Connector functions/pins and testpoint signals are on front silkscreen; JP301 shunt instructions are on the back. Component references remain on F.Fab. Four M3 holes are provisional enclosure mounts, not a verified Raspberry Pi mounting pattern. Confirm mechanical fit and stack height before ordering.

C401/C402/C502/C601/C602 are now 10 µF / 50 V Samsung X7R ceramic capacitors (CL31B106KBHNNNE, JLCPCB C89632) in standard 1206 footprints. See [SMD capacitor selection](routing/SMD_CAPACITORS.md).
