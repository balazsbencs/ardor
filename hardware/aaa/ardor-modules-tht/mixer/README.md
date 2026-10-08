# Stereo buffer / mono average / AC outputs / HAND ASSEMBLY

T1 hand-assembly PCB: 100 × 75 mm. **Unbuilt engineering prototype.**

[KiCad project](Ardor_Mixer_THT.kicad_pro) · [Manual BOM](BOM.csv) · [Schematic](review/schematic.pdf) · [Bare-board Gerbers](assembly/Ardor_Mixer_THT-Gerbers.zip)

Supply 5 V / 10 mA. Fit **MCP6022-I/P** DIP-8 amplifiers; TLV9002 SOIC parts from M1 do not fit. Use bipolar 10 µF coupling capacitors C401/C402/C407/C408/C409. C101 and C403 are ordinary polarized radial capacitors, positive on pad 1.

Inputs are line-level, no greater than 1 Vrms per channel. Outputs drive loads of at least 10 kΩ. OUT_MONO is `(LEFT + RIGHT) / 2`; all three outputs are AC-coupled. Feeding only one channel gives half its amplitude at the mono output. Feed both channels for a full-level mono source. The board is not an instrument preamp or headphone amplifier.

Commission with no signal: check the filtered 5 V rail and about 2.5 V at VREF and the internal buffer outputs. Check near-zero settled DC on the external outputs. Feed separate 1 kHz left/right tones and verify unity stereo buffers, mono averaging and channel separation. Check clipping, noise and oscillation across the audio band. Keep external harnesses short; exposed audio sockets need entry protection.

## Connector pin assignments

Directions are relative to this board. Standard header square pads identify pin 1. Read reference and pin numbers from the back silkscreen; mirrored CAD exports are labelled as back views.

### J101 — 5V POWER INPUT

| Pin | Net | Direction | Connect to |
|---|---|---|---|
| 1 | +5V | POWER INPUT | Positive output of the matching regulated supply. |
| 2 | GND | GROUND | Host or cable ground / 0 V. |

### J102 — STEREO AUDIO INPUT FROM SOURCE

| Pin | Net | Direction | Connect to |
|---|---|---|---|
| 1 | AUDIO_L | INPUT | DAC/codec left audio output, <=1 Vrms. |
| 2 | GND | GROUND | Host or cable ground / 0 V. |
| 3 | AUDIO_R | INPUT | DAC/codec right audio output, <=1 Vrms. |

### J103 — BUFFERED STEREO + MONO AUDIO OUTPUT

| Pin | Net | Direction | Connect to |
|---|---|---|---|
| 1 | OUT_L | OUTPUT | Left line input of another device, >=10k load. |
| 2 | GND | GROUND | Host or cable ground / 0 V. |
| 3 | OUT_R | OUTPUT | Right line input of another device, >=10k load. |
| 4 | OUT_MONO | OUTPUT | Mono line input, >=10k load. Signal is (L+R)/2. |

## Assembly and files

Fit low-profile axial parts first, then sockets/headers and radial capacitors. Match diode bands, electrolytic polarity and IC notches to the PCB. Install the ICs or ADC breakout last. H1/H2 are 2.2 mm nonplated M2 mounting holes, not electrical terminals. Check enclosure clearance and capacitor/socket height: these boards are larger than M1.

Use the manual BOM and the bare-board Gerber/drill ZIP. There is no SMT assembly order. Front silkscreen shows component references; the front fabrication drawing shows package outlines and orientation; use the BOM for values. Back silk includes port functions and pin numbers. Native checks and package manifests are under `verification/`; they do not replace prototype measurements.

[Assembly accessories](assembly/accessories.csv) list sockets, shunts and panel items in addition to the PCB component BOM.

[Family guide](../README.md) · [Substitution sources](../SOURCES.md) · [Native validation](verification/validation.json)
