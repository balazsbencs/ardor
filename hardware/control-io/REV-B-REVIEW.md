# Rev B electrical and documentation review

Review date: 2026-08-28. Scope: the Codec Zero companion board only. Rev C is
explicitly outside this review.

## Result

The Rev B netlist is internally consistent after the corrections below. The
generated design has 102 physical components, no unconnected active pins, no
single-pin nets, no passive parts shorted across one net, and an intact MIDI
isolation boundary. The documentation export is an eight-sheet hierarchical
KiCad design with matching focused SVG pages and an eight-page PDF.

This is a schematic-review milestone, not manufacturing release. There is no
placed and routed PCB in this directory, and therefore no board-level clearance,
return-current, mechanical, thermal, or KiCad DRC evidence yet.

## Corrected findings

| Severity | Finding | Resolution |
| --- | --- | --- |
| Critical | U3, TLE2426IDR, incorrectly assigned `NOISE REDUCTION` to physical pin 5 and marked physical pin 8 NC. A board made from that mapping would not implement the datasheet circuit. | Corrected to pin 1 OUT, pin 2 COMMON, pin 3 IN, pin 8 NOISE REDUCTION; pins 4–7 are NC. Added a regression check tying U3 pin 8 to C25 pin 1. |
| High, documentation | The flat poster-style export made valid connections appear open. R15 was the clearest example. | Replaced the poster with eight functional sheets. C9 pin 2 now has a visible wire to R15 pin 1; R15 pin 2 shares `AMP_RELAY_IN` with both K2 commons and R38. Both sides are regression-tested. |
| Medium | D7 was named generic axial `1N4148` while its footprint was SOD-323. | Changed the part definition to surface-mount `1N4148WS`, matching SOD-323. |
| Medium | R19 was called out as 220 Ω, 0.25 W but used an 0805 footprint without an exact high-power resistor selection. | Moved the footprint to 1206, leaving comfortable power-package margin for assembly sourcing. |
| Medium, documentation | AGND/DGND wording implied a split plane and a special single-point net join, although both names resolve to the same electrical 0 V net. | The layout requirement now calls for one continuous reference plane, with noisy-current partitioning by placement/routing. CHASSIS remains a distinct net. |
| Medium, documentation | The expression description overclaimed that D17 prevents every possible backfeed path. | Clarified that D17 blocks the excitation path only. J7 is passive-expression-only and is not an external CV input. |
| Low | K1/K2 contained fictitious schematic pins 5 and 6 even though the selected ATQ209/TQ2 package has eight physical leads. | Removed the fictitious pins. The symbol and eight-pad footprint now describe the same package positions. |

## Functional review

### Power, reference, GPIO and chassis

- Pi 5 V feeds two protected branches: F1 (500 mA) for the analogue supply and
  F2 (150 mA) for both relay coils.
- FB1 plus C20/C21 precede the TPS7A2045 4.5 V low-noise LDO. C22/C23/C24 provide
  output bulk and high-frequency bypassing.
- The corrected TLE2426 makes a 2.25 V reference from 4.5 V. C25 is connected
  from physical pin 8, NOISE REDUCTION, to 0 V; C26 bypasses the reference.
- TQ2-5V nominal coil resistance is about 178 Ω, so two energized coils draw
  roughly 56 mA total. That is comfortably below F2's 150 mA hold rating.
- R30 (1 MΩ), C27 (4.7 nF, 1 kV), and D18 (SMBJ5.0CA) are the defined
  CHASSIS-to-0 V coupling network. R31 is DNI unless system EMC/hum testing
  justifies a direct bond.
- Only GPIO2, GPIO3, GPIO9, GPIO10, and GPIO25 are electrically tapped by this
  board. GPIO8 is reserved but open. The remaining control GPIO wiring shown in
  `docs/assets/gpio-controls.svg` goes directly to the stack-through header.

### Guitar input and Codec Zero input

- The jack presents a nominal 1 MΩ load. The 220 nF/1 MΩ input high-pass corner
  is approximately 0.72 Hz.
- D1 routes connector ESD to CHASSIS before R1; R1 limits surge current. C1
  blocks external DC. R3/C2 gives an RF corner near 159 kHz before the BAT54S
  rail clamp and U1A follower.
- U1A is unity gain. The two 2.2 µF/100 kΩ Codec Zero feed networks each have an
  approximately 0.72 Hz high-pass corner.
- This is appropriate for passive guitar and normal active pickups. A source
  substantially above Codec Zero's 1 V RMS AUX rating can clip; that is a
  headroom limitation, not an ESD-protection function.

### Codec output, line output and amp output

- U1B/U1C are unity buffers for Codec Zero left/right. U1D uses 20 kΩ from each
  channel and 10 kΩ feedback, giving `-(L + R) / 2` around the 2.25 V reference.
- A 1 V RMS signal swings about ±1.414 V around 2.25 V, staying between roughly
  0.84 V and 3.66 V on the 4.5 V OPA4377 supply.
- C7/C8/C9 are 10 µF bipolar output couplers. Their corner is about 0.16 Hz with
  100 kΩ and about 1.6 Hz with a typical 10 kΩ destination.
- R13/R14/R15 provide 100 Ω output isolation/current limiting. R36–R38 discharge
  the pre-relay nodes; R16–R18 discharge the connector sides.
- K1 switches stereo line left/right independently. K2 parallels its two poles
  for the mono amp output. The output is intended for an amplifier/instrument
  input, never a loudspeaker load.
- D3/D4/D5 are low-capacitance bidirectional ESD parts returned to CHASSIS.

### Fail-safe mute

- R35 holds Q1's gate low during boot/reset. GPIO10 reaches the gate through
  R34, so software must explicitly drive HIGH to energize K1/K2.
- Power loss or a crashed/reset Pi opens the normally-open contacts.
- D11 plus the reverse-biased 5.1 V Zener D12 clamp coil turn-off above the 5 V
  rail while remaining within the AO3400A's 30 V drain rating.

### MIDI input

- J6 tip/ring/shield are isolated from AGND/DGND. Automated checks verify all
  three cable-side nets differ from Raspberry Pi ground.
- D6 is the differential surge suppressor. D13–D16 form a Schottky bridge so
  either TRS MIDI polarity drives the H11L1M LED correctly. R19 is the 220 Ω
  receiver resistor; D7 protects the optocoupler LED from reverse voltage.
- H11L1M provides a Schmitt logic output with R20 pull-up. R21 limits GPIO fault
  current. C10 is local logic bypassing.
- R22/C11 reference cable shield to CHASSIS for EMC only. This is functional
  MIDI isolation, not a mains-safety isolation barrier.

### Expression input and ADC

- J7 accepts a passive three-terminal potentiometer pedal only. SW1 swaps tip
  and ring to support the two common pedal polarities.
- D8/D9 route connector ESD to CHASSIS. D17 and R23 feed the pedal; a short is
  limited to approximately 3 mA.
- R24/C13 form a 4.7 kΩ/100 nF low-pass with a corner near 339 Hz. D10 clamps the
  filtered ADC node to 0 V/3.3 V.
- ADS1115 is at address `0x48`. SDA/SCL have 33 Ω series damping and intentionally
  no duplicate pull-ups. ALERT/RDY has its required 10 kΩ pull-up and 220 Ω GPIO
  series resistor. Unused ADC inputs return to ground through 1 kΩ.
- Do not connect an externally powered expression pedal or bipolar CV source.

## Pin/package verification performed

The schematic definitions were compared with manufacturer pinouts for
OPA4377AIPWR (TSSOP-14), TPS7A2045PDBVR (SOT-23-5), TLE2426IDR (SOIC-8),
H11L1M (DIP-6), ADS1115IDGSR (VSSOP-10), AO3400A (SOT-23), BAT54S (SOT-23),
ATQ209/TQ2-5V, and JS202011CQN. The generated relay footprint contains the
eight actual package leads: 1, 2, 3, 4, 7, 8, 9, and 10.

Primary references:

- [TI TLE2426 datasheet](https://www.ti.com/lit/ds/symlink/tle2426.pdf)
- [TI OPA4377 datasheet](https://www.ti.com/lit/ds/symlink/opa4377.pdf)
- [TI ADS1115 datasheet](https://www.ti.com/lit/ds/symlink/ads1115.pdf)
- [onsemi H11L1M datasheet](https://www.onsemi.com/pdf/datasheet/h11l3m-d.pdf)
- [Panasonic TQ2 relay product page](https://na.industrial.panasonic.com/products/relays-contactors/mechanical-signal-relays/lineup/signal-relays/series/119572/model/119940)
- [Raspberry Pi Codec Zero documentation](https://www.raspberrypi.com/documentation/accessories/audio.html)

## Automated review gates

`npm run check` now rejects:

- any tscircuit error element;
- any active pin without connectivity;
- any one-pin net;
- any resistor, capacitor, diode, or fuse shorted across one net;
- changed critical GPIO, relay-contact, MIDI-isolation, expression, U3, or R15
  connectivity;
- a missing/misordered functional sheet;
- a documentation element without sheet ownership;
- non-orthogonal documentation wires; or
- loss of the visible C9-to-R15 connection.

## Remaining release gates

Before ordering assembled boards:

1. Create the actual PCB and mechanical outline. This repository currently has
   schematic deliverables, not Gerbers or a routed `.kicad_pcb`.
2. Replace generic passives/connectors with approved orderable manufacturer and
   JLCPCB/LCSC parts; verify stock, lifecycle, voltage rating, dielectric, and
   power rating.
3. Compare every footprint against the exact purchased relay, switch, headers,
   jacks, and enclosure. Print a 1:1 mechanical plot.
4. Run KiCad ERC and PCB DRC after import/layout. Inspect every hierarchical
   label and net in KiCad, then compare the exported PCB netlist with
   `control-io.circuit.json`.
5. Use a continuous 0 V plane, a connector-edge CHASSIS region, short TVS
   returns, and keep the isolated MIDI copper free of AGND/DGND pours.
6. Bench-test DC rails, 2.25 V settling, gain/headroom, relay timing, hot-plug
   behavior, shorted outputs, expression extremes, and both MIDI TRS polarities.
7. Perform system ESD/immunity and ground-loop testing in the intended metal
   enclosure with the Pi, display, Codec Zero, and final cable set.

