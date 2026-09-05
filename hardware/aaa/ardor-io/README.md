# Ardor Codec Zero I/O

Open **Ardor_IO.kicad_pro** in KiCad 9. The root schematic opens five functional sheets. No PCB is included.

- **Ardor_IO.pdf** — printable six-sheet schematic.
- **BOM.csv** — component values, critical part numbers and footprints.
- **DESIGN_NOTES.md** — wiring, GPIOs, signal levels, firmware sequence, layout guidance and production-release tests.
- **SOURCES.md** — primary data sheets and interface references.
- **verification/** — KiCad ERC, exported netlist and connectivity audit.

Uses 5 V from the Pi, a 100 kΩ passive expression pedal, mono 6.3 mm line out, stereo headphones, DIN MIDI in and an internal feed to the separate amp circuit.

Rev A is a checked engineering prototype schematic, not a bench-qualified production release. Read the integration and release notes before building. Panel connector models and stack height are mechanical selections to be finalized with your PCB/enclosure.
