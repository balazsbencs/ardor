# Ardor Raspberry Pi Audio + Control I/O

This directory contains the reproducible tscircuit design and generated KiCad
schematic for the Ardor guitar processor companion board. The intended physical
implementation is a stack-through board between the Raspberry Pi 40-pin header
and Raspberry Pi Codec Zero, with short four-wire harnesses to Codec Zero P1 and
P2.

The design provides:

- 1 MΩ, AC-coupled, protected guitar input buffered into both Codec Zero AUX IN
  channels.
- Protected stereo 1 V RMS line output from Codec Zero AUX OUT.
- Mono `(L + R) / 2` guitar-amplifier output so stereo effects do not lose one
  channel.
- Normally-open relay muting of every audio output during boot, reset, and
  shutdown.
- Galvanically isolated 3.5 mm TRS MIDI input that accepts Type A or Type B
  polarity.
- Protected passive expression-pedal input, hardware tip/ring polarity switch,
  and ADS1115 conversion.
- Filtered 4.5 V analogue supply, precision 2.25 V mid-rail, local decoupling,
  connector ESD paths, and a deliberate chassis/0 V bonding network.

## Deliverables

- `generate-schematic.tsx`: editable tscircuit source and deterministic export
  script.
- `check-connectivity.ts`: automated pin/net and isolation-boundary checks.
- `control-io.circuit.json`: generated tscircuit Circuit JSON.
- `control-io.sheets.circuit.json`: the same settled connectivity with the Rev B
  documentation split into eight functional sheets.
- `control-io.kicad_pro`: KiCad project metadata.
- `control-io.kicad_sch`: generated hierarchical KiCad root schematic. Its
  `gpio_header`, `power_reference`, `guitar_input`,
  `codec_output_processing`, `relay_outputs`, `midi_input`,
  `expression_input`, and `output_mute` child sheets are generated alongside it.
- `tscircuit.pretty/`, `fp-lib-table`: generated, project-local KiCad footprints.
- `tscircuit.kicad_sym`, `sym-lib-table`: generated project-local symbol library.
- `control-io.svg`: stacked vector overview of every functional sheet.
- `control-io-01-*.svg` through `control-io-08-*.svg`: tightly cropped,
  readable feature schematics.
- `control-io.pdf`: eight-page A3 landscape schematic set.
- `REV-B-REVIEW.md`: electrical audit, corrected findings, calculations, and
  remaining fabrication gates.

Regenerate and verify with Node 24:

```sh
npm install --ignore-scripts
npm run generate
npm run check
```

## Host pin allocation

| Function | BCM GPIO | Physical pin | Electrical interface |
| --- | ---: | ---: | --- |
| I2C SDA to ADS1115 | GPIO2 | 3 | Shared I2C1, local 33 Ω series damping |
| I2C SCL to ADS1115 | GPIO3 | 5 | Shared I2C1, local 33 Ω series damping |
| Audio-output relay enable | GPIO10 | 19 | 1 kΩ gate resistor, 10 kΩ boot pull-down |
| MIDI receive | GPIO9 | 21 | UART4 RX after Schmitt optocoupler and 220 Ω series resistor |
| Expression data-ready | GPIO25 | 22 | ADS1115 open-drain output, 10 kΩ to 3.3 V and 220 Ω series |
| MIDI transmit | GPIO8 | 24 | Reserved, not connected |

GPIO2/GPIO3 intentionally share I2C1 with Codec Zero. The companion board does
not duplicate the host pull-ups. GPIO18/19/20/21 and the Codec Zero LED/button
pins remain untouched. The stack-through header mechanically passes all 40 pins;
the schematic marks unused connections as no-connect only for this circuit.

## Codec Zero audio connection

The Codec Zero uses the Dialog Semiconductor DA7212. Its P1 AUX IN and P2 AUX
OUT headers are both four pins. From square pin 1, the order is:

| Pin | Signal |
| ---: | --- |
| 1 | Left |
| 2 | Ground |
| 3 | Right |
| 4 | Ground |

Both AUX IN and AUX OUT are specified for up to 1 V RMS. The guitar buffer is
unity gain and feeds both input channels; gain should be set in the DA7212 mixer,
where it can be calibrated without sacrificing analogue headroom. The stereo
line output remains unity gain. The mono amp output is an inverting average, so a
full-scale correlated stereo signal remains full-scale instead of summing to
twice the voltage.

## Audio design

The guitar jack sees a 1 MΩ nominal load. A low-capacitance bidirectional
PESD5V0U1BA-Q routes connector ESD to chassis, 1 kΩ limits surge/RF current,
220 nF film coupling
blocks external DC, and 10 kΩ plus a 100 pF C0G capacitor limits RF before a
BAT54S rail clamp. U1 is one OPA4377AIPWR quad RRIO amplifier. Channel A is the
input follower, channels B/C buffer Codec Zero left/right output, and channel D
forms the mono sum.

Audio output connectors are AC-coupled through 10 µF bipolar capacitors, have
100 Ω short-circuit/isolation resistors, 100 kΩ discharge resistors on both
sides of each relay, and low-capacitance ESD protection to chassis. K1 and K2
are 5 V normally-open signal
relays. GPIO10 must remain low until the codec, virtual ground, and output coupling
capacitors have settled; software then drives it high to unmute. A crash or power
loss returns the outputs to open circuit.

Use isolated-bushing audio jacks. Connect the metal enclosure to J9 with the
shortest practical low-inductance bond. R30 and C27 reference circuit 0 V to the
chassis for static/EMC control. R31 is deliberately DNI; fit the 0 Ω bond only if
system-level hum and EMC testing shows that a direct bond is preferable.

## MIDI input

The MIDI cable side is isolated from Raspberry Pi ground. A low-capacitance
differential TVS protects tip/ring, a four-Schottky bridge accepts either TRS
polarity, and the 220 Ω resistor limits optocoupler LED current. H11L1M provides
a fast Schmitt output at 3.3 V, pulled up by 4.7 kΩ. Its output is valid for the
31.25 kbaud MIDI physical layer and drives GPIO9/UART4 RX.

The sleeve/shield connects only to chassis through 1 MΩ in parallel with 1 nF
at 1 kV. It must never be joined to AGND or DGND. This is a functional isolation
boundary, not mains-safety isolation.

## Expression input

The expression jack is only for a passive potentiometer pedal with sleeve as the
ground end. SW1 is a mechanically linked DPDT switch that swaps tip/ring between
3.3 V excitation and the ADC wiper, covering the two common pedal polarities.
R23 limits a shorted cable to approximately 3 mA. R24/C13 form a 4.7 kΩ/100 nF low-pass
filter, D10 clamps the ADC node, and low-capacitance TVS parts route connector
ESD to chassis. D17 blocks reverse current through the pedal excitation path,
but the input is not a general-purpose control-voltage input: an externally
powered pedal can still drive the ADC protection clamp. ADS1115 address pin is grounded (`0x48`);
unused inputs return to ground through 1 kΩ.

## Power and layout requirements

The analogue rail is derived from Raspberry Pi 5 V through a 500 mA resettable
fuse, ferrite bead, bulk/ceramic filtering, and TPS7A2045 low-noise 4.5 V LDO.
TLE2426IDR creates the 2.25 V signal reference, with its noise-reduction pin
bypassed locally. Relay coils use their own 150 mA resettable-fuse branch so
their current pulses do not flow through the analogue regulator. The series
diode/Zener flyback clamp releases the relays faster than a plain diode, reducing
shutdown and fault pops.

For PCB layout:

- Place every TVS at its connector, with a short, wide return to the chassis
  region before traces enter the quiet circuit area.
- Keep the MIDI isolated copper island and its creepage gap free of pours and
  routing. Use a DIP-6 optocoupler footprint if generous separation is desired.
- Treat AGND and DGND as one electrical 0 V net and use a continuous reference
  plane. Partition noisy relay/digital return currents by placement and routing,
  not by cutting the ground plane. Keep CHASSIS separate except for the defined
  R30/C27/D18 coupling network (and optional DNI R31 bond).
- Put U1, its 100 nF bypass, and the 2.25 V reference together. Keep the 1 MΩ
  guitar node short, guarded by VREF where practical, and away from I2S/display
  clocks and relay-coil routing.
- Route Codec Zero analogue harnesses as left/ground and right/ground twisted
  pairs. Keep them short and away from DSI and switching-power wiring.
- Check jack, relay, switch, header, and enclosure footprints against actual
  ordered mechanical parts before layout release.

The generated project-local library includes an eight-pad ATQ209/TQ2 footprint
(the unpopulated package positions 5 and 6 are not fabricated), the C&K
JS202011CQN 2.5 mm × 3.3 mm hole pattern with physical common terminals 3 and
6, and the ADS1115IDGSR VSSOP-10 package. These package choices match the
manufacturer drawings; still compare the fabrication plot to the exact orderable
parts before releasing a board.

## Engineering status

This revision has passed the connectivity and documentation checks recorded in
`REV-B-REVIEW.md`, but it is not a released production PCB. Components have
concrete tscircuit footprints and the generated model has no missing-footprint
errors; connector, relay, switch, and enclosure mechanicals still require
validation against purchased parts. The generated project does not contain a
placed and routed fabrication PCB. Before manufacturing, complete that PCB,
map and verify every generated footprint field in KiCad, run KiCad ERC/DRC,
perform analogue simulation or bench gain/headroom
measurements, and validate IEC 61000-4-2 contact/air ESD, radiated immunity,
conducted noise, hot-plug pops, relay timing, and ground-loop behavior on the
assembled Raspberry Pi + display + Codec Zero system.
