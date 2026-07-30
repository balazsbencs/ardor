# Ardor Control I/O

This KiCad 10 project contains the first-pass companion circuit for:

- 3.5 mm TRS Type-A MIDI input on Raspberry Pi UART4.
- One passive expression pedal input through an ADS1115.
- A hardware DPDT polarity selector for the two common expression-pedal TRS wirings.

## Raspberry Pi interface

| J3 | Signal | Raspberry Pi |
| ---: | --- | --- |
| 1 | +5V | 5V header supply |
| 2 | +3V3 | 3.3V header supply |
| 3 | GND | Digital ground |
| 4 | I2C SDA | GPIO2, physical pin 3 |
| 5 | I2C SCL | GPIO3, physical pin 5 |
| 6 | MIDI RX | GPIO9, physical pin 21, UART4 RX |
| 7 | ADC ALERT/RDY | GPIO25, physical pin 22 |
| 8 | MIDI TX reserve | GPIO8, physical pin 24, UART4 TX |

## Design choices

The MIDI input follows the official TRS Type-A pinout: tip is the current sink,
ring is the current source, and sleeve is the cable shield. It is galvanically
isolated with a 6N138. The optocoupler output
side is powered from 5 V, while its open-collector output is pulled up only to
3.3 V for the Raspberry Pi. The TRS sleeve is left isolated from Pi ground.

The expression jack supplies a current-limited 3.3 V excitation and sends the
wiper through a 4.7 kΩ/100 nF input filter and Schottky clamps into ADS1115
AIN0. The DPDT switch swaps tip and ring; software calibration handles the
remaining pedal travel and endpoint variation.

No extra I2C pull-ups are included because this bus is shared with the Codec
Zero. Add them only if measurements on the assembled system show they are
needed.

## Files and verification

- `control-io.kicad_sch` is the editable native KiCad schematic.
- `control-io.pdf` is a review copy exported by KiCad.
- `control-io-erc.rpt` is the KiCad ERC report.
- `generate-schematic.tsx` is the reproducible schematic source. With Node 24
  selected through `nvm`, run `npm install --ignore-scripts` and
  `npm run generate` in this directory to regenerate the native file. Ignoring
  optional native install scripts keeps this schematic-only toolchain portable.

KiCad 10.0.5 exports the schematic successfully and reports no ERC errors.
The remaining warnings are generator artifacts: embedded custom-symbol library
IDs, coordinates off KiCad's preferred grid, and intentional single-ended nets
(`SHIELD` and reserved MIDI TX). Connectivity should still be reviewed in
KiCad after any regeneration.

This revision is a schematic design, not a production-ready PCB. Jack and
switch footprints must be checked against the enclosure and chosen mechanical
parts before layout.
