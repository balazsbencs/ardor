> Archived pre-fix review. The current design incorporates the authorized fixes; see [current status](../FIXES.md).

# Ardor I/O — independent schematic and PCB review

Reviewed **29 September 2026**, against repository commit `08d34ce63a3c0cefa48f76d85e0f98d2bd15e2ea` and the source hashes in [source-sha256.txt](source-sha256.txt). Scope: all six schematic sheets, BOM, embedded board footprints, current 68 × 46 mm routed board, design notes, and component/interface references. The historical `pcb-plan/` is not the current PCB.

**Assessment: the basic circuit architecture and pin connectivity are sound, but I would resolve the chassis implementation, relay drive margin, MIDI shell instructions, and C501 procurement/land-pattern definition before a production order.** There is no discovered CAD short, open, or schematic-to-board mismatch. Passing CAD checks does not establish audio quality, transient immunity, or mechanical fit.

The source files were not changed. The [connected single-page PDF](Ardor_IO_connected.pdf) and [SVG](Ardor_IO_connected.svg) show the existing circuit, including its outstanding issues, rather than an unapproved redesign.

## Findings requiring action

### 1. High — the chassis return described in the notes has no implemented enclosure terminal

**Evidence:** R101; D203/D204, D301/D302, D501, D601/D602; root power sheet and routed PCB. The only ten numbered pads on CHASSIS belong to protection components, C202/C203, and R101. None belongs to a connector, exposed bonding terminal, or mounting hole. H1–H4 have unnumbered, unconnected mechanical holes. R101 bonds this network to signal GND at board-relative (15, 43) mm. The CHASSIS network has **128.779 mm total routed track**, with widths from **0.20 to 0.60 mm**. This is the sum of the branching network, not a claim that every discharge follows a 129 mm path. See [source-audit.json](source-audit.json).

**Consequence:** the instruction to bond CHASSIS at connector entry cannot be implemented through a defined board terminal. As drawn, protection current returns through R101 into board/Pi ground unless an additional physical connection is improvised. A net name and a zero-ohm resistor do not provide an enclosure discharge path. Long shared paths also couple connector transients into sensitive circuits.

**Action:** define the actual enclosure, add a documented low-inductance bond point or connector-entry protection assembly, and route each exposed-port protection return to that structure. Revisit R101's location and the enclosure/GND relationship. If the intended enclosure is insulating, design and document the return strategy explicitly. Inspect the assembled geometry and test it; do not infer immunity from TVS component ratings.

### 2. Medium — Q501 relay operation is not guaranteed by its specified gate-drive conditions

**Evidence:** J101.15 → R503 1k → Q501 gate, with R504 100k to GND. Nominal gate voltage is approximately `3.3 × 100k / 101k = 3.267 V`. The selected Nexperia 2N7002 specifies maximum on-resistance at 4.5 V and 10 V gate drive, not 3.3 V. Threshold voltage is measured at only 0.25 mA and does not prove useful relay drive. [Nexperia 2N7002, table 7](https://assets.nexperia.com/documents/data-sheet/2N7002.pdf).

The G5V-1 DC5 coil is nominally 167 ohms / 30 mA; its standard must-operate rating is 80% of 5 V at the stated reference conditions. With a 4.75 V source, approximately 0.75 V is available for driver and distribution losses before reaching that 4.0 V threshold. This is a margin check, not a complete temperature analysis. [Omron G5V-1, coil ratings](https://components.omron.com/us-en/system/files/2023-01/datasheet_pdf/K048-E1.pdf).

**Action:** choose a pin-compatible MOSFET with guaranteed on-resistance at 2.5 V or below, checking pin order and package; alternatively validate worst-case drive with the exact procured device and temperature range. The existing circuit will likely operate at room temperature, but that is not a production guarantee. Flyback D502 and the gate pulldown have the correct topology and polarity.

### 3. Medium — the MIDI input shell-bond instruction conflicts with the cited interface specification

**Evidence:** sheet 02 says to bond the DIN metal shell mechanically to the enclosure. DESIGN_NOTES also calls for shell bonding, while R101 is intended to bond enclosure/chassis and logic ground.

CA-033 distinguishes the input socket's shield contact from output/thru shield contacts: the input shield must not have a DC path to receiver ground; a small capacitor is allowed. DIN pin 2 is correctly left open in the electrical schematic. [MIDI Association CA-033, page 3](https://midi.org/wp-content/uploads/wpforo/default_attachments/1709416667-ca33-MIDI-10-Electrical-Specification-Update.pdf).

**Action:** resolve the chosen socket's shield contact, mounting metal, and enclosure bond as a mechanical/electrical drawing. Where the shell is electrically the mating shield contact, use an insulated mounting arrangement and the permitted capacitive connection or leave it open. Do not implement the blanket direct-bond instruction without checking this distinction. This is an integration/documentation defect, not a claim that the optocoupler LED is wired incorrectly.

### 4. Medium — C501's exact purchase part and land pattern are unresolved

**Evidence:** BOM specifies `Panasonic EEE-FK1C470R`, while the board uses `CP_Elec_6.3x5.4`. Its copper pads are 3.5 × 1.6 mm at ±2.8 mm, giving a 2.1 mm inner gap. Panasonic's current FK selection table lists the 47 µF / 16 V choices `EEEFK1C470UR` (5.0 × 5.8 mm) and `EEEFK1C470P` (6.3 × 5.8 mm); the exact BOM string without `U` or `P` is not listed. The recommended standard land gaps are 1.5 mm for the 5 mm case and 1.8 mm for the 6.3 mm case. [Panasonic FK catalog, dimensions/lands and selection table, pages 1–3](https://industrial.panasonic.com/cdbs/www-data/pdf/RDE0000/ABA0000C1181.pdf).

**Consequence:** CAD parity checks a footprint identifier against the same identifier; it cannot determine whether the ordered part is correct. The present pads may be usable for a selected part, but their assembly suitability has not been established by this comparison. Absence from the current table does not prove that an older part never existed.

**Action:** freeze an available exact manufacturer number, validate its terminal/land drawing and height, then update BOM, schematic footprint, and board together as needed. Obtain assembly acceptance of any deliberate land-pattern deviation. Do not silently substitute the 5 mm version onto a footprint chosen for the 6.3 mm case.

### 5. Low — documentation contains stale values and a gain calculation error

- Sheet 04 still states `2.2u/100k` and approximately 0.72 Hz, although C401/C402 are now 10 µF. Their nominal individual pole is **0.159 Hz**. DESIGN_NOTES' opening architecture paragraph still describes 2.2 µF film capacitors.
- DESIGN_NOTES gives approximately 0.990 gain for the amp feed into a 100k external load. R506 is another 100k shunt at AMP_FEED: the effective load is 50k, so the correct midband ratio is **50k / (50k + 1k) = 0.9804**. The unloaded ratio is approximately 0.9901. The quoted loaded pole of approximately 0.32 Hz is consistent with the loaded circuit.
- Sheet 03 says endpoint calibration removes pot-loading error. The design notes more accurately acknowledge residual nonlinearity. With an ideal 100k pot and only R303's 1M load, half travel gives **0.4878** of excitation, not 0.5000; three-quarter travel gives **0.7362**, not 0.7500. Both endpoints remain unchanged, so endpoint calibration cannot eliminate this error.

**Action:** synchronize schematic notes, DESIGN_NOTES and generation scripts. For expression accuracy, specify an acceptable travel-error budget and use a measured response curve or a lower source impedance if needed. These are documentation/performance corrections, not evidence of failed MIDI or audio connectivity.

## Circuit-by-circuit assessment

| Function | Review result | Remaining qualification |
|---|---|---|
| Pi power and GPIO | Correct physical pins: 1 = 3V3, 2/4 = 5V, 10 = GPIO15 RX, 11 = GPIO17, 15 = GPIO22; all eight ground contacts connected. Audio and ADC have separate filtered branches. | Confirm host model, UART/console configuration, rail droop and stack orientation. FB101/FB102 filter; they do not regulate or prevent reverse power. |
| Codec integration | L/GND/R harness is explicitly the expansion's definition. Reserved Codec Zero I²S and LED/button pins are not borrowed. | Map the harness to actual AUX OUT pads, verify mixer routing and DC level. Official nominal AUX level is 1 Vrms. [Raspberry Pi audio documentation](https://www.raspberrypi.com/documentation/accessories/audio.html). |
| MIDI current loop | 220R series input, antiparallel D201, correct LED/output polarity, isolated input nets, 3.3 V pull-up despite 5 V optocoupler supply. | Test weak transmitters, 3.3/5 V sources, cable length, temperature, edge shape and UART errors. H11L1M's 1.6 mA turn-on maximum at specified conditions leaves nominal 5 mA margin; lifetime guard band is advised. [onsemi H11L1M](https://www.onsemi.com/pdf/datasheet/h11l3m-d.pdf). |
| Expression ADC | ADS1115 DGS pinout, grounded address = 0x48, both input clamp orientations, 33R I²C series resistors and local bypass are consistent. Both jumper settings map correctly. | High source impedance and clamp leakage limit accuracy; ±4.096 V PGA does not extend the allowed pin voltage past its supply limits. Allow conversions after mux changes and avoid pairing stale channel values. [TI ADS1115, input circuit and conversion operation](https://www.ti.com/lit/gpn/ads1115). |
| Stereo input / mono | U401A/B are unity followers. R403/R404 feed a high-impedance node, producing `(L+R)/2`; U402A buffers it. U402B buffers the 10k/10k midpoint. No large capacitor directly loads VREF. | Measure codec level, noise and clipping with all outputs active. Antiphase cancellation is expected. OPA2320 5 V operation and SOIC pin assignment are appropriate. [TI OPA2320](https://www.ti.com/lit/ds/symlink/opa2320.pdf). |
| Line output | Separate unity driver, correctly polarized C501, 10k discharge resistor, 100R output isolation, and correct NC/NO/common connections. Resting relay grounds the external tip and disconnects the source. | Resolve findings 2/4, measure cable stability and shorts, and capture turn-on, shutdown and brownout transients. |
| Internal amp feed | Separate driver and DC block; R505 isolates the external load. Header ground is provided. | External amplifier must implement mute and define input impedance. It is not a speaker output. Correct loaded gain is 0.9804. |
| Headphones | Correct single-ended negative inputs, grounded positive inputs, gain straps, output channel assignment, and distinct internally generated HPVDD/HPVSS nets. CPP–CPN flying capacitor is correct. | Check actual load power, clipping, pop, crosstalk, and switching noise. SGND must sense the jack ground while pump bypass returns go to PGND. [TI TPA6132A2, sections 5 and 10](https://www.ti.com/lit/ds/symlink/tpa6132a2.pdf). |
| Protection | Connector TVS devices are bidirectional; ADCs also have resistors and secondary rail clamps. | The 5 V TVS is not a 5 V clamp: specified pulse clamping is up to 10 V at 1 A and 14 V at 12 A. Evaluate residual stress, especially behind 2.2R headphone resistors. These surge numbers do not directly model an IEC ESD strike. [Nexperia PESD5V0S1BA](https://assets.nexperia.com/documents/data-sheet/PESD5V0S1BA.pdf). |

## Nominal calculations independently checked

Assumptions: ideal op-amps within headroom, nominal component values, low-impedance codec source, settled DC bias. These are circuit calculations, not simulated or measured performance.

| Quantity | Calculation/result |
|---|---|
| Input high-pass, each channel | `1 / (2π × 100k × 10µ) = 0.159 Hz` |
| Midrail filter | `10k || 10k = 5k`; with 10µ, pole ≈ 3.18 Hz, time constant 50 ms |
| Audio headroom at 5 V | 1 Vrms = 1.414 V peak: nominal biased waveform 1.086–3.914 V |
| Line, 10k external load | Gain `10k / 10.1k = 0.9901`; C501 sees `10k || 10.1k`, giving 0.674 Hz |
| Amp, 100k external load | Gain `50k / 51k = 0.9804`; pole `1 / (2π × 51k × 10µ) = 0.312 Hz` |
| Headphones, 32 ohms | Approximate signed gain `−0.5 × 32 / 34.2 = −0.4678`; 0.468 Vrms and 6.84 mW for 1 Vrms AUX |
| HP coupling pole | Approximately `1 / (2π × 26.4k × 10µ) = 0.603 Hz`, using typical input resistance |
| Expression excitation short | `3.3 V / 1k = 3.3 mA` nominal |
| Expression filter, middle travel | `(25k || 1M) + 10k ≈ 34.39k`; with 100n, ≈46.3 Hz, excluding ADC loading and excitation-source impedance |
| Bias settling scale | Input RC is 1 s. A hypothetical 2.5 V step leaves approximately 16.8 mV after 5 s in a single ideal RC. Actual startup has several interacting nodes; a fixed delay is not proof of inaudible switching. |

The 10 µF / 50 V Samsung coupling parts are nonpolar X7R in 1206, consistent with their published dimensions and nominal specification. [Samsung CL31B106KBHNNN family](https://product.samsungsem.com/cn/mlcc/CL31B106KBHNNN.do). Voltage-dependent capacitance, dielectric distortion and microphonics still need measurement. Increasing capacitance reduces the AC voltage across a coupling capacitor but does not eliminate dielectric effects. [TI capacitor distortion application note](https://www.ti.com/lit/an/slyt796a/slyt796a.pdf).

## PCB, assembly and integration

- **Layout extent:** current board is 68 × 46 mm, two copper layers, 95 electrical footprints plus four mounting holes. Existing front/back previews were inspected along with the board data. Courtyard and clearance checks pass with the project's enabled rules. This does not establish that a stacked HAT, plug body, standoff or enclosure fits.
- **Grounding:** both copper layers contain GND pours. The MIDI island has a pour keepout and a separate 3.2 mm-wide track/via/pour keepout corridor beside the optocoupler. Those features are useful, but the schematic's blanket ≥3 mm separation around all input nets is not independently enforced as a dedicated net-class clearance. Do not interpret the ordinary 0.15 mm DRC as a 3 mm isolation certificate. This is functional ground-loop isolation, not a safety isolation barrier; the chassis TVS parts intentionally limit common-mode isolation.
- **Headphone placement:** U601.14 to C603.1 is 4.294 mm straight-line pad-center distance, and to C604.1 is 2.128 mm. Reservoir/flying-capacitor pin-to-pad distances are approximately 2.57–2.77 mm. These are compact placements, but distances alone do not measure loop inductance. Inspect actual supply/return loops and SGND-to-jack routing, especially because the ground plane is cut by two-layer routing. C603's placement is within TI's 5 mm guidance by direct pad distance; actual path length and return geometry still matter. [TI TPA6132A2, supply and layout sections](https://www.ti.com/lit/ds/symlink/tpa6132a2.pdf).
- **Assembly definition:** freeze exact C101/C103/C403/C603–C607 parts and effective capacitance under bias. U601's footprint includes 0.20 mm thermal drills; agree solder-paste and via treatment with the fabricator/assembler. Current drawings alone do not establish solder-wicking yield.
- **Mechanics:** J101 stack height/orientation and mounting-hole positions are still provisional. Confirm the board's four M3 holes against the actual enclosure; they are not a Pi mounting pattern. Check the height of C501, relay, H11L1M and stacked connectors. Audio jacks must be the intended insulated type.
- **Serviceability:** references are on F.Fab, with functional connector labels on silkscreen. Keep an assembly drawing available for debugging; do not assemble solely from the silkscreen preview.

## What was verified, and what was not

Fresh checks used **KiCad 9.0.2**, with the design mounted read-only in an isolated Debian container:

| Check | Result | Evidence |
|---|---|---|
| Schematic ERC, all severities | 0 violations | [fresh-erc.json](fresh-erc.json) |
| PCB DRC, all track errors, all severities | 0 violations | [fresh-drc.json](fresh-drc.json) |
| Unconnected items | 0 | Same DRC report |
| Schematic parity | 0 issues | Same DRC report |
| Independent current netlist → board pads | All 276 unique numbered pad assignments match, including NC pads | [source-audit.json](source-audit.json), the source audit |
| Saved versus fresh schematic connectivity | Identical after normalizing KiCad's slash representation | Same audit |
| New drawing coverage | 99 components; all 260 connected, non-NC component pins represented | [drawing-audit.json](drawing-audit.json) |
| New drawing wire geometry | No different-net shorts or separated signal-net groups under the drawing's junction convention | Same audit and the drawing generator |

The command-line environment initially used system libraries and reported footprint-library warnings. Mapping the three vendored package namespaces to this project's snapshots eliminated them; no DRC rules or exclusions were changed. The current project does have several ordinary footprint/courtyard rule categories set to `ignore`; “all severities” does not override disabled rules. No claim is made that every possible KiCad rule is enabled.

This review includes manual circuit reasoning, data-sheet pin/configuration checks, geometry inspection, and nominal calculations. It does **not** include a manufactured board, powered measurements, SPICE transient/noise modeling, transmission/field simulation, measured EMI/ESD immunity, or a complete manufacturer-specific land-pattern audit of every generic passive.

## Focused next revision and bring-up sequence

1. Resolve findings 1–4; synchronize the notes in finding 5. Keep the proven signal topology unless measurements justify changing it. Re-run CAD parity and DRC after any edit.
2. Freeze the panel/stack/amp harness drawing and exact procurement list; review connector and polarity orientation before ordering.
3. Power with both enables low; verify rails, approximately 2.5 V VREF and biased op-amp outputs, near-zero external output DC, and expected idle current. Verify the relay's supply/coil voltage at minimum input supply.
4. Confirm Codec Zero routing and begin below full volume. Measure line/amp/headphone gain, bass response, noise and clipping with simultaneous loads. Check 32/80/250-ohm headphones and cable capacitance; inspect op-amp outputs for high-frequency oscillation.
5. Capture hot-plug, short, enable, brownout and sudden power-off waveforms. Test the external amp's independent mute. Establish the required silence/settling delay from measurements.
6. Exercise MIDI source types and dense traffic; sweep the expression pedal, jumper modes and open/short contacts. Check ADC recovery and ratio stability. At 128 conversions/s, alternating two channels gives at most approximately 64 samples/s per channel before overhead.
7. Test exposed ports and shell/enclosure behavior with the actual enclosure and cabling. Set acceptance criteria for reset, corruption, latchup and damage as well as analog performance.
