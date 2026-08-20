# Lovetone Big Cheese extracted netlist

Source: [`lovetone-big-cheese.asc`](https://github.com/Cushychicken/ltspice-guitar-pedals/tree/master/lovetone-big-cheese),
retrieved 2026-08-15.

Connectivity was resolved the same way as the wah and the RAT: union-find over
the wire graph, with symbol pins placed from LTspice's standard offsets. Run
`scripts/extract-ltspice-netlist.py` on the `.asc` to reproduce the table below.

**This source is incomplete, and the gaps matter.** Read the section on them
before using anything here. The checker reports all three.

## Nodes

| Node | Meaning |
| --- | --- |
| `Vin` (V2) | Input at the jack |
| `n3`, `n4` | Input network; `n4` is the buffer's non-inverting input |
| `n6` | Buffer output, tied back to its own inverting input |
| `n5` | Q1 base; the fuzz loop sums here |
| `n8` | Q1 collector, Q2 base |
| `n10` | Q1 emitter |
| `n7` | Q2 emitter; the Fuzz control sits below it |
| `n9` | Q2 collector, the fuzz output |
| `n11`, `n12` | Fuzz feedback and the Fuzz pot's lower leg |
| `n13` | After the coupling cap out of the fuzz |
| `n14` | Clipping node |
| `n24` | Between the two 47 k resistors below the clipper |
| `n25`, `n26` | The two ends of the tone stack |
| `n21` | Tone pot wiper |
| `n17` | Output stage input, biased to half the rail |
| `n18`, `n19`, `n20` | Output stage feedback network |
| `n22`, `n23` | After the output stage, before the volume pot |
| `n15`, `n16` | The trim leg. `n16` connects to nothing |
| `Va` | Supply rail after the protection diode |
| `Vout` | Output |
| `0` | Ground |

## Devices

| Device | Type | Nodes | Value |
| --- | --- | --- | --- |
| V1 | supply | `n1`–`0` | 9 V |
| D1 | diode | A=`n1` K=`Va` | 1N4148 (reverse-polarity protection) |
| C1, C2 | cap | `Va`–`0` | 22 µF each |
| V2 | source | `Vin`–`0` | input |
| R1 | res | `Vin`–`0` | 4.7 MΩ |
| C3 | cap | `n3`–`Vin` | 47 nF |
| R2 | res | `n4`–`n3` | 1 kΩ |
| C4 | cap | `n4`–`0` | 1.5 nF |
| R3 | res | `n4`–`Va` | 680 kΩ |
| R4 | res | `n4`–`0` | 680 kΩ |
| U3 | opamp | IN−=`n6` IN+=`n4` OUT=`n6` | AD712, one half. A voltage follower |
| C5 | cap | `n5`–`n6` | 47 nF |
| R5 | res | `n5`–`n7` | 100 kΩ |
| Q1 | npn | B=`n5` C=`n8` E=`n10` | 2N3904 |
| R6 | res | `Va`–`n8` | 100 kΩ |
| R8 | res | `n10`–`0` | 470 Ω |
| R9 | res | `n11`–`n10` | 470 kΩ |
| C7 | cap | `n9`–`n11` | 47 nF |
| Q2 | npn | B=`n8` C=`n9` E=`n7` | 2N3904 |
| C6 | cap | `n9`–`n8` | 47 pF |
| R7 | res | `Va`–`n9` | 10 kΩ |
| R10 | res | `n7`–`n12` | Fuzz pot, upper leg |
| R11 | res | `n12`–`0` | Fuzz pot, lower leg (1 kΩ track) |
| C9 | cap | `n12`–`0` | 4.7 µF |
| R14 | res | `n15`–`n12` | Trim pot |
| R13 | res | `n16`–`n15` | 1 kΩ. **`n16` is floating** |
| C8 | cap | `n13`–`n9` | 47 nF |
| R12 | res | `n14`–`n13` | 47 kΩ |
| Q3 | npn | B=`0` C=`n14` E=`n14` | 2N3904, collector tied to emitter |
| D2 | diode | A=`n14` K=`0` | 1N4148 |
| R22 | res | `n24`–`n14` | 47 kΩ |
| R23 | res | `0`–`n24` | 47 kΩ |
| C14 | cap | `n25`–`n14` | 2.2 nF |
| C15 | cap | `n14`–`n25` | 6.8 nF |
| R24 | res | `0`–`n25` | 47 kΩ |
| R25 | res | `n26`–`n14` | 47 kΩ |
| C16 | cap | `n26`–`0` | 10 nF |
| R26 | res | `n25`–`n21` | Tone pot, upper leg (100 kΩ track) |
| R27 | res | `n21`–`n26` | Tone pot, lower leg |
| C12 | cap | `n17`–`n21` | 47 nF |
| R15 | res | `n17`–`Va` | 680 kΩ |
| R16 | res | `n17`–`0` | 680 kΩ |
| C10 | cap | `n18`–`n19` | 100 pF |
| R17 | res | `n18`–`n19` | 33 kΩ |
| R18 | res | `n20`–`n19` | 15 kΩ |
| C11 | cap | `n20`–`0` | 100 nF |
| R19 | res | `n22`–`n18` | 470 Ω |
| C13 | cap | `n23`–`n22` | 4.7 µF |
| R20 | res | `n23`–`Vout` | Volume pot, upper leg (10 kΩ track) |
| R21 | res | `Vout`–`0` | Volume pot, lower leg |

## What the circuit is

An op-amp buffers the input and, at the far end, recovers gain. Between them
sits a two-transistor silicon fuzz of the Fuzz Face and Tone Bender family,
followed by hard diode clipping and a Big Muff style tone stack. That reading
comes out of the netlist and is corroborated by the published descriptions of
the pedal.

**The fuzz is a feedback pair, not two cascaded stages.** R5, 100 kΩ, runs from
Q2's emitter back to Q1's base, so the two transistors share one loop and one
bias point. The Fuzz control is the 1 kΩ pot below Q2's emitter: it sets how
much of that emitter is degenerated to ground through C9, which sets the loop
gain. This is why a Fuzz Face style circuit cleans up the way it does, and why
its bias and its gain are not independent.

**The clipping is asymmetric.** D2 conducts from `n14` to ground. Q3 has its
collector tied to its emitter with its base at ground, which puts its
base-emitter and base-collector junctions in parallel, both facing the other
way. So one silicon drop in one direction and two paralleled junctions in the
other, which do not clip at the same voltage. Symmetric-looking parts, an
asymmetric curve.

**The tone stack is a Big Muff.** A capacitive path from `n14` through C14 and
C15 to `n25`, a resistive path through R25 to `n26`, a pot between them and the
wiper as the output. That arrangement scoops the mids; where it scoops depends
on which caps are in circuit.

## Where this source is incomplete

**The output op-amp is missing.** The AD712 is a dual, and only one half is
drawn. The other half's supply wiring is there — two labelled stubs at
(4048, −192) and (4048, −32), exactly a pin's spacing from each other with
nothing between them — but the symbol itself is absent. Without it, `n17` is a
biased dead end and the R17/R18/C10/C11 network has nothing driving it. The
extraction script reports these as labelled stubs reaching no component pin.

The surrounding parts only make sense one way, and the reconstruction below is
what the model uses. `n17` is the non-inverting input. R17 in parallel with C10
is the feedback from the output at `n18`. R18 in series with C11 runs from the
inverting input at `n19` to ground. That gives unity gain at DC, rising to
1 + 33k/15k above the 106 Hz corner set by R18 and C11, with C10 rolling the
top off at 48 kHz. This matches the published description of the second op-amp
half as output gain recovery. It is a reconstruction and is labelled as one in
the code.

**R13 has a floating terminal.** `n16` connects to nothing. R13 and R14 are the
two legs of the Trim pot, and as drawn the leg carries no current, so the trim
does nothing at all in this schematic. A floating branch has no effect on the
solution, so the model omits it rather than guessing where it was meant to go.

**The voicing switch is not wired.** SW1 is a three-pole four-throw rotary and
is the pedal's headline control, but the drawing represents it as three plain
nodes plus three text notes. The notes are unreliable: all three name "PinB" in
their bodies, which is a copy-paste, and taken literally they make positions 1
and 2 identical and leave the third pole closed in every position.

What the four positions do is documented elsewhere: tone bypass, scooped mids,
flat mids, and a fourth "Cheese" setting that shifts the fuzz bias into gated
territory. The first three are tone stack states this netlist can express. The
fourth is a bias change whose value is nowhere in this source.
