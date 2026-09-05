# Ardor Codec Zero I/O — Rev A

Editable KiCad 9 schematic, six A3 sheets. This is an engineering prototype design, with production-oriented protection and documentation. It is **not a hardware-validated production release**. No PCB is included.

## Agreed interfaces

- Regulated 5 V supply shared with Raspberry Pi. The expansion takes power from the Pi header; it does not add a separate power-input connector or a 9 V pedal-supply converter.
- One 5-pin, 180-degree female DIN MIDI input.
- One 6.3 mm TRS passive expression input: nominal 100 kΩ linear potentiometer. Tip is the wiper, ring is excitation, sleeve is ground. A two-shunt selector reverses tip and ring.
- One mono, unbalanced 6.3 mm TS line output, intended for loads of at least 10 kΩ.
- One stereo 3.5 mm TRS headphone output, designed around 32 Ω or higher loads. High-impedance headphones will be quieter; this is not a high-voltage studio headphone amplifier.
- An internal, AC-coupled mono feed to the user's separate amp circuit. The amplifier and its external connector/protection remain outside this design.

## Files and opening

Open `Ardor_IO.kicad_pro`, then open its schematic. The root sheet links to the five functional sheets. `Ardor.kicad_sym` and `sym-lib-table` make the project portable; symbols are also embedded in the schematic files. Use `Ardor_IO.pdf` for review and printing. `BOM.csv` includes one row per physical component, with critical IC/protection part numbers and assigned PCB footprints.

The panel jacks and DIN socket are intentionally marked **off-board**. Their T/R/S or DIN contact numbers describe panel wiring, not the pad numbering of an unspecified PCB jack. Board-side harness headers are J202 (MIDI 4/5), J302 (expression T/R/S), J503 (line T/S) and J602 (headphone T/R/S). J102 and J502 are the Codec AUX and amp board headers. Select mechanically suitable insulated panel audio jacks and bond their sleeves through the specified ground wiring. Select the enclosure and stack-through connector height during mechanical design; the J101 part-family entry is provisional for that reason.

## Architecture and signal levels

Codec Zero AUX L and R each see a 2.2 µF film coupling capacitor and a 100 kΩ bias resistor into a unity buffer. The intended nominal maximum at AUX is 1.0 Vrms per channel. The output circuitry must be commissioned at a lower digital volume first; the actual Codec Zero configuration and output amplitude have not been measured here.

U401 buffers the stereo channels. R403/R404 and U402A form the average `(L + R) / 2`. This retains nominal amplitude for coherent dual-mono material. Antiphase stereo cancels, as it does in any mono fold-down. Never short the codec's L and R outputs together. U402B buffers the 2.5 V midrail. U501A/B give the line and amp feed their own output drivers. The headphone driver is fed from the separate stereo buffers.

All op-amp audio outputs before their coupling capacitors have approximately 2.5 V DC bias. These nets are internal only. Film capacitors C401/C402/C502/C601/C602 use the current WIMA MKS2 2.2 µF / 63 V size: approximately 7.2 × 7.2 mm body, 5 mm pitch. Their voltage rating is for component availability and low distortion, not a requirement for a 63 V system supply.

Calculated nominal response, excluding the codec and real-component tolerances:

| Path | Approximate behavior |
|---|---|
| AUX input coupling | 0.72 Hz pole from 2.2 µF / 100 kΩ |
| Mono averaging | Equal L/R weights; 0.1% summing resistors |
| Line out into 10 kΩ | About 0.990 of mono input at midband; 100 Ω source resistor |
| Line output coupling | About 0.67 Hz with the 10 kΩ pulldown and a 10 kΩ external load |
| Amp feed into 100 kΩ | About 0.990 midband gain; 100 kΩ internal pulldown; approximately 1.4 Hz output pole |
| Headphones into 32 Ω | Approximately 0.468 of AUX amplitude with -6 dB IC gain and 2.2 Ω series resistor |
| Headphone input coupling | Approximately 2.74 Hz with the typical 26.4 kΩ driver input resistance |

A 1 Vrms AUX signal therefore gives approximately 0.47 Vrms / 6.8 mW per 32 Ω headphone channel. This is a design estimate, not a hearing-safe volume guarantee or a measured distortion rating. Begin commissioning at low volume. The fixed -6 dB setting permits the nominal 1 Vrms line level without requiring the headphone driver to reproduce 1 Vrms into 32 Ω. This revision has shared codec volume control; it does not include an independent analog headphone volume knob.

## Raspberry Pi and Codec Zero integration

| Physical Pi pin | GPIO / purpose | Expansion net |
|---|---|---|
| 1 | 3.3 V | +3V3_PI |
| 2, 4 | 5 V | +5V_PI |
| 3 | GPIO2 / SDA1 | PI_SDA |
| 5 | GPIO3 / SCL1 | PI_SCL |
| 10 | GPIO15 / UART RX | MIDI_RX |
| 11 | GPIO17 | HP_ENABLE |
| 15 | GPIO22 | LINE_ENABLE |
| 6, 9, 14, 20, 25, 30, 34, 39 | Ground | GND |

GPIO18–21 remain reserved for codec I²S, GPIO23/24 for Codec Zero LEDs, GPIO27 for its button, and GPIO0/1 for HAT identification. The no-connect marks on other J101 contacts mean **this expansion adds no connection**; they do not instruct you to cut those contacts out of a stacking header. Pi model, UART overlay and Bluetooth assignment must be checked against the final host. Disable the serial console on the MIDI UART and use a stable 31,250 baud, 8-N-1 receive port.

J102 is explicitly **our** L / GND / R harness definition, not a claim about the physical order of Codec Zero's AUX pads. Verify the labelled pads on the actual board revision before making the harness. Never connect this circuit to the mono speaker terminals, which are not the AUX signal interface.

The expansion expects 4.75–5.25 V at J101 and reserves 150 mA beyond the Pi and Codec Zero budgets. Actual typical draw should be much lower. The supply filtering does not create an independent regulated rail or provide reverse polarity protection against a miswired external supply. Use the Pi's properly regulated supply arrangement and avoid a second supply back-feeding the header.

## Expression acquisition

JP301 is a 2×3 header. Normal shunts are **1–3 and 2–4**. Reversed shunts are **3–5 and 4–6**. Fit both in the same position. The center contacts 3 and 4 are wiper and excitation respectively.

U301 is ADS1115 at address 0x48. Read AIN0 and AIN1 single-ended with a ±4.096 V full-scale setting, initially at 128 samples/second. AIN0 is the filtered wiper; AIN1 measures the excitation after its 1 kΩ current-limiting resistor. Use their ratio, then calibrated heel/toe endpoints. Apply smoothing and a small deadband. Reject near-zero excitation readings and require a short stable interval after plug insertion before using a new value. Do not assume 16-bit absolute pedal accuracy: pot tolerance, loading and clamp leakage dominate.

The 1 kΩ excitation resistor limits a normal short to approximately 3.3 mA. The 10 kΩ / 100 nF ADC network and a 100 kΩ pot give a position-dependent low-pass response; the slowest nominal pole is around 45 Hz. R303 returns an unplugged wiper toward zero and introduces a small nonlinearity that endpoint calibration alone does not completely remove.

Both TRS signal contacts have connector-side TVS devices. Both ADC inputs have a series resistor, capacitor and Schottky clamps. R307 provides a rail discharge/load path. This interface is for passive pedals; it is not specified for arbitrary powered CV inputs, continuous 9/12/24 V misconnection, or accepting a voltage while the Pi is unpowered. The series resistors also limit residual transient injection into the ADC's internal protection.

## MIDI and ESD

The receiver uses a 220 Ω input resistor, reverse LED diode, RF beads and H11L1M Schmitt optocoupler. The optocoupler is powered at 5 V, while its open-collector output is pulled up only to the Pi's 3.3 V rail. An illuminated LED gives UART logic low. DIN contacts 1, 2 and 3 are unconnected; the shell is an enclosure bond.

D202 is differential protection across DIN 4 and 5. D203/D204 provide a **24 V standoff, low-leakage common-mode path to the chassis**. Those devices deliberately limit the common-mode isolation voltage; this is functional MIDI ground-loop isolation, not a rated safety isolation barrier. The 100 pF capacitors assist RF diversion. Keep the connector-side copper separate from logic copper and place the protection at the harness entry.

Audio TVS devices are bidirectional because headphone/line waveforms swing below ground. They are followed toward the driver by the output impedance specified on the sheet. Component ESD ratings do not establish assembled-product immunity. Fast trace inductance, enclosure bonding and return paths determine residual pin stress.

## Mute sequence and PCB constraints

Configure GPIO17 and GPIO22 low before enabling audio. Their hardware pulldowns hold the headphone driver off and the line relay de-energized while the Pi pins are high impedance. After power is stable, initialize codec routing and stream digital silence. Allow at least **5 seconds** for the coupling/bias networks to settle during first commissioning, then enable headphones and energize the line relay while still sending silence. Ramp volume up. Before a controlled shutdown, ramp volume down, drive both controls low, and allow relay release before removing power.

The line relay disconnects the source and grounds the external jack in its resting state. This reduces startup transients; it does not prove pop-free behavior on sudden power loss or brownout. Qualify those cases on hardware. The external amp must implement its own startup/shutdown mute.

Use a continuous ground plane with functional placement. Keep the headphone charge-pump loops short, place bypass capacitors directly at each IC, and do not share narrow ADC/audio return traces with jack ESD, relay or pump current. HPVDD is internally generated and must **not** be tied to 5 V. HPVSS is negative; its ceramic reservoir is nonpolar. Route the headphone sleeve return to the local driver ground region with adequate width. Bond CHASSIS to the enclosure at connector entry and implement R101 as a short, wide bond. Do not make TVS current pass through a long signal-ground path.

Use the included test pads and the accessible component pads for production test. Default resistors: 1%, ≥0.1 W, ≤100 ppm/°C unless explicitly stated; R403/R404: 0.1%, ≤25 ppm/°C. Ceramic bypass capacitors: X7R, voltage ratings shown; allow DC-bias derating, especially bulk and charge-pump capacitors. Pump capacitors should retain at least 1 µF effective capacitance. C501 is polarized, positive toward LINE_BUF. Check every selected manufacturer's land pattern before layout release.

## Verification status and release gates

KiCad ERC and an exported-netlist comparison are supplied under `verification/`. The comparison checks every assigned physical pin against the intended net, rather than relying only on a zero-error ERC report. No hardware, analog SPICE, EMC chamber or acoustic test has been performed.

Before production release:

1. Verify final Codec Zero AUX pad mapping, DC level, output range and host GPIO availability; verify supply headroom and current.
2. Measure audio gain, 20 Hz–20 kHz response, noise, crosstalk and THD+N with simultaneous line/headphone/amp loads. Sweep headphone loads of 32, 80 and 250 Ω. Confirm clipping margin before setting the software volume ceiling.
3. Test every jack hot-plug and signal-contact short, with the other outputs operating. Check op-amp stability with long cables and the actual TVS capacitance. Test relay engagement, normal shutdown, brownout and sudden removal of power.
4. Test MIDI with both 3.3 V and 5 V transmitters, weak-current transmitters, long cables and dense traffic; inspect optocoupler rise/fall times and UART errors across the intended temperature range.
5. Test expression endpoints, both jumper settings, open/short/intermittent cables, insertion shorts, unpowered behavior and ADC recovery.
6. Qualify assembled-enclosure ESD to the chosen product standard (initial engineering target ±8 kV contact / ±15 kV air), including connector shells, exposed contacts, common-mode MIDI strikes and repeated operation afterwards. Verify no latchup, damage or persistent software state corruption.
7. Freeze connector mechanics, stack height, exact passive procurement choices, land patterns and the user's amp interface. Review the PCB placement/grounding against these notes before ordering a production batch.

See `SOURCES.md` for the primary component and interface references used.
