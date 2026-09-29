# Ardor I/O — review after fixes

Updated **29 September 2026**. The user specified an aluminium die-cast enclosure grounded through the jack connectors and authorized the remaining fixes. The current KiCad schematic, PCB and BOM now include those changes. [Original review and pre-fix evidence](baseline/REVIEW.md) remain archived separately.

**The reviewed design corrections are implemented and pass CAD verification.** The board remains an engineering prototype requiring the existing bench and enclosure tests. Full implementation details and verification are in [FIXES.md](FIXES.md); current CAD input hashes are in [source-sha256.txt](source-sha256.txt).

| Original finding | Current disposition |
|---|---|
| Undefined physical chassis bond | Defined by the user's enclosure arrangement: CHASSIS → R101 → GND → wired audio sleeves → metal bushings → enclosure. Notes now show this path. High-frequency return behavior remains a bench qualification item. |
| Relay MOSFET gate-drive margin | Q501 changed to AO3400A, with specified on-resistance at 2.5 V gate drive and the same G1/S2/D3 pinout. |
| MIDI input shell bond | Shell left unconnected and insulated from the grounded enclosure; audio jack bonding remains intact. |
| C501 part/land ambiguity | Panasonic EEEFK1C470P selected; local manufacturer-specific lands installed in the PCB and schematic. |
| Stale notes and calculations | Corrected input capacitance/pole, loaded amp gain, and expression endpoint-calibration claims in the relevant source notes and builder. |

The [connected single-page PDF](Ardor_IO_connected.pdf) and [SVG](Ardor_IO_connected.svg) now show the corrected circuit. The original six-sheet [KiCad PDF](../Ardor_IO.pdf) has also been regenerated. All 276 numbered PCB pad assignments match the schematic, with no changes to the original net topology.

## Circuit-by-circuit assessment

| Function | Review result | Remaining qualification |
|---|---|---|
| Pi power and GPIO | Correct physical pins: 1 = 3V3, 2/4 = 5V, 10 = GPIO15 RX, 11 = GPIO17, 15 = GPIO22; all eight ground contacts connected. Audio and ADC have separate filtered branches. | Confirm host model, UART/console configuration, rail droop and stack orientation. FB101/FB102 filter; they do not regulate or prevent reverse power. |
| Codec integration | L/GND/R harness is explicitly the expansion's definition. Reserved Codec Zero I²S and LED/button pins are not borrowed. | Map the harness to actual AUX OUT pads, verify mixer routing and DC level. Official nominal AUX level is 1 Vrms. [Raspberry Pi audio documentation](https://www.raspberrypi.com/documentation/accessories/audio.html). |
| MIDI current loop | 220R series input, antiparallel D201, correct LED/output polarity, isolated input nets, 3.3 V pull-up despite 5 V optocoupler supply. | Test weak transmitters, 3.3/5 V sources, cable length, temperature, edge shape and UART errors. H11L1M's 1.6 mA turn-on maximum at specified conditions leaves nominal 5 mA margin; lifetime guard band is advised. [onsemi H11L1M](https://www.onsemi.com/pdf/datasheet/h11l3m-d.pdf). |
| Expression ADC | ADS1115 DGS pinout, grounded address = 0x48, both input clamp orientations, 33R I²C series resistors and local bypass are consistent. Both jumper settings map correctly. | High source impedance and clamp leakage limit accuracy; ±4.096 V PGA does not extend the allowed pin voltage past its supply limits. Allow conversions after mux changes and avoid pairing stale channel values. [TI ADS1115, input circuit and conversion operation](https://www.ti.com/lit/gpn/ads1115). |
| Stereo input / mono | U401A/B are unity followers. R403/R404 feed a high-impedance node, producing `(L+R)/2`; U402A buffers it. U402B buffers the 10k/10k midpoint. No large capacitor directly loads VREF. | Measure codec level, noise and clipping with all outputs active. Antiphase cancellation is expected. OPA2320 5 V operation and SOIC pin assignment are appropriate. [TI OPA2320](https://www.ti.com/lit/ds/symlink/opa2320.pdf). |
| Line output | Separate unity driver, correctly polarized C501, 10k discharge resistor, 100R output isolation, and correct NC/NO/common connections. Resting relay grounds the external tip and disconnects the source. | AO3400A and the specified Panasonic capacitor/lands are implemented. Measure cable stability, shorts, and turn-on/shutdown/brownout transients. |
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
- **Mechanics:** J101 stack height/orientation and mounting-hole positions are still provisional. Confirm the board's four M3 holes against the actual enclosure; they are not a Pi mounting pattern. Check the height of C501, relay, H11L1M and stacked connectors. Audio jack sleeves/bushings bond to the aluminium enclosure; the MIDI input shell must be insulated from it.
- **Serviceability:** references are on F.Fab, with functional connector labels on silkscreen. Keep an assembly drawing available for debugging; do not assemble solely from the silkscreen preview.

## What was verified, and what was not

Fresh checks used **KiCad 9.0.2**, against the updated design in an isolated Debian container:

| Check | Result | Evidence |
|---|---|---|
| Schematic ERC, all severities | 0 violations | [fresh-erc.json](fresh-erc.json) |
| PCB DRC, all track errors, all severities | 0 violations | [fresh-drc.json](fresh-drc.json) |
| Unconnected items | 0 | Same DRC report |
| Schematic parity | 0 issues | Same DRC report |
| Independent current netlist → board pads | All 276 unique numbered pad assignments match, including NC pads | [source-audit.json](source-audit.json), [audit_source.py](audit_source.py) |
| Saved versus fresh schematic connectivity | Identical after normalizing KiCad's slash representation | Same audit |
| New drawing coverage | 99 components; all 260 connected, non-NC component pins represented | [drawing-audit.json](drawing-audit.json) |
| New drawing wire geometry | No different-net shorts or separated signal-net groups under the drawing's junction convention | Same audit and [draw_connected.py](draw_connected.py) |

The command-line environment initially used system libraries and reported footprint-library warnings. Mapping the vendored package namespaces to this project's snapshots eliminated them; no DRC rules or exclusions were changed. The current project does have several ordinary footprint/courtyard rule categories set to `ignore`; “all severities” does not override disabled rules. No claim is made that every possible KiCad rule is enabled.

This review includes manual circuit reasoning, data-sheet pin/configuration checks, geometry inspection, and nominal calculations. It does **not** include a manufactured board, powered measurements, SPICE transient/noise modeling, transmission/field simulation, measured EMI/ESD immunity, or a complete manufacturer-specific land-pattern audit of every generic passive.

## Focused next revision and bring-up sequence

1. The reviewed part substitutions and note corrections are implemented and pass CAD checks. Keep the verified signal topology unless measurements justify changing it; re-run CAD parity and DRC after subsequent edits.
2. Finalize panel/stack/amp harness mechanics and remaining generic passive procurement. Confirm audio-sleeve/enclosure continuity and MIDI-shell isolation, then review connector and polarity orientation before ordering.
3. Power with both enables low; verify rails, approximately 2.5 V VREF and biased op-amp outputs, near-zero external output DC, and expected idle current. Verify the relay's supply/coil voltage at minimum input supply.
4. Confirm Codec Zero routing and begin below full volume. Measure line/amp/headphone gain, bass response, noise and clipping with simultaneous loads. Check 32/80/250-ohm headphones and cable capacitance; inspect op-amp outputs for high-frequency oscillation.
5. Capture hot-plug, short, enable, brownout and sudden power-off waveforms. Test the external amp's independent mute. Establish the required silence/settling delay from measurements.
6. Exercise MIDI source types and dense traffic; sweep the expression pedal, jumper modes and open/short contacts. Check ADC recovery and ratio stability. At 128 conversions/s, alternating two channels gives at most approximately 64 samples/s per channel before overhead.
7. Test exposed ports and shell/enclosure behavior with the actual enclosure and cabling. Set acceptance criteria for reset, corruption, latchup and damage as well as analog performance.
