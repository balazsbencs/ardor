# Stereo headphones / LM386 / relay mute

T1 hand-assembly PCB: 96 × 84 mm. **Unbuilt engineering prototype.**

[KiCad project](Ardor_Headphones_THT.kicad_pro) · [Manual BOM](BOM.csv) · [Schematic](review/schematic.pdf) · [Bare-board Gerbers](assembly/Ardor_Headphones_THT-Gerbers.zip)

Supply 5 V / **150 mA** and use 32–300 Ω headphones. Fit two **LM386N-1/NOPB** DIP-8 amplifiers, **G5V-2-H1 DC5** high-sensitivity DPDT relay, and onsemi 2N3904BU. Do not substitute LM386N-4 or the standard non-H1 relay: their operating conditions differ. Keep LM386 gain pins 1 and 8 open. No SMD IC or charge pump is present.

Input limit is 1 Vrms. The 39 kΩ/1 kΩ divider and nominal gain-20 amplifier give about 0.49 unloaded gain. R631/R632 provide 2.2 Ω output isolation; C651/C652 are 470 µF / 16 V, with positive toward the amplifiers. Their can diameter must be at most 8 mm with 3.5 mm lead spacing. C631/C632 bypass the internal references. The 10 Ω/47 nF output Zobel networks must be fitted. Both amplifiers remain powered when muted.

**Keep HP_ENABLE LOW for at least five seconds after stable supply/audio; mute before powering down.** HIGH is 3.3 V and draws approximately 5 mA. This timing is supplied by the host, not an onboard timer. R641/R642 are 1 kΩ bleeders which let the output coupling capacitors charge while the relay grounds the panel channels. Switching early can produce a loud transient. The relay commons are panel left/right, NC contacts ground them, and NO contacts select the AC-coupled audio.

Commission into separate **32 Ω dummy resistors**, with no headphones connected. Check about 2.5 V at U601/U602 pin 5, correct capacitor polarity, grounded panel outputs when muted, and settled near-zero DC at the relay's audio feeds before enabling. Capture startup, enable, disable and power-down waveforms, including host reset/disconnect. Measure gain, 20 Hz–20 kHz response, clipping, channel separation, hum, hiss and thermal behavior. This DIY design has different noise/distortion characteristics from M1's dedicated headphone IC. Begin listening at low host volume only after these checks pass.

## Connector pin assignments

Directions are relative to this board. Standard header square pads identify pin 1. Read reference and pin numbers from the back silkscreen; mirrored CAD exports are labelled as back views.

### J101 — 5V POWER + ENABLE INPUT

| Pin | Net | Direction | Connect to |
|---|---|---|---|
| 1 | +5V | POWER INPUT | Positive output of the matching regulated supply. |
| 2 | GND | GROUND | Host or cable ground / 0 V. |
| 3 | HP_ENABLE | CONTROL INPUT | Host control output. Keep LOW until audio and supply are stable. |

### J102 — STEREO AUDIO INPUT FROM SOURCE

| Pin | Net | Direction | Connect to |
|---|---|---|---|
| 1 | AUDIO_L | INPUT | DAC/codec left audio output, <=1 Vrms. |
| 2 | GND | GROUND | Host or cable ground / 0 V. |
| 3 | AUDIO_R | INPUT | DAC/codec right audio output, <=1 Vrms. |

### J602 — STEREO HEADPHONE AUDIO OUTPUT

| Pin | Net | Direction | Connect to |
|---|---|---|---|
| 1 | HP_L | OUTPUT | TRS socket tip; headphones >=32 ohm. |
| 2 | HP_R | OUTPUT | TRS socket ring; headphones >=32 ohm. |
| 3 | GND | GROUND | TRS socket sleeve; enclosure bond. |

## Assembly and files

Fit low-profile axial parts first, then sockets/headers and radial capacitors. Match diode bands, electrolytic polarity and IC notches to the PCB. Install the ICs or ADC breakout last. H1/H2 are 2.2 mm nonplated M2 mounting holes, not electrical terminals. Check enclosure clearance and capacitor/socket height: these boards are larger than M1.

Use the manual BOM and the bare-board Gerber/drill ZIP. There is no SMT assembly order. Front silkscreen shows component references; the front fabrication drawing shows package outlines and orientation; use the BOM for values. Back silk includes port functions and pin numbers. Native checks and package manifests are under `verification/`; they do not replace prototype measurements.

[Assembly accessories](assembly/accessories.csv) list sockets, shunts and panel items in addition to the PCB component BOM.

[Family guide](../README.md) · [Substitution sources](../SOURCES.md) · [Native validation](verification/validation.json)
