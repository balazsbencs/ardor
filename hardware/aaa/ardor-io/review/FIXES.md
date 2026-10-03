# Review fixes — 29 September 2026

The schematic, PCB, BOM, local symbol/footprint libraries, generation script, six-sheet PDF and connected one-page drawing now agree on the review corrections. Original review evidence is retained under [baseline/](baseline/REVIEW.md).

## Implemented changes

The subsequent [track width cleanup](TRACK_WIDTH_REVIEW.md) widens 36 complete segments and shortens 22 neckdowns while preserving all route centerlines. Fresh DRC and all 276 pad/net checks pass. [Before/after views](track-width-cleanup.png) show representative changes.

| Item | Implemented resolution |
|---|---|
| Enclosure grounding | Adopted the specified aluminium die-cast enclosure, bonded by the audio jack sleeves/bushings. Keep the wired sleeve returns on J302.3, J503.2 and J602.3. R101 remains 0 Ω: PCB CHASSIS → R101 → GND → sleeve wiring → jack bushing → enclosure. No extra chassis connector was added. |
| Relay driver Q501 | Replaced 2N7002 with **Alpha & Omega AO3400A**. Same SOT-23 geometry and pins: 1 gate, 2 source, 3 drain. R503/R504 and flyback D502 are retained. |
| MIDI input shell | Changed schematic and assembly notes to require an insulated DIN socket/mounting arrangement. The mating shield/shell is unconnected and isolated from the metal enclosure. Pins 1, 2 and 3 remain NC. This does not change the audio jack bonding. |
| C501 | Selected **Panasonic EEEFK1C470P**, 47 µF / 16 V, 6.3 × 5.8 mm. Added a local footprint using two 3.2 × 1.6 mm rectangular lands, 5.0 mm center spacing and 1.8 mm inner gap. The original positive/negative nets and placement are retained. |
| Stale calculations | Corrected the 10 µF input pole to 0.159 Hz, loaded amp-feed gain to 0.9804, and expression notes to acknowledge residual pot-loading nonlinearity after endpoint calibration. |

AO3400A has a 48 mΩ maximum on-resistance specification at VGS = 2.5 V under the datasheet conditions. The existing gate divider gives 3.267 V nominal, and 30 mA × 48 mΩ gives an estimated 1.44 mV driver drop at the specified reference temperature. [AOS datasheet](https://www.aosmd.com/res/data_sheets/AO3400A.pdf).

The C501 lands follow Panasonic's standard size D drawing. Its 5.8 ±0.3 mm height must be included in stack clearance. The incorrect 5.4 mm-height 3D model was removed; no replacement mechanical model is claimed. [Panasonic FK dimensions, recommended lands and selection table](https://industrial.panasonic.com/cdbs/www-data/pdf/RDE0000/ABA0000C1181.pdf).

The MIDI input shield remains isolated because the chosen audio-jack enclosure bonds connect the enclosure to receiver ground. [MIDI CA-033, page 3](https://midi.org/wp-content/uploads/wpforo/default_attachments/1709416667-ca33-MIDI-10-Electrical-Specification-Update.pdf).

## Verification

- KiCad 9.0.2: **zero ERC violations; zero DRC violations; zero unconnected items; zero schematic parity issues**. Local footprint libraries were loaded; no rules or exclusions were changed.
- All **276 numbered PCB pad assignments** match the current schematic; the full pin/net map is unchanged from the original reviewed circuit.
- The BOM, schematic and PCB agree on both replacement part numbers, datasheets and footprints. The capacitor land dimensions and critical transistor/capacitor/ground pin assignments pass [verify_review_fixes.py](../design/verify_review_fixes.py). Results: [fix-verification.json](fix-verification.json).
- Existing schematic invariants, board geometry/courtyard checks and Samsung coupling-capacitor checks also pass. The routed board remains 68 × 46 mm, with 95 electrical components and four mounting holes. No tracks were rerouted.
- The updated [one-page PDF](Ardor_IO_connected.pdf) represents all 99 components and 260 connected pins. Its geometry audit reports no signal opens or shorts. The six-sheet [KiCad PDF](../Ardor_IO.pdf) and front/back board previews were regenerated.

## Assembly and bench items

The enclosure choice resolves the missing *physical bond definition*. It does not prove high-frequency ESD performance of the existing shared CHASSIS/GND/harness path. Confirm bare-metal jack contact, sleeve-to-enclosure continuity and DIN-shell isolation after assembly. Keep the sleeve wires short and retain their explicit PCB ground connections.

Low-supply/hot-coil relay pickup, audio performance, startup/shutdown pops, expression response, and assembled-enclosure ESD still require hardware measurements. These are the original prototype qualification tasks, not failed CAD checks. See [DESIGN_NOTES.md](../DESIGN_NOTES.md).
