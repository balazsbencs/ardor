# Ardor Rev C — Integrated DA7212 Audio + Control HAT

Rev C is a separate design that puts the same DA7212 codec family used by
Raspberry Pi Codec Zero directly on the Ardor board. Codec Zero is not required.
Rev B remains unchanged in `../control-io`.

Rev C includes its own protected guitar input buffer. The separate minimal
[`../../buffer/`](../../buffer/) board is not required for this design.

## Generated design files

- `generate-schematic.tsx`: Rev C entry point into the shared tscircuit source.
- `ardor-revc.circuit.json`: generated tscircuit Circuit JSON.
- `ardor-revc.kicad_pro`, `ardor-revc.kicad_sch`: native KiCad project and schematic.
- `tscircuit.pretty/`, library tables, and `tscircuit.kicad_sym`: local KiCad libraries.
- `ardor-revc.svg`, `ardor-revc.pdf`: vector and one-page PDF schematics.
- `gpio-wiring-revc.svg`: top-view 40-pin GPIO and physical wiring map.
- `check-connectivity.ts`: automated codec, GPIO, audio, clock, supply, and ground checks.

Generate from `../control-io` after installing its locked Node dependencies:

```sh
npm ci --ignore-scripts
npm run generate:revc
npm run check:revc
```

## Rev C architecture

| Function | Raspberry Pi connection | Rev C destination |
| --- | --- | --- |
| I2C SDA | GPIO2, physical pin 3 | DA7212 SDA and ADS1115 SDA |
| I2C SCL | GPIO3, physical pin 5 | DA7212 SCL and ADS1115 SCL |
| I2S BCLK | GPIO18, physical pin 12 | DA7212 BCLK through 33 Ω source damping |
| I2S WCLK | GPIO19, physical pin 35 | DA7212 WCLK through 33 Ω source damping |
| I2S data to Pi | GPIO20, physical pin 38 | DA7212 DATOUT through 33 Ω source damping |
| I2S data from Pi | GPIO21, physical pin 40 | DA7212 DATIN through 33 Ω source damping |
| Output unmute | GPIO10, physical pin 19 | Default-low MOSFET and normally-open relays |
| MIDI receive | GPIO9, physical pin 21 | Isolated H11L1M receiver output |
| Expression ready | GPIO25, physical pin 22 | ADS1115 ALERT/RDY |

U8 provides the codec's 12.288 MHz MCLK, with a 33 Ω source resistor. U7 makes
the low-noise 1.8 V `VDD_A` rail from 3.3 V. `VDD_IO` and `VDD_MIC` run at 3.3 V
and have separate local decoupling. `VDIG`, `DACREF`, `VREF`, `VMID`, and both
charge-pump capacitor pairs follow the DA7212 reference topology. Speaker and
microphone paths are intentionally unconnected.

The protected guitar buffer feeds both AUX channels through a 10 kΩ/10 kΩ Rev
C divider for active-pickup headroom; codec gain restores the nominal level in
software. HP_L/HP_R feed the existing line and mono-amp buffers. J2, J3, and J10
are optional DNI bring-up headers only.

The DA7212 schematic uses logical pins 1–34. Their WLCSP ball mapping is:

| Pins | Balls | Pins | Balls |
| --- | --- | --- | --- |
| 1–4 | A1, C1, B2, D2 | 18–21 | C9, B10, D10, A11 |
| 5–8 | A3, C3, B4, D4 | 22–25 | C11, B12, D12, A13 |
| 9–12 | A5, C5, B6, D6 | 26–29 | C13, B14, D14, A15 |
| 13–17 | A7, C7, B8, D8, A9 | 30–34 | C15, B16, D16, A17, C17 |

The generated U6 footprint contains 34 round 0.25 mm pads on the staggered 0.5
mm ball grid and an external pin-1 marker. Verify this mapping against the
controlled Renesas package drawing again at layout release.

## Software

DA7212 is at I2C address `0x1a`; ADS1115 remains at `0x48`. The existing
Raspberry Pi overlay is explicitly loaded, so an EEPROM is not required for
bring-up:

```ini
dtoverlay=rpi-codeczero
```

The overlay configures the DA721x codec and Raspberry Pi as the I2S clock
consumer. GPIO10 must stay low until ALSA is configured and codec clock/bias
settling is complete, then it may be driven high to close K1/K2.

## JLCPCB/JLCPCBA target and release status

- Use Standard PCBA, four copper layers, ENIG, and a single uninterrupted ground
  reference plane under codec, clock, I2S, and audio circuitry.
- Tell JLCPCB that U6 is a 34-ball, 0.5 mm-pitch WLCSP; require X-ray inspection
  and first-article images. Do not use via-in-pad or capped vias under U6.
- Place every 0201 codec reference/charge-pump capacitor immediately beside its
  assigned balls. Place U7/U8 and their bypass capacitors in the same quiet zone.
- Keep I2S over a continuous ground plane and away from AUX/HP/high-impedance
  nodes. Keep relay-coil return current and connector ESD current out of this zone.
- DNP J2, J3, J10, and R31 for production. Do not substitute U6, U7, U8, or the
  codec's reference and charge-pump capacitors without engineering review.

This directory is **schematic-complete, not fabrication-released**. It does not
yet contain a routed `.kicad_pcb`, Gerbers, drill files, JLC BOM, or centroid
file. Those files cannot be made responsibly until the board outline, mounting
holes, connector edge locations, Raspberry Pi keep-outs, maximum component
height, and enclosure/harness constraints are fixed. The exact release gates
are in `RELEASE-CHECKLIST.md`.
