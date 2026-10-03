# Ardor Optional stereo headphones — M1

An external stereo line-level source feeds the local TPA6132A2 and charge pump. No mixer or external negative supply is required.

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
| J102 | 1 | **INPUT** — LEFT audio enters the module. | DAC/codec left audio output, <=1 Vrms. |
| J102 | 2 | **GROUND** — Common 0 V return; not a signal or positive supply. | Host or cable ground / 0 V. |
| J102 | 3 | **INPUT** — RIGHT audio enters the module. | DAC/codec right audio output, <=1 Vrms. |
| J602 | 1 | **OUTPUT** — LEFT audio leaves the module. | TRS socket tip; headphones >=32 ohm. |
| J602 | 2 | **OUTPUT** — RIGHT audio leaves the module. | TRS socket ring; headphones >=32 ohm. |
| J602 | 3 | **GROUND** — Common 0 V return; not a signal or positive supply. | TRS socket sleeve; enclosure bond. |

**IN** enters this board. **OUT** leaves this board. **I/O** uses both directions. **GND** is the common 0 V return. **3V3** means 3.3 V.

H1/H2 are mounting holes. Small via holes and other component pads are not wire connectors.

## Wiring and commissioning

Connect an external stereo line-level DAC/codec to J102: pin 1 left, pin 2 ground, pin 3 right; each **≤1 Vrms**. J602 goes to a TRS headphone socket: pin 1 tip/left, pin 2 ring/right, pin 3 sleeve/GND. The sleeve/bushing bonds the enclosure through the local CHASSIS/GND link. Use headphones of **32 Ω or greater**; there is no balanced output.

J101 pin 3 **HP_ENABLE** is a 3.3 V logic input. LOW or disconnected disables the amplifier; HIGH enables it. Keep it low until audio and 5 V are stable, then start at low volume and ramp up. Do not drive enable above the amplifier’s supply plus 0.3 V when unpowered. Both gain pins are grounded, selecting **−6 dB inverting gain**. A 1 Vrms input gives about 0.47 Vrms / 6.8 mW per channel into 32 Ω after the 2.2 Ω output resistors (nominal calculation, not a measured result).

The TPA6132A2 charge-pump flying/storage capacitors, exposed-pad ground and input DC blocking are all local. HPVDD/HPVSS are internal rails and must not be connected to external power. This module deliberately carries the optional QFN amplifier and its added assembly/X-ray cost so other features do not pay for it. The local 0.5 mm pitch QFN land and 0.2 mm drilled thermal vias are frozen in this project; do not substitute a generic footprint in an ordering copy.

Fit J101/J102/J602 by hand. SMT includes U601 and the charge-pump capacitors. Inspect U601 pin 1 and exposed-pad soldering in the JLCPCB preview and delivered boards. Bring-up into resistive dummy loads first: check supplies, enable operation, channel assignment, output DC, charge-pump ripple, clipping/noise and hot-plug behavior. Then test headphones at low volume.

## Order this board

Upload [the fabrication ZIP](assembly/Ardor_Headphones-JLCPCB-Gerbers.zip), [SMT BOM](assembly/jlcpcb-bom.csv) and [CPL](assembly/jlcpcb-cpl.csv) as **one separate design**. Select two assembled boards, two copper layers, 1.6 mm FR-4, 1 oz finished copper, and top-side SMT assembly only. The ZIP includes both silk/mask/copper layers and separate PTH/NPTH drill files. Use the supplied absolute origin throughout; do not apply an auxiliary-origin shift to just the CPL.

The dated parts/loss model is **$9.93** for two boards, plus **$12.28** for 4 Economic Extended feeder types, plus approximately $3.28 QFN X-ray. This excludes PCB/setup/stencil/joints/shipping/tax/manual parts and is not a checkout quote. Public stock was checked **2 October 2026**; no parts are reserved. Recheck the exact C-codes and quantities when ordering.

[Manual-fit BOM](assembly/manual-bom.csv) lists headers. Panel sockets, wires, host, regulated supply, M2 hardware and enclosure are external items, chosen to suit the build; they are not in the SMT order. This PCB has two 2.2 mm NPTH mounting holes. Keep conductive mounting hardware clear of copper.

Inspect **every IC, diode and polarized capacitor** against its pin 1/polarity in JLCPCB’s interactive placement preview. CPL uses native KiCad CCW angles; the supplier may assign a different zero orientation for its part model. That preview has not been checked or approved here. Correct supplier-side rotations before paying.

[Verification summary](verification/validation.json) records clean ERC/DRC/parity, physical pad/net checks, uniform widths and zero inter-module dependencies. These are design checks; the M1 boards have not been built or bench-tested. Complete the commissioning checks above before treating them as validated hardware.
