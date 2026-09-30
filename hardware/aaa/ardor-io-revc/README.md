# Ardor IO — Rev C, budget SMT

Rev C is an alternate 68 × 46 mm, two-layer board for two-board JLCPCB SMT assembly. It retains DIN MIDI input, passive expression input, mono line output with startup relay mute, and the internal amp feed. Headphones are omitted. The full-featured revision remains in [../ardor-io](../ardor-io/README.md).

Open `Ardor_IO.kicad_pro` in KiCad 9. Use the [connected one-page schematic](review/Ardor_IO_connected.pdf) to follow the circuit, or [Ardor_IO.pdf](Ardor_IO.pdf) for the five editable schematic sheets. The [assembly guide](assembly/README.md) contains the Gerber ZIP, exact JLCPCB BOM, CPL and placement reference.

| For two assembled boards | Previous revision | Rev C |
| --- | ---: | ---: |
| SMT placements per board | 76 | 60 |
| Unique SMT parts | 28 | 24 |
| Extended part types | 15 | 8 |
| Modelled components, including minimums/loss | $39.06 | $16.57 |
| Economic feeder fees at $3.07/type | $46.05 | $24.56 |
| Components + feeder subtotal | $85.11 | $41.13 |

This model predicts **$43.98 less for two boards** in components and feeder fees. It is not a checkout quotation or a guaranteed total. PCB fabrication, setup, stencil, joints, shipping, tax, fixtures, manual parts and discounts are outside the subtotal. The earlier user quote was $108 for two assembled boards; its fee breakdown was not provided. See [cost-comparison.json](assembly/cost-comparison.json), the dated inventory snapshots and [JLCPCB's current price schedule](https://jlcpcb.com/help/article/pcb-assembly-price). Rev C also removes the headphone QFN and its X-ray inspection requirement.

## Changes and tradeoffs

- Two TI TLV9002IDR SOIC-8 dual op-amps replace three OPA2320 devices. The mono buffer feeds two separate coupling capacitors and source resistors. The line and amp outputs share this buffer; simultaneous loads must meet the specified minimum impedances.
- ADS1115, address 0x48, GPIO mapping, pedal polarity selector, H11L1M optocoupler, relay driver and jack protection remain. Firmware can keep the existing MIDI, expression and line-mute interfaces. GPIO17 is unused.
- Mono averaging resistors are 10 kΩ / 1%, shared with other Basic resistors. Worst-case weights are 0.495 and 0.505; opposite-phase inputs can leave a 1% residual, instead of the previous 0.1% resistor target.
- Supply bulk and audio coupling capacitors use Basic Samsung X5R parts. The 50 V, 1206 audio capacitors retain their nominal 10 µF. X5R has a lower temperature limit than X7R and still has voltage-dependent capacitance, microphonics and distortion that require measurement.
- The three 0603 ferrites are Basic Murata BLM18PG121SN1D, 120 Ω at 100 MHz rather than 600 Ω. DC operation is retained; RF attenuation changes. No assembled-enclosure EMC claim is made.
- The outline and mounting coordinates match the compact board. Removed circuitry leaves spare area; enclosure fit, Pi stack height and panel-jack choices still need mechanical confirmation.

The board passes all-severity ERC and DRC, with zero unconnected or parity findings and no exclusions. [Connectivity audit](verification/connectivity-audit.json) checks all **219 electrical pad/net assignments** and 199 intended non-NC connections. The one-page drawing separately checks coverage of all 80 components and wire geometry. This is an engineering prototype; audio, hot-plug, startup and enclosure tests have not been performed.

Read [DESIGN_NOTES.md](DESIGN_NOTES.md) before building. SMT is assembled by JLCPCB; headers, optocoupler, relay, panel connectors and shunts are fitted by hand.
