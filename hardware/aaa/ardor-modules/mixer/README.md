# Ardor Stereo buffer and mono mixer — M1

An external line-level stereo source feeds two unity buffers and an equal-weight mono average. All three outputs are AC coupled. Bias generation is local.

Power from an external regulated **5 V / 10 mA** supply budget. These figures reserve operating margin; they are not measured consumption. No onboard input regulator or reverse-power protection is provided. Use only the named rails and connect host GND. Budget each fitted module separately.

[Single-page schematic](review/schematic.pdf) · [Component placement](review/pcb-assembly.svg) · [Front routing](review/pcb-front.svg) · [Back routing and connector legends](review/pcb-back.svg)

## Connector pins

All connectors/jumpers are 2.54 mm headers. **Square pad = pin 1.** Pin numbers below are native schematic/PCB numbers, not left-to-right screen positions. Back-side text is read from the back of the physical board. The same reference names appear on multiple modules; use this board’s table. **J101 pin assignments differ between module types; harnesses are not interchangeable.**

| Connector | Pin | Signal |
|---|---|---|
| J102 | 1 | AUDIO_L |
| J102 | 2 | GND |
| J102 | 3 | AUDIO_R |
| J101 | 1 | +5V |
| J101 | 2 | GND |
| J103 | 1 | OUT_L |
| J103 | 2 | GND |
| J103 | 3 | OUT_R |
| J103 | 4 | OUT_MONO |

## Wiring and commissioning

Connect an external **line-level DAC or codec** to J102: pin 1 left, pin 2 ground, pin 3 right. Keep each input at or below **1 Vrms**. This is not a high-impedance guitar input, ADC, headphone driver or speaker amplifier.

J103 exposes left, ground, right and mono in that order. Left/right nominal gain is unity. Mono is **(L + R) / 2**; feeding just one channel reduces its mono level by 6 dB. Feed the same mono source into both input pins if unity mono level is desired. Each output has its own 10 µF coupling capacitor, 100 Ω series resistor and 100 kΩ bleed, so no 2.5 V internal bias is exported.

Use loads of **10 kΩ or greater**. These are internal harness ports; external panel connectors need appropriate protection at the enclosure entry. No enable/control input is required. Fit J101/J102/J103 by hand. The SOIC dual amplifiers and all filtering/bias parts are SMT assembled.

Bring-up: check filtered 5 V and the local approximately 2.5 V bias. Apply a 1 kHz sine to each channel separately and together, confirm unity stereo gain and the expected mono average, and check outputs for near-zero settled DC. Check clipping/noise at the intended load and allow coupling capacitors to settle before enabling downstream amplifiers.

## Order this board

Upload [the fabrication ZIP](assembly/Ardor_Mixer-JLCPCB-Gerbers.zip), [SMT BOM](assembly/jlcpcb-bom.csv) and [CPL](assembly/jlcpcb-cpl.csv) as **one separate design**. Select two assembled boards, two copper layers, 1.6 mm FR-4, 1 oz finished copper, and top-side SMT assembly only. The ZIP includes both silk/mask/copper layers and separate PTH/NPTH drill files. Use the supplied absolute origin throughout; do not apply an auxiliary-origin shift to just the CPL.

The dated parts/loss model is **$6.88** for two boards, plus **$6.14** for 2 Economic Extended feeder types. This excludes PCB/setup/stencil/joints/shipping/tax/manual parts and is not a checkout quote. Public stock was checked **2 October 2026**; no parts are reserved. Recheck the exact C-codes and quantities when ordering.

[Manual-fit BOM](assembly/manual-bom.csv) lists headers. Panel sockets, wires, host, regulated supply, M2 hardware and enclosure are external items, chosen to suit the build; they are not in the SMT order. This PCB has two 2.2 mm NPTH mounting holes. Keep conductive mounting hardware clear of copper.

Inspect **every IC, diode and polarized capacitor** against its pin 1/polarity in JLCPCB’s interactive placement preview. CPL uses native KiCad CCW angles; the supplier may assign a different zero orientation for its part model. That preview has not been checked or approved here. Correct supplier-side rotations before paying.

[Verification summary](verification/validation.json) records clean ERC/DRC/parity, physical pad/net checks, uniform widths and zero inter-module dependencies. These are design checks; the M1 boards have not been built or bench-tested. Complete the commissioning checks above before treating them as validated hardware.
