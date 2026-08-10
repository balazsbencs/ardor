# GCB-95 extracted netlist

Source: [`dunlop-crybaby-wah.asc`](https://github.com/Cushychicken/ltspice-guitar-pedals/tree/master/dunlop-crybaby-wah),
retrieved 2026-08-06.

Connectivity was resolved from the file's wire graph by union-find over wire
endpoints, with symbol pins placed from LTspice's standard pin offsets. Every
pin landed exactly on a wire endpoint, which is what makes the extraction
self-checking rather than a reading of a picture.

Transistor part numbers come from the schematic's own annotations and agree
with the [ElectroSmash GCB-95
analysis](https://electrosmash.mas-effects.com/crybaby-gcb-95.html).

## Nodes

| Node | Meaning |
| --- | --- |
| `Vin` | Input, after the C3 coupling cap; Q1 base |
| `n3` | Q1 collector |
| `n7` | Q1 emitter |
| `n2` | R5 output, feeding C2 |
| `n1` | Q2 base |
| `n8` | Q2 collector |
| `n9` | Q2 emitter |
| `n5` | Tank node; L1/R9 low side, C7 feedback injection point |
| `n6` | Tank node; L1/R9 high side |
| `Vout` | Output, after the C1 coupling cap |
| `Vfb_in` | Pot wiper (R14/R12 divider tap) |
| `n10` | Q3 base |
| `n11` | Q3 collector |
| `Vfb` | Q3 emitter; drives C7 into the tank |
| `V9P0` | 9 V supply rail |
| `0` | Ground |

## Devices

| Device | Type | Nodes | Value |
| --- | --- | --- | --- |
| V1 | supply | `V9P0`–`0` | 9 V |
| D1 | zener | `0`–`V9P0` | TFZ9_1B (reverse-polarity protection) |
| C4 | cap | `V9P0`–`0` | 220 µF |
| C5 | cap | `V9P0`–`0` | 0.1 µF |
| C3 | cap | `Vin`–source | 10 nF |
| R2 | res | `n3`–`Vin` | 1 MΩ |
| R3 | res | `Vin`–`0` | 2.2 MΩ |
| C6 | cap | `Vin`–`0` | 22 pF |
| Q1 | npn | B=`Vin` C=`n3` E=`n7` | MPSA13 (Darlington) |
| R1 | res | `n3`–`V9P0` | 1 kΩ |
| R4 | res | `0`–`n7` | 10 kΩ |
| R5 | res | `n7`–`n2` | 68 kΩ |
| C2 | cap | `n1`–`n2` | 10 nF |
| R10 | res | `n5`–`n1` | 1.5 kΩ |
| Q2 | npn | B=`n1` C=`n8` E=`n9` | MPSA18 |
| R6 | res | `n8`–`V9P0` | 22 kΩ |
| R7 | res | `0`–`n9` | 390 Ω |
| R8 | res | `n8`–`n6` | 470 kΩ |
| R9 | res | `n6`–`n5` | 33 kΩ |
| L1 | ind | `n6`–`n5` | 500 mH |
| C11 | cap | `n6`–`0` | 4.7 µF |
| R16 | res | `n6`–`0` | 82 kΩ |
| C1 | cap | `Vout`–`n8` | 0.22 µF |
| R14 | res | `Vfb_in`–`Vout` | 100 kΩ − wiper |
| R12 | res | `0`–`Vfb_in` | wiper |
| C8 | cap | `n10`–`Vfb_in` | 0.22 µF |
| R11 | res | `n10`–`n8` | 470 kΩ |
| Q3 | npn | B=`n10` C=`n11` E=`Vfb` | MPSA18 |
| R18 | res | `n11`–`V9P0` | 1 kΩ |
| R19 | res | `0`–`Vfb` | 10 kΩ |
| C7 | cap | `Vfb`–`n5` | 10 nF |

## Notes for the DK derivation

- **L1 is in parallel with R9**, not in series with a small winding resistance.
  R9 at 33 kΩ is the dominant damping term; the winding resistance is a second-
  order correction.
- **The pot is a divider, not a series element.** R14 (`Vfb_in`–`Vout`) and R12
  (`0`–`Vfb_in`) sum to the 100 kΩ track. Sweeping the tap changes how much of
  the output reaches Q3, which changes the feedback injected into the tank —
  that is the sweep mechanism.
- **Q3 is a feedback follower**, a detail the ElectroSmash prose does not make
  explicit. The loop is `Vout` → pot → C8 → Q3 → C7 → tank node `n5`.
- **Reactive elements in the signal path (8 states):** C3, C6, C2, C11, L1, C1,
  C8, C7. C4 and C5 sit across the supply rail and are excluded — the rail is
  modelled as an ideal 9 V source.
- **Nonlinear ports (2):** the base-emitter junctions of Q2 and Q3. Q1 is
  linearized.
