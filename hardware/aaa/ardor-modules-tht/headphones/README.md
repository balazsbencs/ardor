# Stereo headphones / OPA1656 / relay mute

T1 hand-assembly PCB: 96 × 84 mm. **Unbuilt engineering prototype.**

[KiCad project](Ardor_Headphones_THT.kicad_pro) · [Manual BOM](BOM.csv) · [Schematic](review/schematic.pdf) · [Bare-board Gerbers](assembly/Ardor_Headphones_THT-Gerbers.zip)

Supply regulated **5 V ±5% / 150 mA** and use 32–300 Ω headphones. Fit one **OPA1656ID**, ordinary **SOIC-8, 1.27 mm pitch**, directly to U601. This is the board's only SMD component; all supporting components are through-hole. Use the dot/notch and pad 1 to orient it. Solder it first with flux and a fine iron, inspect all eight joints for bridges, then fit the THT parts. There is no exposed thermal pad, adapter, DIP socket or charge pump. Fit **G5V-2-H1 DC5** high-sensitivity DPDT relay and onsemi 2N3904BU; the standard non-H1 relay has a different coil budget.

Input limit is 1 Vrms per channel. The 20 kΩ input and 10 kΩ feedback resistors give **−0.5 unloaded gain**. Both channels invert polarity together. The 10 kΩ feedback resistors R651/R652 stand upright with **2.54 mm lead spacing**; bend the upper lead back down and keep the loop short. They are axial parts in vertical footprints, unlike the other resistors at 10.16 mm pitch. Fit 100 pF C0G feedback capacitors C661/C662. R621/R622 and C631/C632 generate a filtered 2.5 V bias. Both op-amp inputs stay at this bias, within the restricted common-mode range; do not convert this to a noninverting circuit. C641 is the local 100 nF bypass, **2.5 mm pitch, body ≤3.8 mm diameter and ≤2.6 mm thick**. Trim its leads close to the PCB.

R631/R632 provide **10 Ω output isolation before the coupling capacitors**, outside the feedback loops. C651/C652 are 470 µF / 16 V, positive toward the amplifiers, body ≤8 mm diameter and 3.5 mm lead spacing. C601/C602 are 10 µF **bipolar** input capacitors; C631 is 47 µF polarized with positive toward VREF. Calculated midband gain into 32 Ω is about −0.378, or approximately 4.5 mW/channel at 1 Vrms input. These are ideal circuit calculations, not measured clean-power ratings. The OPA1656's typical ±100 mA short-circuit current is not a low-distortion headphone-drive guarantee. Both channels remain powered when muted.

**Keep HP_ENABLE LOW for at least five seconds after stable supply/audio; mute before powering down.** HIGH is 3.3 V and draws approximately 5 mA. This timing is supplied by the host, not an onboard timer. R641/R642 are 1 kΩ bleeders which charge the coupling capacitors while the relay grounds the panel channels. Switching early can produce a loud transient. The relay commons are panel left/right, NC contacts ground them, and NO contacts select the AC-coupled audio.

Commission into separate **32 Ω dummy resistors**, with no headphones connected. Check filtered supply at U601 pin 8, ground at pin 4, about 2.5 V at pins 3/5 and output pins 1/7, correct capacitor polarity, grounded panel outputs when muted, and settled near-zero DC at the relay's audio feeds before enabling. Capture startup, enable, disable and power-down waveforms, including host reset/disconnect. Measure gain, 20 Hz–20 kHz response, clipping, channel separation, hum, hiss and thermal behavior at supply limits. Inspect for high-frequency oscillation with the actual headphone cable and representative capacitive loads (for example 100 pF and 1 nF), including unplugged output and TRS insertion shorts. Retain the isolation resistors and local feedback/decoupling layout. Begin listening at low host volume only after these checks pass.

## Connector pin assignments

Directions are relative to this board. Standard header square pads identify pin 1. Read reference and pin numbers from the back silkscreen; mirrored CAD exports are labelled as back views.

### J101 — 5V POWER + ENABLE INPUT

| Pin | Net | Direction | Connect to |
|---|---|---|---|
| 1 | +5V | POWER INPUT | Positive output of the matching regulated supply. |
| 2 | GND | GROUND | Host or cable ground / 0 V. |
| 3 | HP_ENABLE | CONTROL INPUT | Host control output. Keep LOW until audio and supply are stable. |

### J102 — STEREO AUDIO INPUT FROM SOURCE

| Pin | Net | Direction | Connect to |
|---|---|---|---|
| 1 | AUDIO_L | INPUT | DAC/codec left audio output, <=1 Vrms. |
| 2 | GND | GROUND | Host or cable ground / 0 V. |
| 3 | AUDIO_R | INPUT | DAC/codec right audio output, <=1 Vrms. |

### J602 — STEREO HEADPHONE AUDIO OUTPUT

| Pin | Net | Direction | Connect to |
|---|---|---|---|
| 1 | HP_L | OUTPUT | TRS socket tip; headphones >=32 ohm. |
| 2 | HP_R | OUTPUT | TRS socket ring; headphones >=32 ohm. |
| 3 | GND | GROUND | TRS socket sleeve; enclosure bond. |

## Assembly and files

Solder and inspect U601 SOIC-8 first; no IC socket is used. Then fit upright feedback resistors, other axial parts, headers and radial capacitors. Match diode bands, electrolytic polarity and IC orientation to the PCB. H1/H2 are 2.2 mm nonplated M2 mounting holes, not electrical terminals. Check enclosure clearance and capacitor/socket height: these boards are larger than M1.

Use the manual BOM and the bare-board Gerber/drill ZIP. There is no SMT assembly order. Front silkscreen shows component references; the front fabrication drawing shows package outlines and orientation; use the BOM for values. Back silk includes port functions and pin numbers. Native checks and package manifests are under `verification/`; they do not replace prototype measurements.

[Assembly accessories](assembly/accessories.csv) list sockets, shunts and panel items in addition to the PCB component BOM.

[Family guide](../README.md) · [Substitution sources](../SOURCES.md) · [Native validation](verification/validation.json)
