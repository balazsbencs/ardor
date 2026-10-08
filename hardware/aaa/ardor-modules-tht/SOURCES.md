# T1 component choices and source evidence

Reviewed 8 October 2026. These are electrical/package references, not stock or price guarantees. The checked-in BOM lists exact semiconductor choices and dimensional purchasing specifications for commodity passives. Footprints are frozen locally; source files under `hardware/aaa/sources/` are untouched.

| Change | Primary source | Design decision |
|---|---|---|
| TLV9002 SOIC → MCP6022-I/P DIP-8 | [Microchip MCP6022 datasheet](https://www.microchip.com/content/dam/mchp/documents/APID/ProductDocuments/DataSheets/20001685E.pdf) | 2.5–5.5 V, unity-stable rail-to-rail dual amplifier with the same 1/2/3, 5/6/7, 4/8 pin functions. Used at 5 V with short feedback links. Higher bandwidth/current and different noise remain bench-test items. |
| Bare ADS1115 → assembled breakout | [TI ADS1115](https://www.ti.com/lit/ds/symlink/ads1115.pdf), [Adafruit 1085](https://www.adafruit.com/product/1085), [Adafruit PCB source](https://github.com/adafruit/ADS1X15-Breakout-Board-PCBs/tree/5aa7dbfdd92692a7751740b0f622606c2ff9ca55) | Preserve ADC registers, channels and addresses. The footprint matches `Adafruit ADS1115 ADC STEMMA QT.brd`, two six-pin 2.54 mm rows 12.7 mm apart, 25.4 × 17.78 mm body. Earlier Adafruit revisions and generic blue modules do not fit this footprint. |
| AO3400A → onsemi 2N3904BU | [onsemi 2N3904 datasheet](https://www.onsemi.com/pdf/datasheet/2n3903-d.pdf) | Physical TO-92 pin order E1/B2/C3. 470 Ω base resistor supplies about 5 mA from 3.3 V; 100 kΩ base pulldown and axial flyback diode remain. A 2N7000 was avoided because gate threshold alone does not guarantee conduction at 3.3 V. |
| TPA6132A2 QFN → two LM386N-1/NOPB DIP-8 | [TI LM386 datasheet](https://www.ti.com/lit/ds/symlink/lm386.pdf) | 5 V operation, ground-referenced inputs, pins 1/8 open for nominal gain 20. External 39 kΩ/1 kΩ attenuation, bypass capacitors, 10 Ω/47 nF Zobel networks, 470 µF output coupling. This is a different audio architecture with no charge pump. |
| Stereo output mute | [Omron G5V-2 datasheet](https://components.omron.com/system/files/2023-01/datasheet_pdf/K046-E1.pdf) | Exact **G5V-2-H1 DC5**: high-sensitivity 5 V, 30 mA coil. Common contacts 4/13 go to left/right panel outputs; NC 6/11 to GND; NO 8/9 to the AC-coupled audio feeds. The ordinary non-H1 relay has a different coil budget. |
| Line relay unchanged | [Omron G5V-1](https://omronfs.omron.com/en_US/ecb/products/pdf/en-g5v_1.pdf) | G5V-1 DC5 remains through-hole; its NC grounds the panel output and NO selects audio. |
| SMT ferrites → axial Würth 7427501 | [Würth drawing/electrical data](https://www.we-online.com/components/products/datasheet/7427501.pdf) | 800 Ω at 100 MHz, 3 A rating, 20 mΩ max DC resistance; body 10 ±0.5 mm long, 6 ±0.3 mm diameter; 0.5 ±0.1 mm wire. Form leads to 15.24 mm, use 0.9 mm drills. RF response differs from M1's 120/600 Ω beads. |
| Small SMT TVS → axial bidirectional SA series | [Vishay SA series](https://www.vishay.com/doc/?88378=) | SA5.0CA-E3/54 for 5 V/bipolar audio interfaces; SA24CA-E3/54 for MIDI-to-chassis branches. DO-15 at 10.16 mm pitch. Changed capacitance/layout must be assessed on real MIDI/audio cables; no EMC equivalence claim. |
| BAT54 → axial BAT85S | [Vishay BAT85S](https://www.vishay.com/docs/85513/bat85s.pdf) | Schottky rail clamps after 10 kΩ expression input resistors. DO-35 at 7.62 mm pitch; cathode is pad 1. |
| 1N4148W → 1N4148-TAP | [Vishay 1N4148](https://www.vishay.com/docs/81857/1n4148.pdf) | DO-35 at 7.62 mm pitch for MIDI reverse protection and relay flyback. |
| Ceramic audio coupling → radial bipolar electrolytic | [Nichicon UES series](https://www.nichicon.co.jp/english/products/pdfs/e-ues.pdf) | UES1E100MDM, 10 µF / 25 V, 2.5 mm pitch, body fits the 6.3 mm envelope. Used for mixer input/output coupling and the line board's input/internal amp feed. No DC-polarity assumption about the external source/load. |
| MIDI optocoupler unchanged | [onsemi H11L1M](https://www.onsemi.com/download/data-sheet/pdf/h11l3m-d.pdf) | DIP-6 with 5 V supply and 3.3 V output pull-up. Preserve functional isolation spacing/keepout; not safety isolation. |

## ADS1115 breakout pin translation

The carrier footprint pad numbers intentionally follow the **ADC's logical pin functions**, not the breakout header's left-to-right ordinal. Viewing the assembled module from above, with the digital labels along the lower edge:

| Row, left → right | Carrier footprint pad numbers |
|---|---|
| Upper: AVDD, A3, A2, A1, A0, AGND | 11 (NC), 7 (GND), 6 (GND), 5 (reference ADC), 4 (wiper ADC), 12 (NC) |
| Lower: VDD, GND, SCL, SDA, ADDR, ALRT | 8 (3.3 V), 3 (GND), 10 (clock), 9 (data), 1 (address), 2 (NC) |

AVDD/AGND are already connected inside the breakout through its ferrites and are deliberately left unwired on the carrier. Do not connect them to another supply. The breakout's onboard 10 kΩ pull-ups remain fitted; JP303/304 control only the extra carrier 2.2 kΩ pair. Leave both carrier pull-up shunts off initially. The breakout ADDR-to-VDD solder jumper must remain open because JP302 controls the address.

## Electrical calculations, not measured performance

For the headphone input, `1k || 50k = 980.4 Ω`; `20 × 980.4 / (39000 + 980.4) = 0.4905` nominal unloaded gain. With 32 Ω headphones, the 2.2 Ω series resistor and 1 kΩ bleeder give about 0.458 nominal midband gain. Output coupling has a roughly 10.2 Hz pole at 32 Ω and nominal 470 µF. Capacitor tolerance and load affect this figure.

At startup, each 1 kΩ bleeder charges its 470 µF capacitor with a nominal 0.47 s time constant. Five seconds covers more than eight time constants even at +20% capacitance and +1% resistance, reducing an initial 2.5 V at the relay feed to below 0.4 mV in this simple model. Actual DC, switching transients and audio settling must be measured before connecting headphones. The host supplies the delay; there is no timer on the PCB.
