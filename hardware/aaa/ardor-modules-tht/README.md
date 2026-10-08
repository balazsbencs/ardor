# Ardor hand-assembled function modules — T1

These are alternative KiCad 9 projects for the five [M1 production modules](../ardor-modules/README.md). Order bare PCBs and solder them with an ordinary iron. Every component fitted to these PCBs has through-hole leads. MIDI, mixer, line-out and headphones contain **no SMD components**. Expression uses a **purchased, preassembled ADS1115 breakout**: it contains SMD parts, but the builder only solders headers and sockets. No loose fine-pitch chip, hot-air tool or stencil is required.

| Project | PCB dimensions | Regulated supply budget | Assembly / differences from M1 |
|---|---|---|---|
| [MIDI input](midi-in/README.md) | 85 × 70 mm | 5 V / 10 mA + 3.3 V / 5 mA | DIP H11L1M; axial protection/filter parts |
| [Expression](expression/README.md) | 110 × 85 mm | 3.3 V / 15 mA | Socketed Adafruit 1085 **STEMMA QT revision**; same ADS1115 software |
| [Stereo/mono mixer](mixer/README.md) | 100 × 75 mm | 5 V / 10 mA | Socketed MCP6022-I/P; bipolar coupling capacitors |
| [Line output](line-out/README.md) | 115 × 85 mm | 5 V / 50 mA | MCP6022-I/P, through-hole relay, 2N3904 driver |
| [Stereo headphones](headphones/README.md) | 96 × 84 mm | 5 V / 150 mA | Two LM386N-1 DIP amplifiers, output capacitors, DPDT relay mute |

All host and panel **connector pin assignments are the same as M1**. The larger boards and mounting holes are not mechanically interchangeable with M1. Each project has its own schematic, routed PCB, local libraries, manual BOM, connector guide, review drawings and bare-board Gerber/drill ZIP. Each module operates independently with an external host/audio source and the listed supplies. No carrier board or other Ardor module is required.

## Build choices

- Use 1% 0.25 W axial resistors, formed to 10.16 mm lead spacing. R101 may be a wire link.
- Solder DIP sockets before fitting ICs. Match the IC notch to the socket and PCB pin-1 marking. The MIDI H11L1M uses a six-pin DIP footprint; an optional 7.62 mm-wide socket fits it.
- Use the capacitor dimensions and lead pitch in each BOM. The audio coupling capacitors marked **BIPOLAR** must be nonpolar electrolytics; ordinary polarized electrolytics are not substitutes. Polarized radial capacitors have pin 1 = positive and a `+` on the PCB. The can's stripe indicates negative.
- Use **bidirectional** SA5.0CA/SA24CA protection diodes. The `CA` suffix matters. Small-signal 1N4148 and BAT85S diodes are polarized: their band goes to the cathode, footprint pad 1.
- Filter beads are Würth 7427501 axial parts, formed to 15.24 mm spacing. Their RF characteristics and the axial TVS capacitance differ from M1; EMC/ESD equivalence is not claimed.
- Line/headphone enable drives a transistor base through 470 Ω. The host must source approximately **5 mA at 3.3 V HIGH**, rather than drive only a MOSFET gate. LOW or disconnected keeps the relay muted. Check the GPIO's drive capability.
- Fit standard 2.54 mm headers; panel DIN/TRS/TS sockets remain connected by wires. Read the module's pin table before wiring: different module types do not share a universal power pinout.

## Headphone tradeoff

The all-through-hole headphone board is a practical DIY alternative to the tiny TPA6132A2 QFN, not an equivalent hi-fi implementation. LM386 noise/distortion, supply rejection, available output and relay behavior need measurement. The nominal divider/amplifier gain is about 0.49 before the output resistor and load, close to M1's −6 dB setting. Two 470 µF capacitors block the amplifiers' midrail DC. Use 32–300 Ω headphones, a source no greater than 1 Vrms, and begin at low host volume.

**Keep HP_ENABLE LOW for at least five seconds after the supply and audio are stable.** The 1 kΩ bleeders charge the output capacitors while the relay grounds the panel outputs. The board has no automatic timer: the host must provide this delay and mute before power-down. M1's timing may need adjustment. This relay mutes the output; it does not shut down the amplifiers or save their idle current.

## Files and validation

Open the `.kicad_pro` in the desired folder. Use `BOM.csv` plus `assembly/accessories.csv` for manual shopping and the ZIP under `assembly/` for a bare PCB order. Do not use M1's SMT BOM/CPL for these boards. Front silkscreen identifies every builder-fitted part; back silkscreen provides connector numbers and wiring legends. Native schematic PDFs and front/back/assembly views are under `review/`.

[Printable assembly overview](review/module-overview.pdf) · [All five schematics](review/schematics.pdf)

These are **unbuilt engineering prototypes**. CAD checks establish connectivity, geometry and through-hole assembly; they do not establish measured audio quality, ESD performance or startup behavior. Each module guide includes commissioning steps. See [engineering review](review/REVIEW.md), [validation summary](review/validation-summary.json), [design regeneration](design/README.md) and [component sources and substitutions](SOURCES.md).
