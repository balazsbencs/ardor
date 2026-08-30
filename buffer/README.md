# Ardor Guitar Input Buffer

This folder contains the minimal guitar input buffer for Ardor.

Use this board between a passive guitar and the Raspberry Pi Codec Zero. Do
not connect a passive guitar directly to the Codec Zero input.

## KiCad project

Open [`buffer.kicad_pro`](buffer.kicad_pro) in KiCad to view or edit the
schematic and PCB.

Connect the guitar jack to `GUITAR_IN`. Connect `GUITAR_OUT` to the Codec Zero
input. Follow [`buffer.kicad_sch`](buffer.kicad_sch) for power and ground.

## JLCPCB assembly

The [`jlcpcb/production_files/`](jlcpcb/production_files/) folder contains
order-ready files for SMD assembly:

- [`GERBER-buffer.zip`](jlcpcb/production_files/GERBER-buffer.zip) contains the PCB files.
- [`BOM-buffer.csv`](jlcpcb/production_files/BOM-buffer.csv) contains the parts list.
- [`CPL-buffer.csv`](jlcpcb/production_files/CPL-buffer.csv) contains the part placement list.

Upload the Gerber, BOM, and CPL files to a JLCPCB PCB assembly order. Review
the parts and placement before you order.

The production files cover the SMD parts only. They do not include audio
jacks, wires, connectors, or enclosure parts. Add these parts after the board
arrives.

The larger [`hardware/control-io`](../hardware/control-io/README.md) and
[`hardware/integrated-codec-revc`](../hardware/integrated-codec-revc/README.md)
designs include their own guitar input stages. Choose one input design. Do not
place the standalone buffer in series with either design.
