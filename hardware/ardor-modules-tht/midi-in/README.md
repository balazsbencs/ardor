# Isolated MIDI input / 3.3 V UART / HAND ASSEMBLY

T1.1 compact hand-assembly PCB: 39 × 35 mm. **Unbuilt engineering prototype.**

The dense MIDI carrier is 39 × 35 mm, replacing the 58 × 42 mm outline. The net connections and connector pin assignments are unchanged; the ferrites and optocoupler use the Hestore selections described below. The native 3 mm MIDI-loop-to-logic clearance rule remains enabled, including filled copper. Mounting holes are at (3, 32) and (36, 3) mm from the top-left board corner.

Front and assembly labels show values/types, with connector IDs retained for wiring. Some labels are covered by fitted parts. The 1N4148, SMC TVS and two 100n capacitor labels use 0.8 mm text; other labels use 0.9 mm. Full references and ratings remain in the BOM.

The 3D views omit the custom TVS bodies; the complete embedded body/courtyard reservations are checked separately. Confirm enclosure, socket and cable access before fabrication. See [layout and sourcing notes](review/layout-notes.md). This remains an unbuilt engineering candidate.

[KiCad project](Ardor_MIDI_THT.kicad_pro) · [Manual BOM](BOM.csv) · [Schematic](review/schematic.pdf) · [Bare-board Gerbers](assembly/Ardor_MIDI_THT-Gerbers.zip)

Use a female five-pin DIN panel socket. Wire numbered DIN contact 4 to J202.1 and contact 5 to J202.2. Leave DIN 1/2/3 and shell unwired; insulate the socket shell. Bond the enclosure to J203.1. Do not treat MIDI contact 5 as ground. Fit **Everlight H11L1, Hestore 100.435.31**, standard DIP-6 with 7.62 mm row spacing. It replaces onsemi H11L1M without changing the pin connections, 5 V supply or 3.3 V output pull-up. Everlight's M option has 10.16 mm lead spacing and is not the selected package. A 6N138 is DIP-8 with a different output stage and is not a drop-in replacement.

D203/D204 are **Littelfuse SMDJ24CA**, the agreed pair of large hand-solderable **SMC / DO-214AB** TVS parts. FB201/FB202 now use **FERROCORE LCBB-601, Hestore 100.327.86**, 600 Ω at 100 MHz, 2 A, 0.1 Ω maximum DCR, in 0805. Their 1.6 × 1.8 mm pads extend beyond the 2.2 × 1.45 mm maximum body for iron access; use tweezers and flux. Fit all four SMD parts first. Both ferrites and the bidirectional CA TVS parts are nonpolar. All remaining parts are through-hole. The beads replace the former axial 800 Ω parts and have different RF behavior; bench-test cable/EMC performance. D202 remains DC Components 1.5KE6.8CA; the two higher-voltage SMDJ24CA positions must not be populated with that low-voltage part.

Supply 5 V / 10 mA and 3.3 V / 5 mA. Set the host UART to 31250 baud, 8-N-1. J101.4 is a 3.3 V output into the host RX input.

Commission: inspect DIP orientation and D201 band, verify 5 V at U201.6 and 3.3 V at the output pull-up, then verify idle UART HIGH. Send known MIDI notes and check UART timing and decoding. Test long cables and real transmitters because the axial protection parts have different capacitance/RF behavior. Functional ground-loop isolation is not safety isolation.

## Connector pin assignments

Directions are relative to this board. Standard header square pads identify pin 1. Read reference and pin numbers from the back silkscreen; mirrored CAD exports are labelled as back views.

### J202 — MIDI INPUT FROM DIN SOCKET

| Pin | Net | Direction | Connect to |
|---|---|---|---|
| 1 | MIDI_4 | MIDI INPUT | Female DIN socket numbered contact 4. |
| 2 | MIDI_5 | MIDI INPUT | Female DIN socket numbered contact 5. |

### J101 — POWER INPUT + UART OUTPUT TO HOST

| Pin | Net | Direction | Connect to |
|---|---|---|---|
| 1 | +5V | POWER INPUT | Positive output of the matching regulated supply. |
| 2 | +3V3 | POWER INPUT | Positive output of the matching regulated supply. |
| 3 | GND | GROUND | Host or cable ground / 0 V. |
| 4 | MIDI_RX | OUTPUT | Host UART RX input, 31250 baud, 8-N-1. Do not connect to host TX. |

### J203 — METAL CASE BOND + GROUND

| Pin | Net | Direction | Connect to |
|---|---|---|---|
| 1 | CHASSIS | BOND | Aluminium enclosure bonding point; insulate the MIDI DIN shell. |
| 2 | GND | GROUND | Host or cable ground / 0 V. |

## Assembly and files

Solder and inspect the two SMC TVS parts and both 0805 ferrites first, then fit low-profile axial parts, sockets/headers and radial capacitors. Match D201 band and IC orientation. H1/H2 are 2.2 mm nonplated M2 mounting holes, not electrical terminals. Check enclosure clearance and capacitor/socket height. T1.1 compacts the layouts; hole positions differ from T1 and M1.

Use the manual BOM and the bare-board Gerber/drill ZIP. There is no SMT assembly order. Front silkscreen shows component values/types; the front fabrication drawing shows package outlines and orientation; use the BOM for values. Back silk includes port functions and pin numbers. Native checks and package manifests are under `verification/`; they do not replace prototype measurements.

[Assembly accessories](assembly/accessories.csv) list sockets, shunts and panel items in addition to the PCB component BOM.

[Family guide](../README.md) · [Substitution sources](../SOURCES.md) · [Native validation](verification/validation.json)
