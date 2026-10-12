# Ardor Mono line output with relay mute — M1

An external mono line-level source feeds a local buffer, relay-muted panel output and independent internal amplifier feed. It accepts a source directly; the mixer module is optional.

Power from an external regulated **5 V / 50 mA** supply budget. These figures reserve operating margin; they are not measured consumption. No onboard input regulator or reverse-power protection is provided. Use only the named rails and connect host GND. Budget each fitted module separately.

[Single-page schematic](review/schematic.pdf) · [Component placement](review/pcb-assembly.svg) · [Front routing](review/pcb-front.svg) · [Back routing and connector legends](review/pcb-back.svg)

[Short assembly guide](ASSEMBLY.md) · [Beginner pin card](review/wiring-guide.pdf)

## Connector pins

All connectors/jumpers are 2.54 mm headers. **Square pad = pin 1.** Pin numbers below are native schematic/PCB numbers, not left-to-right screen positions. Back-side text is read from the back of the physical board. The same reference names appear on multiple modules; use this board’s table. **J101 pin assignments differ between module types; harnesses are not interchangeable.**

| Connector | Pin | Direction / function | Connect to |
|---|---|---|---|
| J101 | 1 | **POWER INPUT** — Feed regulated 5V into the module. This pin does not supply power. | Positive output of the matching regulated supply. |
| J101 | 2 | **GROUND** — Common 0 V return; not a signal or positive supply. | Host or cable ground / 0 V. |
| J101 | 3 | **CONTROL INPUT** — 3.3 V HIGH = on; 0 V LOW or disconnected = off. | Host control output. Keep LOW until audio and supply are stable. |
| J102 | 1 | **INPUT** — AUDIO audio enters the module. | Mono DAC/codec audio output, <=1 Vrms. |
| J102 | 2 | **GROUND** — Common 0 V return; not a signal or positive supply. | Host or cable ground / 0 V. |
| J503 | 1 | **OUTPUT** — LINE audio leaves the module. | TS socket tip; line load >=10k. Grounded when relay is off. |
| J503 | 2 | **GROUND** — Common 0 V return; not a signal or positive supply. | TS socket sleeve; enclosure bond. |
| J502 | 1 | **OUTPUT** — AUDIO audio leaves the module. | Signal input of a separate amplifier, >=100k. NOT a speaker. |
| J502 | 2 | **GROUND** — Common 0 V return; not a signal or positive supply. | Host or cable ground / 0 V. |

**IN** enters this board. **OUT** leaves this board. **I/O** uses both directions. **GND** is the common 0 V return. **3V3** means 3.3 V.

H1/H2 are mounting holes. Small via holes and other component pads are not wire connectors.

## Wiring and commissioning

Connect an external mono line-level source, **≤1 Vrms**, to J102 pin 1 with signal ground on pin 2. A stereo source needs upstream mixing if both channels are required; this module does not require the Ardor mixer specifically.

J503 goes to a TS panel socket: pin 1 tip, pin 2 sleeve/GND. The sleeve/bushing bonds the aluminium enclosure through the local CHASSIS/GND link. Use a line load of **10 kΩ or greater**. J502 is a separate AC-coupled internal amplifier feed (pin 1 audio, pin 2 ground), intended for **100 kΩ or greater** input impedance; it remains active while the panel output is muted. Its 1 kΩ output resistor attenuates lower-impedance amplifier inputs.

J101 pin 3 **LINE_ENABLE** is a host logic input. LOW or disconnected de-energizes the relay and grounds the panel tip; HIGH at 3.3 V connects the line signal. Keep enable low at boot, allow the local bias and coupling capacitor to settle, then enable and ramp the source volume. This is **relay startup mute, not true bypass**: no dry-input connection exists. It is not a speaker power amplifier.

Fit K501 **Omron G5V-1 DC5** by hand, along with J101/J102/J503/J502. Use the footprint’s pin numbers and assembly drawing, not the appearance of a different relay case. Coil current is approximately 30 mA; the 50 mA supply budget includes the analog circuit. C501 is polarized: its **positive terminal is toward the op-amp**, negative toward the relay/output. Allow at least 6.1 mm component height for the specified Panasonic capacitor, plus enclosure clearance.

Bring-up: with the relay off, verify continuity from J503 tip to GND. Confirm approximately 2.5 V at the local op-amp bias, near-zero settled output DC after C501, and clean relay switching. Measure the startup/shutdown pop with the actual host and load before relying on software delays. Check J502 independently with the panel output muted.

## Order this board

Upload [the fabrication ZIP](assembly/Ardor_Line_Out-JLCPCB-Gerbers.zip), [SMT BOM](assembly/jlcpcb-bom.csv) and [CPL](assembly/jlcpcb-cpl.csv) as **one separate design**. Select two assembled boards, two copper layers, 1.6 mm FR-4, 1 oz finished copper, and top-side SMT assembly only. The ZIP includes both silk/mask/copper layers and separate PTH/NPTH drill files. Use the supplied absolute origin throughout; do not apply an auxiliary-origin shift to just the CPL.

The dated parts/loss model is **$8.52** for two boards, plus **$12.28** for 4 Economic Extended feeder types. This excludes PCB/setup/stencil/joints/shipping/tax/manual parts and is not a checkout quote. Public stock was checked **2 October 2026**; no parts are reserved. Recheck the exact C-codes and quantities when ordering.

[Manual-fit BOM](assembly/manual-bom.csv) lists headers, relay. Panel sockets, wires, host, regulated supply, M2 hardware and enclosure are external items, chosen to suit the build; they are not in the SMT order. This PCB has two 2.2 mm NPTH mounting holes. Keep conductive mounting hardware clear of copper.

Inspect **every IC, diode and polarized capacitor** against its pin 1/polarity in JLCPCB’s interactive placement preview. CPL uses native KiCad CCW angles; the supplier may assign a different zero orientation for its part model. That preview has not been checked or approved here. Correct supplier-side rotations before paying.

[Verification summary](verification/validation.json) records clean ERC/DRC/parity, physical pad/net checks, uniform widths and zero inter-module dependencies. These are design checks; the M1 boards have not been built or bench-tested. Complete the commissioning checks above before treating them as validated hardware.
