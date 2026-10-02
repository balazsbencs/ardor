# Ardor DIN MIDI input — M1

Isolated current-loop input to a 3.3 V UART. No expression board, mixer, codec, carrier or shared bias circuit is required.

Power from an external regulated **5 V / 10 mA and 3.3 V / 5 mA** supply budget. These figures reserve operating margin; they are not measured consumption. No onboard input regulator or reverse-power protection is provided. Use only the named rails and connect host GND. Budget each fitted module separately.

[Single-page schematic](review/schematic.pdf) · [Component placement](review/pcb-assembly.svg) · [Front routing](review/pcb-front.svg) · [Back routing and connector legends](review/pcb-back.svg)

[Short assembly guide](ASSEMBLY.md) · [Beginner pin card](review/wiring-guide.pdf)

## Connector pins

All connectors/jumpers are 2.54 mm headers. **Square pad = pin 1.** Pin numbers below are native schematic/PCB numbers, not left-to-right screen positions. Back-side text is read from the back of the physical board. The same reference names appear on multiple modules; use this board’s table. **J101 pin assignments differ between module types; harnesses are not interchangeable.**

| Connector | Pin | Direction / function | Connect to |
|---|---|---|---|
| J202 | 1 | **MIDI INPUT** — MIDI current-loop input, DIN contact 4. | Female DIN socket numbered contact 4. |
| J202 | 2 | **MIDI INPUT** — MIDI current-loop return, DIN contact 5; NOT GND. | Female DIN socket numbered contact 5. |
| J101 | 1 | **POWER INPUT** — Feed regulated 5V into the module. This pin does not supply power. | Positive output of the matching regulated supply. |
| J101 | 2 | **POWER INPUT** — Feed regulated 3V3 into the module. This pin does not supply power. | Positive output of the matching regulated supply. |
| J101 | 3 | **GROUND** — Common 0 V return; not a signal or positive supply. | Host or cable ground / 0 V. |
| J101 | 4 | **OUTPUT** — 3.3 V decoded MIDI logic leaves the module; this is not a DIN MIDI output. | Host UART RX input, 31250 baud, 8-N-1. Do not connect to host TX. |
| J203 | 1 | **BOND** — Enclosure/chassis connection, locally joined to GND through R101. | Aluminium enclosure bonding point; insulate the MIDI DIN shell. |
| J203 | 2 | **GROUND** — Common 0 V return; not a signal or positive supply. | Host or cable ground / 0 V. |

**IN** enters this board. **OUT** leaves this board. **I/O** uses both directions. **GND** is the common 0 V return. **3V3** means 3.3 V.

Unused component pins: **U201.3** — NC - LEAVE OPEN. The factory/hand solder joint remains; do not add an external wire.

H1/H2 are mounting holes. Small via holes and other component pads are not wire connectors.

## Wiring and commissioning

Connect J202 pin 1 to contact **4** and pin 2 to contact **5** of a female 5-pin DIN socket. Use the socket manufacturer’s numbered terminals; the solder-side view is mirrored relative to the mating view. Leave contacts 1, 2, 3 and the socket shell unconnected, and insulate the MIDI socket shell from the enclosure. This preserves the MIDI input current-loop isolation.

J101 pin 4 is **MIDI_RX**, a 3.3 V logic output: UART RX, 31,250 baud, 8 data bits, no parity, one stop bit. It is not a MIDI output or USB interface. J101 needs both regulated supply rails; the 5 V rail powers the H11L1M and the 3.3 V rail sets the UART output high level.

For a metal enclosure, bond J203 pin 1 (CHASSIS) directly to the enclosure. R101 already joins CHASSIS to local GND; J203 pin 2 provides a GND terminal, not an isolated ground. This bond does not require an audio-jack module. In a plastic enclosure retain the local CHASSIS/GND network and omit the enclosure wire. The input-side keepout excludes both GND pours; the native rule requires 3 mm between current-loop and logic copper except the intentional ESD/RF network. This is functional MIDI ground-loop isolation, not certified safety isolation.

Fit U201 **onsemi H11L1M**, DIP-6, by hand, observing pin 1/notch. H11L1M is not interchangeable with a bare phototransistor optocoupler. Fit J101/J202/J203 headers or solder cables to the numbered pads. The receiver includes reverse LED protection, differential TVS, common-mode chassis TVS and RF capacitors.

Bring-up: verify 5 V and 3.3 V first. With no MIDI source, RX should be high. Connect a known MIDI transmitter and confirm correctly decoded messages without parity/framing errors. Verify no DC connection from DIN contacts 4/5 to logic ground when the protection devices are not conducting.

## Order this board

Upload [the fabrication ZIP](assembly/Ardor_MIDI-JLCPCB-Gerbers.zip), [SMT BOM](assembly/jlcpcb-bom.csv) and [CPL](assembly/jlcpcb-cpl.csv) as **one separate design**. Select two assembled boards, two copper layers, 1.6 mm FR-4, 1 oz finished copper, and top-side SMT assembly only. The ZIP includes both silk/mask/copper layers and separate PTH/NPTH drill files. Use the supplied absolute origin throughout; do not apply an auxiliary-origin shift to just the CPL.

The dated parts/loss model is **$5.22** for two boards, plus **$9.21** for 3 Economic Extended feeder types. This excludes PCB/setup/stencil/joints/shipping/tax/manual parts and is not a checkout quote. Public stock was checked **2 October 2026**; no parts are reserved. Recheck the exact C-codes and quantities when ordering.

[Manual-fit BOM](assembly/manual-bom.csv) lists headers, optocoupler. Panel sockets, wires, host, regulated supply, M2 hardware and enclosure are external items, chosen to suit the build; they are not in the SMT order. This PCB has two 2.2 mm NPTH mounting holes. Keep conductive mounting hardware clear of copper.

Inspect **every IC, diode and polarized capacitor** against its pin 1/polarity in JLCPCB’s interactive placement preview. CPL uses native KiCad CCW angles; the supplier may assign a different zero orientation for its part model. That preview has not been checked or approved here. Correct supplier-side rotations before paying.

[Verification summary](verification/validation.json) records clean ERC/DRC/parity, physical pad/net checks, uniform widths and zero inter-module dependencies. These are design checks; the M1 boards have not been built or bench-tested. Complete the commissioning checks above before treating them as validated hardware.
