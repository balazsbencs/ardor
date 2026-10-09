# Ardor hand-assembled function modules — T1

These are alternative KiCad 9 projects for the five [M1 production modules](../ardor-modules/README.md). Order bare PCBs and solder them with an ordinary iron. MIDI, mixer and line-out contain **only through-hole components**. The headphone board uses **one hand-solderable OPA1656ID SOIC-8**, with all supporting components through-hole. Expression uses a **purchased, preassembled ADS1115 breakout**: it contains SMD parts, but the builder only solders headers and sockets. The OPA1656 has exposed leads at 1.27 mm pitch and no underside pad; an ordinary iron and flux are sufficient. No hot-air tool or stencil is required.

| Project | PCB dimensions | Regulated supply budget | Assembly / differences from M1 |
|---|---|---|---|
| [MIDI input](midi-in/README.md) | 85 × 70 mm | 5 V / 10 mA + 3.3 V / 5 mA | DIP H11L1M; axial protection/filter parts |
| [Expression](expression/README.md) | 110 × 85 mm | 3.3 V / 15 mA | Socketed Adafruit 1085 **STEMMA QT revision**; same ADS1115 software |
| [Stereo/mono mixer](mixer/README.md) | 100 × 75 mm | 5 V / 10 mA | Socketed MCP6022-I/P; bipolar coupling capacitors |
| [Line output](line-out/README.md) | 115 × 85 mm | 5 V / 50 mA | MCP6022-I/P, through-hole relay, 2N3904 driver |
| [Stereo headphones](headphones/README.md) | 96 × 84 mm | 5 V / 150 mA | One OPA1656ID SOIC-8, THT supporting parts, DPDT relay mute |

All host and panel **connector pin assignments are the same as M1**. The larger boards and mounting holes are not mechanically interchangeable with M1. Each project has its own schematic, routed PCB, local libraries, manual BOM, connector guide, review drawings and bare-board Gerber/drill ZIP. Each module operates independently with an external host/audio source and the listed supplies. No carrier board or other Ardor module is required.

## Build choices

- Use 1% 0.25 W axial resistors, formed to 10.16 mm lead spacing, except headphone feedback resistors R651/R652: stand these upright at 2.54 mm pitch with short folded leads. R101 may be a wire link.
- On the headphone board, solder and inspect the OPA1656 SOIC-8 first; no IC socket or adapter is used. On the other boards, solder DIP sockets before fitting ICs. Match the IC notch to the socket and PCB pin-1 marking. The MIDI H11L1M uses a six-pin DIP footprint; an optional 7.62 mm-wide socket fits it.
- Use the capacitor dimensions and lead pitch in each BOM. The audio coupling capacitors marked **BIPOLAR** must be nonpolar electrolytics; ordinary polarized electrolytics are not substitutes. Polarized radial capacitors have pin 1 = positive and a `+` on the PCB. The can's stripe indicates negative.
- Use **bidirectional** DC Components **1.5KE6.8CA** protection diodes ([HESTORE 100.430.71](https://www.hestore.hu/prod_10043071.html)), formed to **15.24 mm** lead spacing. Either orientation is valid. MIDI D203/D204 retain **SA24CA-E3/54 at 10.16 mm pitch** for their higher-voltage protection function; do not fit 1.5KE6.8CA there. The `CA` suffix matters. Small-signal 1N4148 and BAT85S diodes are polarized: their band goes to the cathode, footprint pad 1.
- Filter beads are Würth 7427501 axial parts, formed to 15.24 mm spacing. Their RF characteristics and the axial TVS capacitance differ from M1; EMC/ESD equivalence is not claimed.
- Line/headphone enable drives a transistor base through 470 Ω. The host must source approximately **5 mA at 3.3 V HIGH**, rather than drive only a MOSFET gate. LOW or disconnected keeps the relay muted. Check the GPIO's drive capability.
- Fit standard 2.54 mm headers; panel DIN/TRS/TS sockets remain connected by wires. Read the module's pin table before wiring: different module types do not share a universal power pinout.

## Headphone tradeoff

The headphone board uses a modern dual OPA1656 audio op amp as the agreed hand-solderable SMD exception. It operates from regulated 5 V ±5% in an inverting circuit with nominal unloaded gain −0.5; both channels invert together. Its inputs stay at the filtered 2.5 V reference because the common-mode input range is restricted near the positive supply. Local feedback uses upright THT 10 kΩ resistors and 100 pF C0G capacitors. A 100 nF THT bypass sits close to U601. The remaining parts are THT.

Two 10 Ω output resistors sit outside the feedback loops, before the 470 µF DC-blocking capacitors. Calculated midband gain into 32 Ω is about −0.378, approximately 4.5 mW/channel for 1 Vrms input. This preserves the modest output level of the DIY alternative; it is not a measured clean-power rating. Use 32–300 Ω headphones, a source no greater than 1 Vrms, and begin at low host volume. Clean output, noise, cable-load stability and startup/switching transients remain bench-test items.
**Keep HP_ENABLE LOW for at least five seconds after the supply and audio are stable.** The 1 kΩ bleeders charge the output capacitors while the relay grounds the panel outputs. The board has no automatic timer: the host must provide this delay and mute before power-down. M1's timing may need adjustment. This relay mutes the output; it does not shut down the amplifiers or save their idle current.

## Files and validation

Open the `.kicad_pro` in the desired folder. Use `BOM.csv` plus `assembly/accessories.csv` for manual shopping and the ZIP under `assembly/` for a bare PCB order. Do not use M1's SMT BOM/CPL for these boards. Front silkscreen identifies every builder-fitted part; back silkscreen provides connector numbers and wiring legends. Native schematic PDFs and front/back/assembly views are under `review/`.

The [Hestore purchasing audit](procurement/README.md) covers every component and assembly accessory, with dated stock, candidate links and replacement limitations. Several candidates require supplier confirmation or PCB changes; the audit does not change these BOMs or manufacturing files.

[Printable assembly overview](review/module-overview.pdf) · [All five schematics](review/schematics.pdf)

These are **unbuilt engineering prototypes**. CAD checks establish connectivity, geometry and the documented hand-assembly package choices; they do not establish measured audio quality, ESD performance or startup behavior. Each module guide includes commissioning steps. See [engineering review](review/REVIEW.md), [validation summary](review/validation-summary.json), [design regeneration](design/README.md) and [component sources and substitutions](SOURCES.md).
