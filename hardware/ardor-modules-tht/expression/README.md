# Passive expression pedal / I2C ADC / HAND ASSEMBLY

T1.1 compact hand-assembly PCB: 72 × 50 mm. **Unbuilt engineering prototype.**

[KiCad project](Ardor_Expression_THT.kicad_pro) · [Manual BOM](BOM.csv) · [Schematic](review/schematic.pdf) · [Bare-board Gerbers](assembly/Ardor_Expression_THT-Gerbers.zip)

Fit the **Hestore ADS1115-M, item 100.417.98**, using one **ten-pin 2.54 mm female socket** and a matching straight male header on the module. With the component side up, its 27.5 × 17 mm body lies above the header row inside the printed outline. The left-to-right order is **VDD, GND, SCL, SDA, ADDR, ALRT, A0, A1, A2, A3**. VDD is the square carrier pad (logical U301 pad 8), at the left end. See the [breakout pin translation](../SOURCES.md#ads1115-breakout-pin-translation). Do not rotate it 180 degrees. Adafruit and ADS1115-M2MINI boards do not match this footprint.

The purchased breakout already contains all small SMD parts. Only its ordinary header pins require soldering. ALRT is not wired; A2/A3 connect to ground. The module has an onboard 10 kΩ ADDR pull-down. JP302 must have one shunt: 1–2 selects 0x48, 2–3 selects 0x49; HIGH sources about 0.33 mA through the pull-down. No onboard resistor removal is required. ADS1115 registers/channels/addresses stay the same as M1.

The supplier schematic labels its chip ADS1015 while the product is sold as 16-bit ADS1115. Before relying on the expected ADC resolution, confirm the actual supplied chip/module. At the driver's 128 SPS ADS1115 setting, check single-shot conversion busy time near 7.8 ms; an ADS1015 at the corresponding data-rate bits runs near 0.625 ms. Check multiple known input voltages as well. A module containing ADS1015 fails the specified ADS1115 assembly requirement even if its I²C address responds.

Use a passive 10–100 kΩ expression pot; do not connect powered CV. JP301 needs **two** shunts: 1–3 plus 2–4 for tip wiper, or 3–5 plus 4–6 for ring wiper. TRS sleeve goes to J302.3. Change polarity with power off.

Leave **JP303 and JP304 unshunted initially**. The breakout has permanent onboard 10 kΩ SDA/SCL pull-ups; these jumpers control only the additional carrier 2.2 kΩ pair. With a Raspberry Pi's 1.8 kΩ pair, the effective resistance is approximately 1.53 kΩ before adding any other module. Add the carrier pair only after calculating total bus pull-up current and checking cable rise time. Removing the carrier shunts does not remove the breakout's pull-ups.

Supply 3.3 V / 15 mA. Commission without the breakout first: verify rail polarity at the socket, then fit the breakout and check I²C detection at 100 kHz. Select ±4.096 V PGA and 128 SPS, read AIN0 (wiper) and AIN1 (excitation reference), normalize AIN0/AIN1, calibrate endpoints and smooth. Test both polarity settings and open-cable behavior. Do not drive the pedal while the module is unpowered.

## Connector pin assignments

Directions are relative to this board. Standard header square pads identify pin 1. Read reference and pin numbers from the back silkscreen; mirrored CAD exports are labelled as back views.

### J302 — PASSIVE PEDAL INPUT / EXCITATION OUTPUT

| Pin | Net | Direction | Connect to |
|---|---|---|---|
| 1 | EXP_TIP | INPUT / OUTPUT | Passive expression socket tip; not a powered CV pedal. |
| 2 | EXP_RING | INPUT / OUTPUT | Passive expression socket ring; not a powered CV pedal. |
| 3 | GND | GROUND | TRS sleeve; pedal pot ground and enclosure bond. |

### J101 — 3V3 POWER INPUT + I2C TO HOST

| Pin | Net | Direction | Connect to |
|---|---|---|---|
| 1 | +3V3 | POWER INPUT | Positive output of the matching regulated supply. |
| 2 | GND | GROUND | Host or cable ground / 0 V. |
| 3 | SDA | INPUT / OUTPUT | Host I2C SDA data pin. |
| 4 | SCL | INPUT | Host I2C SCL clock pin. |

### JP301 — JUMPER ONLY: PEDAL POLARITY

| Pin | Net | Direction | Connect to |
|---|---|---|---|
| 1 | EXP_TIP | JUMPER | Fit a shunt to pin 3 for tip-wiper mode. |
| 2 | EXP_RING | JUMPER | Fit a shunt to pin 4 for tip-wiper mode. |
| 3 | EXP_WIPER | INPUT / JUMPER | Shunt 1-3 for tip wiper, or 3-5 for ring wiper. |
| 4 | EXP_EXC | OUTPUT / JUMPER | Shunt 2-4 for tip wiper, or 4-6 for ring wiper. |
| 5 | EXP_RING | JUMPER | Fit a shunt to pin 3 for ring-wiper mode. |
| 6 | EXP_TIP | JUMPER | Fit a shunt to pin 4 for ring-wiper mode. |

TIP: 1-3 + 2-4; RING: 3-5 + 4-6. Use TWO shunts; no external cable.

### JP302 — JUMPER ONLY: ADC ADDRESS

| Pin | Net | Direction | Connect to |
|---|---|---|---|
| 1 | GND | JUMPER | Shunt 1-2 selects address 0x48. |
| 2 | ADDR | CONTROL / JUMPER | Fit ONE shunt: 1-2 or 2-3. |
| 3 | +3V3_A | JUMPER | Shunt 2-3 selects address 0x49. |

1-2: 0x48; 2-3: 0x49. One shunt required; no external cable.

### JP303 — JUMPER ONLY: SDA PULLUP

| Pin | Net | Direction | Connect to |
|---|---|---|---|
| 1 | SDA_PULL | JUMPER | Shunt 1-2 only if the host has no SDA pull-up. |
| 2 | SDA | INPUT / OUTPUT / JUMPER | Same shunt; this is not a host cable connector. |

Fit 1-2 to enable. Remove if host already has a pull-up.

### JP304 — JUMPER ONLY: SCL PULLUP

| Pin | Net | Direction | Connect to |
|---|---|---|---|
| 1 | SCL_PULL | JUMPER | Shunt 1-2 only if the host has no SCL pull-up. |
| 2 | SCL | INPUT / JUMPER | Same shunt; this is not a host cable connector. |

Fit 1-2 to enable. Remove if host already has a pull-up.

## Assembly and files

Fit low-profile axial parts first, then sockets/headers and radial capacitors. Match diode bands, electrolytic polarity and IC notches to the PCB. Install the ICs or ADC breakout last. H1/H2 are 2.2 mm nonplated M2 mounting holes, not electrical terminals. Check enclosure clearance and capacitor/socket height. T1.1 compacts the layouts; hole positions differ from T1 and M1.

Use the manual BOM and the bare-board Gerber/drill ZIP. There is no SMT assembly order. Front silkscreen shows component references; the front fabrication drawing shows package outlines and orientation; use the BOM for values. Back silk includes port functions and pin numbers. Native checks and package manifests are under `verification/`; they do not replace prototype measurements.

[Assembly accessories](assembly/accessories.csv) list sockets, shunts and panel items in addition to the PCB component BOM.

[Family guide](../README.md) · [Substitution sources](../SOURCES.md) · [Native validation](verification/validation.json)
