# Mono line output / relay mute / internal amp feed / HAND ASSEMBLY

T1 hand-assembly PCB: 115 × 85 mm. **Unbuilt engineering prototype.**

[KiCad project](Ardor_Line_Out_THT.kicad_pro) · [Manual BOM](BOM.csv) · [Schematic](review/schematic.pdf) · [Bare-board Gerbers](assembly/Ardor_Line_Out_THT-Gerbers.zip)

Supply 5 V / 50 mA. Fit MCP6022-I/P, **onsemi 2N3904BU** (E1/B2/C3), and **Omron G5V-1 DC5**. The 470 Ω base resistor means LINE_ENABLE draws approximately 5 mA from a 3.3 V HIGH; confirm the host GPIO can source this current. Fit a banded 1N4148 flyback diode, cathode toward +5 V. C501 is polarized: its positive end goes toward the audio buffer. C401/C502 are bipolar.

The mono input limit is 1 Vrms. The line output requires at least 10 kΩ load; the internal AMP_FEED requires at least 100 kΩ. J502 carries a signal for an external amplifier; it cannot drive a speaker. Relay OFF grounds the panel tip and disconnects the source. Relay ON connects the AC-coupled line signal. This is a mute, not true bypass. The internal amp feed is not relay-muted.

Hold LINE_ENABLE LOW until power/audio have settled; allowing five seconds also settles the line coupling capacitor. Mute before power-down. Commission with the panel unplugged: check the 5 V rail, about 2.5 V internal bias, GPIO base current, relay coil voltage and contact behavior. With the relay off, verify the panel output is grounded. After settling, enable and measure external DC and unity audio into a 10 kΩ dummy load. Check startup/power-down transients before connecting an amplifier.

## Connector pin assignments

Directions are relative to this board. Standard header square pads identify pin 1. Read reference and pin numbers from the back silkscreen; mirrored CAD exports are labelled as back views.

### J101 — 5V POWER + ENABLE INPUT

| Pin | Net | Direction | Connect to |
|---|---|---|---|
| 1 | +5V | POWER INPUT | Positive output of the matching regulated supply. |
| 2 | GND | GROUND | Host or cable ground / 0 V. |
| 3 | LINE_ENABLE | CONTROL INPUT | Host control output. Keep LOW until audio and supply are stable. |

### J102 — MONO AUDIO INPUT FROM SOURCE

| Pin | Net | Direction | Connect to |
|---|---|---|---|
| 1 | AUDIO_IN | INPUT | Mono DAC/codec audio output, <=1 Vrms. |
| 2 | GND | GROUND | Host or cable ground / 0 V. |

### J503 — LINE AUDIO OUTPUT TO TS SOCKET

| Pin | Net | Direction | Connect to |
|---|---|---|---|
| 1 | LINE_JACK | OUTPUT | TS socket tip; line load >=10k. Grounded when relay is off. |
| 2 | GND | GROUND | TS socket sleeve; enclosure bond. |

### J502 — AUDIO OUTPUT TO AMP INPUT

| Pin | Net | Direction | Connect to |
|---|---|---|---|
| 1 | AMP_FEED | OUTPUT | Signal input of a separate amplifier, >=100k. NOT a speaker. |
| 2 | GND | GROUND | Host or cable ground / 0 V. |

Audio signal only. Never connect a speaker here.

## Assembly and files

Fit low-profile axial parts first, then sockets/headers and radial capacitors. Match diode bands, electrolytic polarity and IC notches to the PCB. Install the ICs or ADC breakout last. H1/H2 are 2.2 mm nonplated M2 mounting holes, not electrical terminals. Check enclosure clearance and capacitor/socket height: these boards are larger than M1.

Use the manual BOM and the bare-board Gerber/drill ZIP. There is no SMT assembly order. Front silkscreen shows component references; the front fabrication drawing shows package outlines and orientation; use the BOM for values. Back silk includes port functions and pin numbers. Native checks and package manifests are under `verification/`; they do not replace prototype measurements.

[Assembly accessories](assembly/accessories.csv) list sockets, shunts and panel items in addition to the PCB component BOM.

[Family guide](../README.md) · [Substitution sources](../SOURCES.md) · [Native validation](verification/validation.json)
