# Rev C fabrication release checklist

Do not upload this revision to JLCPCB until every item is closed.

## Mechanical inputs required

- [ ] Approved board outline and maximum dimensions.
- [ ] Raspberry Pi model(s), 40-pin header height, mounting-hole coordinates,
      underside/component keep-outs, and cooling clearance.
- [ ] Enclosure drawing and exact edge/face locations for J1/J4/J5/J6/J7/J9.
- [ ] Confirm whether panel jacks remain on wire harnesses or move onto the PCB.
- [ ] Maximum top and bottom component height and service access to SW1.

## Electrical/layout release

- [ ] Lock exact orderable MPN and LCSC number for every fitted part; mark J2,
      J3, J10, and R31 DNP in both BOM and placement data.
- [ ] Recheck every custom footprint against the current manufacturer drawing,
      especially U6 WLCSP, U8 oscillator, K1/K2, SW1, and the 2x20 header.
- [ ] Place and route a four-layer PCB with a continuous ground reference plane.
- [ ] Review codec capacitor placement, WLCSP fanout, clock return paths, analogue
      partitioning, relay current loops, chassis/ESD paths, and MIDI isolation gap.
- [ ] Run KiCad ERC and DRC with JLC Standard-process rules and resolve all items.
- [ ] Independently compare schematic netlist, PCB netlist, BOM, and centroid data.
- [ ] Generate Gerbers, Excellon drills, fabrication drawing, assembly drawings,
      paste layers, JLC BOM, and top/bottom CPL; inspect every rendered layer.

## JLC order notes

- [ ] Standard PCBA, 4 layers, ENIG, controlled stack-up, electropolished stencil.
- [ ] U6: 34-ball 0.5 mm WLCSP; request X-ray and first-article inspection photos.
- [ ] No substitutions for U6/U7/U8 or DA7212 reference/charge-pump capacitors.
- [ ] Confirm polarity/orientation for U6, U8, diodes, TVS parts, U4, and electrolytics.

## First article

- [ ] Power without Raspberry Pi; verify shorts and rail resistance first.
- [ ] Current-limited power: verify 5 V, 4.5 V, 3.3 V, 1.8 V, and 2.25 V reference.
- [ ] Verify 12.288 MHz MCLK, I2C ACK at `0x1a`/`0x48`, and I2S clocks before unmute.
- [ ] Characterize noise floor, THD+N, frequency response, input clipping, output
      level, relay-pop timing, MIDI polarity, expression range, and GPIO boot states.
- [ ] Perform cable hot-plug, short-circuit, ESD, radiated/conducted immunity, thermal,
      brownout, shutdown, and ground-loop tests in the final metal enclosure.
