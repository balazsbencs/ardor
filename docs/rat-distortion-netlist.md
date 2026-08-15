# ProCo RAT extracted netlist

Source: [`proco-rat-distortion.asc`](https://github.com/Cushychicken/ltspice-guitar-pedals/tree/master/proco-rat-distortion),
retrieved 2026-08-15.

Connectivity was resolved from the file's wire graph by union-find over wire
endpoints, with symbol pins placed from LTspice's standard pin offsets. Run
`scripts/extract-ltspice-netlist.py` on the `.asc` to reproduce this table. The
script checks both directions: every pin must land on a wire endpoint, and every
net a wire reaches must carry at least one pin or a label. Both pass, which is
what makes the extraction self-checking rather than a reading of a picture.

Two pin roles cannot be read off the geometry and were fixed by function.

The **diode** near pin is the anode. D1 sits across the supply for
reverse-polarity protection, which only works one way round, and that settles
the orientation for D2 and D3 as well.

The **LM308** left-hand pins are the two inputs. The feedback network (R9, C9)
has to reach the inverting input or the stage would latch rather than amplify,
so the pin carrying it is `IN-`. This also confirms the topology: the guitar
signal arrives at `IN+`, making the gain stage **non-inverting**, which is the
RAT and not a Tube Screamer.

## Nodes

| Node | Meaning |
| --- | --- |
| `Vin` | Input, at the jack |
| `n5` | After C3; input bias injection point |
| `n2` | LM308 non-inverting input |
| `n1` | LM308 inverting input; the gain network sums here |
| `n6` | R7 low side, above C7 |
| `n7` | R8 low side, above C8 |
| `n3`, `n4` | LM308 compensation pins, bridged by C1 |
| `Vfb` | LM308 output |
| `n8` | After C10, before the clipping series resistor |
| `Vclip` | Diode clipping node |
| `n11` | Between the two filter resistors |
| `Vtone` | Filter output, across C11 |
| `n9` | J1 gate |
| `n10` | J1 source |
| `Vout` | Output, after C13 |
| `V9P0_BATT` | Battery terminal, before the series resistor |
| `V9P0` | 9 V rail |
| `V4P5` | 4.5 V bias rail |
| `0` | Ground |

## Devices

| Device | Type | Nodes | Value |
| --- | --- | --- | --- |
| V1 | supply | `V9P0_BATT`–`0` | 9 V |
| R4 | res | `V9P0`–`V9P0_BATT` | 47 Ω |
| D1 | diode | A=`0` K=`V9P0` | 1N4148 (reverse-polarity protection) |
| C4 | cap | `V9P0`–`0` | 100 µF |
| C5 | cap | `V9P0`–`0` | 0.1 µF |
| R5 | res | `V9P0`–`V4P5` | 100 kΩ |
| R6 | res | `V4P5`–`0` | 100 kΩ |
| C6 | cap | `V4P5`–`0` | 1 µF |
| V2 | source | `Vin`–`0` | input |
| R3 | res | `Vin`–`0` | 1 MΩ |
| C3 | cap | `n5`–`Vin` | 22 nF |
| R2 | res | `V4P5`–`n5` | 1 MΩ |
| R1 | res | `n2`–`n5` | 1 kΩ |
| C2 | cap | `n2`–`0` | 1 nF |
| U1 | opamp | IN−=`n1` IN+=`n2` OUT=`Vfb` V+=`V9P0` V−=`0` COMP=`n3`,`n4` | LM308 |
| C1 | cap | `n3`–`n4` | 30 pF |
| R9 | res | `Vfb`–`n1` | 100 kΩ log (**Distortion**) |
| C9 | cap | `n1`–`Vfb` | 100 pF |
| R7 | res | `n1`–`n6` | 47 Ω |
| C7 | cap | `n6`–`0` | 2.2 µF |
| R8 | res | `n1`–`n7` | 560 Ω |
| C8 | cap | `n7`–`0` | 4.7 µF |
| C10 | cap | `Vfb`–`n8` | 4.7 µF |
| R10 | res | `Vclip`–`n8` | 1 kΩ |
| D2 | diode | A=`0` K=`Vclip` | 1N914 |
| D3 | diode | A=`Vclip` K=`0` | 1N914 |
| R17 | res | `n11`–`Vclip` | 100 kΩ log (**Filter**) |
| R15 | res | `Vtone`–`n11` | 1.5 kΩ |
| C11 | cap | `Vtone`–`0` | 3.3 nF |
| C12 | cap | `n9`–`Vtone` | 22 nF |
| R12 | res | `n9`–`0` | 1 MΩ |
| J1 | njf | D=`V9P0` G=`n9` S=`n10` | 2N5485 |
| R13 | res | `n10`–`0` | 10 kΩ |
| C13 | cap | `Vout`–`n10` | 1 µF |
| R14 | res | `0`–`Vout` | 100 kΩ log (**Volume**) |

The schematic annotates J1 as "nominally 2N5458" in a text note while the symbol
value says 2N5485. Production RATs use a 2N5458.

## What makes the sound

**The gain stage is frequency dependent, and that is the RAT's voice.** It is a
non-inverting amplifier whose gain is `1 + R9 / Z`, where `Z` is the impedance
from `n1` to ground: R7 in series with C7, in parallel with R8 in series with C8.
Both legs are capacitively coupled, so the gain falls to unity at DC and climbs
with frequency — 560 Ω/4.7 µF opens first, then 47 Ω/2.2 µF adds a second, much
larger climb higher up. C9 across the feedback resistor rolls the top back off.
This is why a RAT sounds nothing like a Tube Screamer at the same gain setting:
the distortion is fed a signal that is already tilted.

**The clipping is asymmetric in time, not in level.** D2 and D3 are a matched
antiparallel pair to ground, so the static curve is symmetric. What is not
symmetric is the LM308 itself: it is a slow part, and with the 30 pF at C1 its
slew rate limits before its output reaches the rails on fast transients. A model
that clips the diodes but treats the op-amp as ideal gets the wrong pedal.

**The filter control is a series resistance, not a divider.** R17 runs in series
into the R15/C11 shunt, so turning it up moves the corner down. At the bright end
R15 alone sets the limit, around 32 kHz, which is to say no filtering at all.

**Everything after the filter is a buffer.** J1 is a self-biased source follower
into R13, and R14 is the volume control. The follower contributes a little
asymmetry and no gain.

## Where this diverges from the pedal

The volume pot R14 appears here as a rheostat to ground rather than a divider
with the wiper as the output, because the LTspice sheet only needed a load for
its level sweep. The pedal takes the output from the wiper. Model it as a plain
output gain.
