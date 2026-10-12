# Rev C complete trace-width review

The audit covers all **41 routed nets, 467 track segments and 51 vias**. Seven nets had mixed widths; the completed cleanup leaves only CHASSIS with mixed widths. The count of width-change junctions falls from **40 to 11**. See the [before/after view](trace-widths.svg) and [machine-readable audit](../verification/trace-width-audit.json).

All ordinary signal, supply and relay routes now use **0.20 mm** throughout. The three short GND links use **0.40 mm**. CHASSIS uses **0.60 mm** where it fits and explicitly reviewed **0.20 mm** corridors where necessary. This cleanup changes 69 segment widths: 63 narrower, 6 wider (four analog-supply segments and two CHASSIS segments). It follows the earlier uniform 3.3 V change, whose 37 edits remain independently checked. Junction counts compare widths at shared same-net, same-layer segment endpoints; pad/via and filled-zone geometry are excluded.

## Electrical assessment

- The 5 V supply carries this IO board's loads, including the [nominal 30 mA relay coil](https://omronfs.omron.com/en_US/ecb/products/pdf/en-g5v_1.pdf). Screening it at the documented 100 mA board budget gives a conservative 23.28 mV trace drop, summing all branches; the actual source-to-load path is shorter.
- The relay return is screened at 50 mA: 1.73 mV trace drop. The analog supply is screened at 10 mA: 1.51 mV. [TLV9002 quiescent current](https://www.ti.com/lit/ds/symlink/tlv9002.pdf) is 60 µA per channel typically, plus output loads. Audio, MIDI, expression, I²C and GPIO routes have small normal operating currents and no controlled-impedance requirement in this board.
- Resistance estimates assume nominal 35 µm copper and room-temperature copper resistivity, sum all branches, and exclude component/via resistance. Screening currents are engineering checks rather than new circuit current ratings; startup, fault and assembled-enclosure behavior still require qualification.

## CHASSIS exception

CHASSIS is the return for TVS and RF-protection components. Its fast ESD pulses require a low-impedance return. [TI's ESD layout guide](https://www.ti.com/lit/an/slva680a/slva680a.pdf) explains the importance of minimizing TVS return inductance. Retaining broad copper where space permits is preferable to narrowing the entire return for visual consistency.

A trial with all 45 CHASSIS segments at 0.60 mm produced 46 DRC findings, all involving CHASSIS: shorts, insufficient copper/hole clearance and corresponding solder-mask bridges. Twenty-four segments conflict with neighboring pads/tracks/vias. One additional segment stays at 0.20 mm to avoid inserting a short wide patch between the constrained ends of the JP301/R101 corridor. Two adjoining segments were safely widened to 0.60 mm. The final CHASSIS route has 20 wide and 25 constrained narrow segments. Their UUIDs, obstacles and reasons are recorded in [trace-width-plan.json](../design/trace-width-plan.json).

Making CHASSIS uniformly wide requires rerouting its pad/via corridors. Its existing routing topology is retained in this cleanup; assembled-enclosure ESD performance is not claimed.

## Validation

KiCad 9.0.2, all-severity DRC with all-track and schematic-parity checks: **zero violations, zero unconnected items and zero parity findings**, with no rule relaxation or new exclusions. All 219 electrical pad/net assignments and 199 intended non-NC connections match. Retained route centerlines, copper layers, vias, footprint/pad geometry, outline and isolation keepouts match the frozen layout. Every width is checked against the two explicit plans and the CHASSIS exception list. The two mono-buffer bridges have stable UUIDs for reproducible regeneration.

## Every routed net

| Net | Segments | Widths before (mm) | Widths after (mm) | Changed segments |
| --- | ---: | --- | --- | ---: |
| +3V3_ADC | 38 | 0.20 | 0.20 | 0 |
| +3V3_PI | 15 | 0.20 | 0.20 | 0 |
| +5V_A | 41 | 0.15 / 0.20 / 0.40 | 0.20 | 27 |
| +5V_PI | 25 | 0.20 / 0.40 | 0.20 | 16 |
| /04 / Audio distribution/BIAS_L | 9 | 0.20 | 0.20 | 0 |
| /04 / Audio distribution/BIAS_R | 9 | 0.20 | 0.20 | 0 |
| /04 / Audio distribution/BUF_L | 3 | 0.25 | 0.20 | 3 |
| /04 / Audio distribution/BUF_R | 7 | 0.20 / 0.25 | 0.20 | 1 |
| /05 / Line and amp feed/AMP_AC | 2 | 0.20 | 0.20 | 0 |
| /05 / Line and amp feed/AMP_FEED | 7 | 0.20 | 0.20 | 0 |
| /05 / Line and amp feed/LINE_AC | 9 | 0.20 | 0.20 | 0 |
| /05 / Line and amp feed/LINE_DRIVE | 2 | 0.20 | 0.20 | 0 |
| ADC_SCL | 2 | 0.20 | 0.20 | 0 |
| ADC_SDA | 6 | 0.20 | 0.20 | 0 |
| AUX_L | 5 | 0.20 | 0.20 | 0 |
| AUX_R | 2 | 0.20 | 0.20 | 0 |
| CHASSIS | 45 | 0.20 / 0.60 | 0.20 / 0.60 | 2 |
| EXP_ADC | 22 | 0.20 | 0.20 | 0 |
| EXP_EXC | 5 | 0.20 | 0.20 | 0 |
| EXP_REF_ADC | 15 | 0.20 | 0.20 | 0 |
| EXP_RING | 8 | 0.20 | 0.20 | 0 |
| EXP_TIP | 10 | 0.20 | 0.20 | 0 |
| EXP_WIPER | 6 | 0.20 | 0.20 | 0 |
| GND | 3 | 0.40 | 0.40 | 0 |
| LINE_ENABLE | 9 | 0.20 | 0.20 | 0 |
| LINE_JACK | 11 | 0.20 | 0.20 | 0 |
| MIDI_4 | 11 | 0.20 | 0.20 | 0 |
| MIDI_4_F | 2 | 0.20 | 0.20 | 0 |
| MIDI_5 | 14 | 0.20 | 0.20 | 0 |
| MIDI_A | 9 | 0.20 | 0.20 | 0 |
| MIDI_K | 5 | 0.20 | 0.20 | 0 |
| MIDI_OC | 5 | 0.20 | 0.20 | 0 |
| MIDI_RX | 9 | 0.20 | 0.20 | 0 |
| MONO_BUF | 28 | 0.20 / 0.25 | 0.20 | 8 |
| MONO_MIX | 9 | 0.20 | 0.20 | 0 |
| PI_SCL | 7 | 0.20 | 0.20 | 0 |
| PI_SDA | 16 | 0.20 | 0.20 | 0 |
| RELAY_GATE | 4 | 0.20 | 0.20 | 0 |
| RELAY_LOW | 6 | 0.20 / 0.40 | 0.20 | 5 |
| VREF | 17 | 0.20 / 0.25 | 0.20 | 7 |
| VREF_DIV | 9 | 0.20 | 0.20 | 0 |
