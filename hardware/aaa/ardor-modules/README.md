# Ardor independent function modules — M1

Build only the features you need. Each folder is a complete, separately orderable KiCad 9 project with its own circuitry, local libraries, single-page wired schematic, PCB, SMT BOM/CPL and Gerber ZIP. **No module requires another Ardor module or a carrier board.** The existing integrated Rev C remains available at `../ardor-io-revc`.

“Independent” means an external host/audio source and the listed regulated power are sufficient. It does not mean a MIDI receiver works without a UART host, or an audio output produces sound without a DAC. Local ADC, analog midrail, filtering, charge pump and protection are included wherever needed. No shared reference voltage is exported. **Host connector pin assignments differ between module types; use each guide’s pin table, not an interchangeable power harness.** Panel sockets are cabled to standard headers, making the boards independent of enclosure connector spacing.

[One-page module overview](review/module-overview.pdf) · [All five schematics, one page each](review/schematics.pdf)

| Module | PCB mm | External power budget | SMT / board | Two-board component model + Extended fees |
|---|---|---|---:|---:|
| [DIN MIDI input](midi-in/README.md) | 34 × 28 | 5 V / 10 mA and 3.3 V / 5 mA | 14 | $5.22 + $9.21 |
| [Expression pedal](expression/README.md) | 44 × 34 | 3.3 V / 15 mA | 23 | $8.06 + $9.21 |
| [Stereo buffer and mono mixer](mixer/README.md) | 40 × 30 | 5 V / 10 mA | 25 | $6.88 + $6.14 |
| [Mono line output with relay mute](line-out/README.md) | 46 × 34 | 5 V / 50 mA | 22 | $8.52 + $12.28 |
| [Optional stereo headphones](headphones/README.md) | 36 × 30 | 5 V / 50 mA | 18 | $9.93 + $12.28 |

The optional headphone board adds approximately $3.28 QFN X-ray for two assembled boards. The table is **not a total assembly quote**: PCB, setup, stencil, joints, shipping, tax and manual parts are excluded. Components use the public catalogue unit price, two-board quantities and SMT attrition/minimum-patch allowances, checked 2 October 2026. Wholesale `preMinPurchaseNum` is not treated as an assembly minimum. Extended fees use the current Economic $3.07/type model. Stock is not reserved. Ordering a subset removes unused circuits; **ordering all five separately can cost more than the integrated board because setup charges repeat**. Upload designs separately rather than as one assembled panel.

## What is included

- MIDI input to a 3.3 V UART; no MIDI output or USB MIDI.
- Passive expression pedal input with its own ADS1115, polarity/address jumpers and selectable I²C pull-ups.
- Line-level stereo buffers and mono average, with independent AC-coupled outputs.
- Mono line output with relay startup mute and a separate internal amplifier feed. The relay is **not true bypass** and the feed is **not a speaker amplifier**.
- Optional stereo headphone output with its own charge pump.

The DAC/codec, processing host, guitar/instrument preamp, power supply and speaker power amplifier remain external. They were not separate functions implemented by the integrated Rev C IO board.

## Mechanical and grounding choices

Each board is two-layer, with front/back GND pours, local return vias and no routed GND track segments. All signal/supply tracks are **0.20 mm** throughout, including pad approaches. CHASSIS tracks are uniformly **0.60 mm** throughout; there are no mid-trace neckdowns in the released boards. Ground vias are 0.60/0.30 mm; the headphone QFN also has 0.50/0.20 mm exposed-pad holes. No blind/buried vias or impedance-controlled stackup is required.

Expression, line-out and headphone panel sleeves/bushings bond the aluminium enclosure to local ground through the local CHASSIS link. MIDI has its own chassis terminal for builds that contain only MIDI; its DIN shell must be insulated. The mixer is an internal harness board and shares host GND. Modules used together should share host power/ground deliberately; avoid relying on signal cables as their only return. Functional MIDI isolation is not certified safety isolation, and EMC/ESD performance remains a bench-test item.

## Assembly and review

JLCPCB assembles SMT only. Fit the headers, expression shunts, MIDI DIP optocoupler and line relay by hand. Panel sockets/wiring, mounting hardware, host and enclosure are separate DIY items. Each module guide gives exact pin maps, polarity settings, supply limits, manual parts and commissioning steps. Component references are on front fabrication drawings; connector names and pin legends are on the back silkscreen.

All five native ERC/DRC checks, including unconnected and schematic parity checks at every severity, pass with zero findings and no exclusions. Independent schematic-wire partition checks and all physical pad/net assignments pass. All 102 SMT placements across the five designs map to 27 exact inventory C-codes with sufficient snapshot stock for two copies of each selected design. The stock/identity snapshot, electrical checks and SHA256 manifests are included per project. Supplier placement preview and physical commissioning remain outstanding; these are prototype designs, not tested production releases.

[All-module validation](review/validation-summary.json) · [Regeneration comparison](review/regeneration-validation.json) · [Sources and design assumptions](SOURCES.md) · [Regeneration and verification](design/README.md)
