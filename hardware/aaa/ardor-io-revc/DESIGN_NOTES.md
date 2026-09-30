# Rev C integration and commissioning

Rev C uses the Pi's regulated 4.75–5.25 V supply and Codec Zero AUX L/R signals. It adds no external power converter and no instrument input. J102 is our L/GND/R harness definition: verify the actual Codec Zero pad labels and output level. Never connect the BTL speaker terminals to J102. Reserve 100 mA beyond the Pi/codec budgets, including the nominal 30 mA relay coil.

## Audio

C401/C402 couple AUX L/R into 100 kΩ bias resistors at 2.5 V. U401 buffers both channels, two 10 kΩ resistors average them, U402A buffers the average, and U402B buffers the midrail divider. All four amplifier sections use TI TLV9002 at 5 V. Output coupling capacitors have separate series resistors: line 100 Ω, internal amp feed 1 kΩ. The mono buffer is shared, so a short or excessive load on one output can affect the other. The amp feed is internal and its external amplifier owns its mute and protection.

Nominal AUX ceiling remains 1 Vrms/channel. Into a line load of at least 10 kΩ and an amp load of at least 100 kΩ simultaneously, nominal peak current from U402A is about 0.305 mA. A 20 kHz, 1 Vrms sine requires 0.1777 V/µs slew; TLV9002's typical slew is 2 V/µs. Its 1 MHz unity bandwidth and approximately 30 nV/√Hz input noise make it a cost compromise relative to OPA2320, not a measured audio-performance equivalent. Gain, noise, capacitive-load stability, THD+N and clipping must be qualified.

Nominal input high-pass pole is 0.159 Hz (10 µF / 100 kΩ). Line coupling is about 0.67 Hz into 10 kΩ with its parallel 10 kΩ pulldown; amp coupling about 0.312 Hz into 100 kΩ with its parallel 100 kΩ pulldown. Actual MLCC capacitance is bias-dependent. X5R parts retain their nominal capacitance over the specified −55 to +85 °C temperature range within ±15%; DC bias, tolerance and aging add further changes. Bench-test bass distortion and startup settling.

C501 remains Panasonic EEEFK1C470P, 47 µF / 16 V, on the existing manufacturer-specific lands. Its positive terminal is MONO_BUF; allow at least 6.1 mm component height plus mechanical clearance. The relay grounds the external line jack and disconnects the source while de-energized. This is startup mute, not an audio bypass relay. AO3400A retains its guaranteed resistance specification at 2.5 V gate drive and the flyback diode.

## GPIOs and panel wiring

| Connector | Pin | Function |
| --- | --- | --- |
| J101 | 1 | Pi 3.3 V |
| J101 | 2, 4 | Pi 5 V |
| J101 | 3, 5 | I²C SDA, SCL |
| J101 | 10 | GPIO15, MIDI UART RX |
| J101 | 15 | GPIO22, line relay enable |
| J101 | 11 | GPIO17 unused by Rev C |
| J102 | 1, 2, 3 | Codec AUX L, GND, R |
| J202 | 1, 2 | DIN MIDI contacts 4, 5 |
| J302 | 1, 2, 3 | Expression tip, ring, sleeve |
| J503 | 1, 2 | Mono line tip, sleeve |
| J502 | 1, 2 | Internal amp signal, GND |

The Pi I²S, Codec Zero LED/button and HAT EEPROM pins remain unconnected on this expansion. A no-connect mark means no added load; retain all contacts on the stacking header. Disable the serial console and configure a stable 31,250 baud, 8-N-1 UART. Verify Pi model, UART overlays, GPIO ownership and mating height.

MIDI uses H11L1M at 5 V with its open collector pulled up only to 3.3 V. The current loop, 220 Ω resistor, reverse diode and differential TVS remain; common-mode 24 V TVS and 100 pF RF capacitors also remain. The 120 Ω beads provide less RF impedance than the original 600 Ω beads. DIN contacts 1/2/3 and its shell remain unconnected. Insulate the MIDI shell from the aluminium enclosure. This is functional ground-loop isolation, not a rated safety barrier.

Expression retains ADS1115 at 0x48. Read AIN0 and AIN1 single-ended at ±4.096 V, initially 128 SPS, and use the wiper/excitation ratio, heel/toe calibration, smoothing and deadband. Passive potentiometer pedals only; no powered CV input. Normal JP301 shunts: 1–3 and 2–4. Reversed: 3–5 and 4–6. Move both shunts together. Series resistance, 100 nF filters, Schottky clamps, TVS and rail bleed remain. Loading creates travel nonlinearity, as in the full revision.

Metal-bushing audio jack sleeves bond the aluminium enclosure. Retain wired sleeve returns; the enclosure does not replace those wires. CHASSIS connects through populated R101 (0 Ω) to board GND, then through the sleeve wiring and jack bushings to the enclosure. Verify continuity on the assembled enclosure, keeping the MIDI shell insulated.

## Ground planes and layout size

Both copper layers carry filled GND zones. The ground cleanup removes 192 of the previous 195 GND segments, reducing explicit ground-track length from 328.78 to 4.65 mm. All 20 ground vias remain to connect the two layers. Two short front-layer connections remain at J101.20, and one connects C302.2 to its adjacent via. Trial removal of all ground tracks left no unconnected nets, but did produce starved thermal-relief errors at J101.20 and C302.2; retaining these local connections gives zero DRC findings without relaxing the rules. [KiCad's zone documentation](https://docs.kicad.org/9.0/en/pcbnew/pcbnew.html#working_with_zones) describes filled-zone connections and thermal reliefs.

The MIDI isolation keepouts, CHASSIS traces and R101 bond remain. Front and back filled-copper previews are in `routing/front.svg` and `routing/back.svg`; `routing/ground-routing.svg` hides pours to show the track removal. Plane connectivity and local thermals have been checked, but this is not a measured noise or EMC qualification.

The outline remains 68 × 46 mm. J101 spans the top, J302/JP301 and the expression components occupy the lower left, and H3/H4 retain the lower mounting locations. Removing GND tracks does not move those features. The unused lower-right area could support a future placement redesign, but a smaller rectangular board requires relocating the lower circuitry and changing the mounting pattern. The SMT part count and assembly cost model are unchanged by this routing cleanup.

Both `+3V3_PI` and `+3V3_ADC` use uniform 0.20 mm tracks. The previous 0.40 mm runs were a general power-routing preference; these rails carry only a few milliamps, so using the narrower width throughout simplifies routing with negligible resistive loss. At nominal 3.3 V, R307 draws 1.5 mA, R301 limits an expression short to about 3.3 mA, the [ADC draws at most 0.3 mA](https://www.ti.com/lit/ds/symlink/ads1115.pdf), and the MIDI pull-up adds up to about 3.3 mA on the unfiltered rail. The width audit uses a conservative 10 mA screening current and nominal 35 µm copper. It sums every branch's trace resistance for an upper bound on trace voltage drop, excluding ferrite/via resistance. Narrowing 37 segments makes all 53 segments uniform; route centerlines, vias and other track widths retain their reviewed geometry.

## First power-up

1. Inspect polarity, shunts and soldering. Check for shorts and verify header/panel wiring before attaching the Pi.
2. Hold GPIO22 low; verify 5 V, 3.3 V and the approximately 2.5 V midrail. Check optocoupler pull-up remains at 3.3 V.
3. Initialize codec routing with digital silence and low volume. Wait at least five seconds for first commissioning, energize the relay while silent, then ramp volume.
4. Before shutdown, ramp down, drive GPIO22 low and allow relay release. Test brownout and sudden power loss separately.
5. Measure frequency response, noise, clipping and distortion with both outputs loaded. Test cable capacitance, jack shorts/hot-plug and startup pops. Qualify MIDI traffic, expression endpoints/insertion shorts, and enclosure ESD before relying on the hardware.

Rebuild with KiCad 9.0.2, Python `pcbnew`, `wx` and `sexpdata`: run `python3 design/regenerate.py` under a GUI display (`xvfb-run -a` on headless Linux). It regenerates the five sheets, exports the netlist, derives the layout, refills zones in a separate process after the layout builder exits, then checks ERC, DRC, pad/net invariants, ground routing and assembly tables. The PCB builder uses a frozen Rev 2 layout, the current netlist, a reviewed list of pruned branch UUIDs, `design/ground-plane-routing.json` and `design/uniform-3v3-routing.json`. The ground audit verifies the three local links, all 20 ground vias and unchanged geometry/net assignment of all other routing apart from the separately audited 3.3 V width edits. The geometry audit verifies exactly those 37 width changes and uniform width across both rails. The original revision's files are not modified by this workflow. Regenerate the one-page drawing, assembly drawing and Gerbers after CAD changes; `design/draw_ground_routing.py` produces the ground comparison. Inventory is a dated snapshot and must be refreshed independently.
