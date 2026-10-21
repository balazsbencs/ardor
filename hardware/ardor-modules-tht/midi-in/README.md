# Isolated MIDI input / 3.3 V UART / HAND ASSEMBLY

T1.1 compact hand-assembly PCB: 58 × 42 mm. **Unbuilt engineering prototype.**

[KiCad project](Ardor_MIDI_THT.kicad_pro) · [Manual BOM](BOM.csv) · [Schematic](review/schematic.pdf) · [Bare-board Gerbers](assembly/Ardor_MIDI_THT-Gerbers.zip)

Use a female five-pin DIN panel socket. Wire numbered DIN contact 4 to J202.1 and contact 5 to J202.2. Leave DIN 1/2/3 and shell unwired; insulate the socket shell. Bond the enclosure to J203.1. Do not treat MIDI contact 5 as ground. Fit the exact H11L1M optocoupler; other optocouplers have different switching thresholds or pinouts.

D203/D204 are **Littelfuse SMDJ24CA**, the agreed pair of large hand-solderable **SMC / DO-214AB** TVS parts. Fit them first with flux and an ordinary iron; the two broad terminal pads are accessible. Either orientation is valid for the bidirectional CA version. All other builder-fitted components are through-hole. D202 remains DC Components 1.5KE6.8CA; the two higher-voltage SMDJ24CA positions must not be populated with that low-voltage part.

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

Solder and inspect D203/D204 SMC parts first, then fit low-profile axial parts, sockets/headers and radial capacitors. Match D201 band and IC orientation. H1/H2 are 2.2 mm nonplated M2 mounting holes, not electrical terminals. Check enclosure clearance and capacitor/socket height. T1.1 compacts the layouts; hole positions differ from T1 and M1.

Use the manual BOM and the bare-board Gerber/drill ZIP. There is no SMT assembly order. Front silkscreen shows component references; the front fabrication drawing shows package outlines and orientation; use the BOM for values. Back silk includes port functions and pin numbers. Native checks and package manifests are under `verification/`; they do not replace prototype measurements.

[Assembly accessories](assembly/accessories.csv) list sockets, shunts and panel items in addition to the PCB component BOM.

[Family guide](../README.md) · [Substitution sources](../SOURCES.md) · [Native validation](verification/validation.json)
