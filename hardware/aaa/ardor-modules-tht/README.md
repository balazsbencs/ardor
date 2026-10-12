# Ardor hand-assembled function modules — compact T1.1

These are alternative KiCad 9 projects for the five [M1 production modules](../ardor-modules/README.md). Order bare PCBs and solder them with an ordinary iron. Mixer and line-out contain **only through-hole components**. MIDI uses **two large hand-solderable SMDJ24CA SMC TVS parts**, with all other components through-hole. The headphone board uses **one hand-solderable OPA1656ID SOIC-8**, with all supporting components through-hole. Expression uses the **purchased, preassembled Hestore ADS1115-M**: it contains SMD parts, but the builder only solders headers and a socket. The OPA1656 has exposed leads at 1.27 mm pitch and no underside pad; the SMC parts have two broad extended terminal pads. An ordinary iron and flux are sufficient. No hot-air tool or stencil is required.

| Project | PCB dimensions | Regulated supply budget | Assembly / differences from M1 |
|---|---|---|---|
| [MIDI input](midi-in/README.md) | 58 × 42 mm | 5 V / 10 mA + 3.3 V / 5 mA | DIP H11L1M; two large SMC TVS; axial filters |
| [Expression](expression/README.md) | 72 × 50 mm | 3.3 V / 15 mA | Hestore ADS1115-M on one ten-pin socket; confirm supplied ADC identity |
| [Stereo/mono mixer](mixer/README.md) | 70 × 48 mm | 5 V / 10 mA | Socketed MCP6022-I/P; Nichicon 10 µF / 50 V bipolar coupling |
| [Line output](line-out/README.md) | 70 × 50 mm | 5 V / 50 mA | MCP6022-I/P, through-hole relay, 2N3904 driver |
| [Stereo headphones](headphones/README.md) | 76 × 58 mm | 5 V / 150 mA | One OPA1656ID SOIC-8, THT supporting parts, DPDT relay mute |

All host and panel **connector pin assignments are the same as M1**. The larger boards and mounting holes are not mechanically interchangeable with M1. Each project has its own schematic, routed PCB, local libraries, manual BOM, connector guide, review drawings and bare-board Gerber/drill ZIP. Each module operates independently with an external host/audio source and the listed supplies. No carrier board or other Ardor module is required.

T1.1 replaces the earlier scaled placements with compact functional groups, reducing board area by **45–64%**. It reserves the entire ADC module body, maximum Nichicon can diameters, solder access, edge headers and insulated M2 mounting holes. The headphone feedback/bypass cluster retains its geometry; MIDI retains its 3 mm loop-to-logic clearance. See the [size and courtyard checks](review/compact-layouts.json). T1.1 mounting-hole positions differ from the earlier T1 boards.

## Build choices

- Use 1% 0.25 W axial resistors, formed to 10.16 mm lead spacing, except headphone feedback resistors R651/R652: stand these upright at 2.54 mm pitch with short folded leads. R101 may be a wire link.
- On the headphone board, solder and inspect the OPA1656 SOIC-8 first; no IC socket or adapter is used. On MIDI, solder the two SMC TVS parts first. Solder DIP sockets before fitting ICs. Match the IC notch to the socket and PCB pin-1 marking. The MIDI H11L1M uses a six-pin DIP footprint; an optional 7.62 mm-wide socket fits it.
- Use the capacitor dimensions and lead pitch in each BOM. All nine audio coupling capacitors marked **BIPOLAR** are **Nichicon UES1H100MPM, 10 µF / 50 V**, 3.5 mm pitch, nominal 8 × 11.5 mm can; the footprints allow maximum 8.5 mm diameter and 12.5 mm height. Either orientation is valid. Ordinary polarized electrolytics are not substitutes. Polarized radial capacitors have pin 1 = positive and a `+` on the PCB. The can's stripe indicates negative.
- Use **bidirectional** DC Components **1.5KE6.8CA** protection diodes ([HESTORE 100.430.71](https://www.hestore.hu/prod_10043071.html)), formed to **15.24 mm** lead spacing. MIDI D203/D204 instead use **Littelfuse SMDJ24CA** ([HESTORE 100.461.56](https://www.hestore.hu/prod_10046156.html)), large SMC packages with extended hand-solder pads, for their higher-voltage protection function; do not fit 1.5KE6.8CA there. Either orientation is valid for both bidirectional types. The `CA` suffix matters. Small-signal 1N4148 and BAT85S diodes are polarized: their band goes to the cathode, footprint pad 1.
- Filter beads are Würth 7427501 axial parts, formed to 15.24 mm spacing. Their RF characteristics and the axial TVS capacitance differ from M1; EMC/ESD equivalence is not claimed.
- Line/headphone enable drives a transistor base through 470 Ω. The host must source approximately **5 mA at 3.3 V HIGH**, rather than drive only a MOSFET gate. LOW or disconnected keeps the relay muted. Check the GPIO's drive capability.
- Fit standard 2.54 mm headers; panel DIN/TRS/TS sockets remain connected by wires. Read the module's pin table before wiring: different module types do not share a universal power pinout.

## Headphone tradeoff

The headphone board uses a modern dual OPA1656 audio op amp as the agreed hand-solderable SMD exception. It operates from regulated 5 V ±5% in an inverting circuit with nominal unloaded gain −0.5; both channels invert together. Its inputs stay at the filtered 2.5 V reference because the common-mode input range is restricted near the positive supply. Local feedback uses upright THT 10 kΩ resistors and 100 pF C0G capacitors. A 100 nF THT bypass sits close to U601. The remaining parts are THT.

Two 10 Ω output resistors sit outside the feedback loops, before the 470 µF DC-blocking capacitors. Calculated midband gain into 32 Ω is about −0.378, approximately 4.5 mW/channel for 1 Vrms input. This preserves the modest output level of the DIY alternative; it is not a measured clean-power rating. Use 32–300 Ω headphones, a source no greater than 1 Vrms, and begin at low host volume. Clean output, noise, cable-load stability and startup/switching transients remain bench-test items.
**Keep HP_ENABLE LOW for at least five seconds after the supply and audio are stable.** The 1 kΩ bleeders charge the output capacitors while the relay grounds the panel outputs. The board has no automatic timer: the host must provide this delay and mute before power-down. M1's timing may need adjustment. This relay mutes the output; it does not shut down the amplifiers or save their idle current.

## Files and validation

Open the `.kicad_pro` in the desired folder. Use `BOM.csv` plus `assembly/accessories.csv` for manual shopping and the ZIP under `assembly/` for a bare PCB order. Do not use M1's SMT BOM/CPL for these boards. Front silkscreen identifies every builder-fitted part; back silkscreen provides connector numbers and wiring legends. Native schematic PDFs and front/back/assembly views are under `review/`.

The [Hestore purchasing audit](procurement/README.md) covers every component and assembly accessory, with dated stock, candidate links and replacement limitations. T1.1 adopts ADS1115-M, UES1H100MPM and SMDJ24CA in the authoritative BOMs and footprints. Remaining candidates still need supplier confirmation or a separate reviewed PCB change.

[Printable assembly overview](review/module-overview.pdf) · [All five schematics](review/schematics.pdf)

These are **unbuilt engineering prototypes**. CAD checks establish connectivity, geometry and the documented hand-assembly package choices; they do not establish measured audio quality, ESD performance or startup behavior. Each module guide includes commissioning steps. See [engineering review](review/REVIEW.md), [validation summary](review/validation-summary.json), [design regeneration](design/README.md) and [component sources and substitutions](SOURCES.md).
