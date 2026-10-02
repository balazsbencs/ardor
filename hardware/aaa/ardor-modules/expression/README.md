# Ardor Expression pedal — M1

Passive potentiometer to a local ADS1115 ADC on a 3.3 V I²C host. No MIDI board or external ADC is required.

Power from an external regulated **3.3 V / 15 mA** supply budget. These figures reserve operating margin; they are not measured consumption. No onboard input regulator or reverse-power protection is provided. Use only the named rails and connect host GND. Budget each fitted module separately.

[Single-page schematic](review/schematic.pdf) · [Component placement](review/pcb-assembly.svg) · [Front routing](review/pcb-front.svg) · [Back routing and connector legends](review/pcb-back.svg)

## Connector pins

All connectors/jumpers are 2.54 mm headers. **Square pad = pin 1.** Pin numbers below are native schematic/PCB numbers, not left-to-right screen positions. Back-side text is read from the back of the physical board. The same reference names appear on multiple modules; use this board’s table. **J101 pin assignments differ between module types; harnesses are not interchangeable.**

| Connector | Pin | Signal |
|---|---|---|
| J302 | 1 | EXP_TIP |
| J302 | 2 | EXP_RING |
| J302 | 3 | GND |
| J101 | 1 | +3V3 |
| J101 | 2 | GND |
| J101 | 3 | SDA |
| J101 | 4 | SCL |
| JP301 | 1 | EXP_TIP |
| JP301 | 2 | EXP_RING |
| JP301 | 3 | EXP_WIPER |
| JP301 | 4 | EXP_EXC |
| JP301 | 5 | EXP_RING |
| JP301 | 6 | EXP_TIP |
| JP302 | 1 | GND |
| JP302 | 2 | ADDR |
| JP302 | 3 | +3V3_A |
| JP303 | 1 | SDA_PULL |
| JP303 | 2 | SDA |
| JP304 | 1 | SCL_PULL |
| JP304 | 2 | SCL |

## Wiring and commissioning

Use a **passive 10 kΩ–100 kΩ linear expression potentiometer**. Wire J302 to TRS: pin 1 tip, pin 2 ring, pin 3 sleeve/GND. The sleeve and a conductive jack bushing bond the aluminium enclosure through the local GND/CHASSIS link. Powered CV pedals and audio signals are outside this interface’s design range.

Set JP301 with **two 2.54 mm shunts**:

| Pedal convention | Shunts |
|---|---|
| Tip is wiper; ring is excitation | 1–3 and 2–4 |
| Ring is wiper; tip is excitation | 3–5 and 4–6 |

JP301’s numbered rows are `1 2`, `3 4`, `5 6`. Use the square pad to locate pin 1; the physical board orientation may rotate this map. Do not short 1–2, 3–4 or 5–6. Change the polarity with power off.

**JP302 requires one shunt:** 1–2 selects I²C address **0x48**, 2–3 selects **0x49**. Never leave ADDR floating. JP303 enables the local SDA pull-up and JP304 enables SCL. Fit both when the host has no pull-ups; remove both when using a Raspberry Pi’s already pulled-up I²C bus. Local pull-ups are 2.2 kΩ to the filtered 3.3 V rail. Do not use a 5 V I²C host without external level translation.

For the first software test, use I²C at 100 kHz, single-ended AIN0 for wiper and AIN1 for excitation, ADS1115 PGA ±4.096 V and 128 samples/s. Calculate wiper/excitation, then calibrate pedal endpoints and smooth the ratio. Excitation includes a 1 kΩ limiter, so its value depends on pedal resistance; measuring AIN1 avoids assuming an exact 3.3 V reference. AIN2 and AIN3 are tied to GND; ALERT/RDY is unused.

Fit J101/J302, JP301–304 and three mandatory shunts by hand. Two additional pull-up shunts are optional according to the host. Bring-up: measure filtered 3.3 V, read the selected ADC address, then sweep the pedal and verify a monotonic ratio. Test both supported polarity settings and unplug/replug behavior. Rail clamps and TVS are protection components, not a guarantee of indefinite survival with an incorrectly powered pedal.

## Order this board

Upload [the fabrication ZIP](assembly/Ardor_Expression-JLCPCB-Gerbers.zip), [SMT BOM](assembly/jlcpcb-bom.csv) and [CPL](assembly/jlcpcb-cpl.csv) as **one separate design**. Select two assembled boards, two copper layers, 1.6 mm FR-4, 1 oz finished copper, and top-side SMT assembly only. The ZIP includes both silk/mask/copper layers and separate PTH/NPTH drill files. Use the supplied absolute origin throughout; do not apply an auxiliary-origin shift to just the CPL.

The dated parts/loss model is **$8.06** for two boards, plus **$9.21** for 3 Economic Extended feeder types. This excludes PCB/setup/stencil/joints/shipping/tax/manual parts and is not a checkout quote. Public stock was checked **2 October 2026**; no parts are reserved. Recheck the exact C-codes and quantities when ordering.

[Manual-fit BOM](assembly/manual-bom.csv) lists headers, jumpers/shunts. Panel sockets, wires, host, regulated supply, M2 hardware and enclosure are external items, chosen to suit the build; they are not in the SMT order. This PCB has two 2.2 mm NPTH mounting holes. Keep conductive mounting hardware clear of copper.

Inspect **every IC, diode and polarized capacitor** against its pin 1/polarity in JLCPCB’s interactive placement preview. CPL uses native KiCad CCW angles; the supplier may assign a different zero orientation for its part model. That preview has not been checked or approved here. Correct supplier-side rotations before paying.

[Verification summary](verification/validation.json) records clean ERC/DRC/parity, physical pad/net checks, uniform widths and zero inter-module dependencies. These are design checks; the M1 boards have not been built or bench-tested. Complete the commissioning checks above before treating them as validated hardware.
