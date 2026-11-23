# Dense MIDI carrier — 21 November 2026

The board is **39 × 35 mm** (1365 mm²), replacing 58 × 42 mm (2436 mm²): **44.0% less area**. It retains all 18 components, the connector pin assignments, two M2 mounting holes and the native **3 mm MIDI-loop-to-logic copper clearance** rule. This provides functional MIDI ground-loop isolation, not a certified safety barrier. Hole centres are (3, 32) and (36, 3) mm from the top-left corner; the previous mounting pattern does not fit.

Values and part types replace passive/semiconductor reference IDs on the front and assembly drawing. Connector IDs and all eight pin numbers remain, with complete back-side wiring maps. The 100n capacitors have labels in the gaps beside their own rows; other labels cannot occupy the optocoupler body. Some labels are covered after fitting parts. The 1N4148, SMC TVS and 100n labels use 0.8 mm text; other labels use 0.9 mm.

## Locally sourced components

**FB201/FB202: FERROCORE LCBB-601, [Hestore 100.327.86](https://www.hestore.hu/prod_10032786.html).** These replace axial Würth 7427501 parts. The [manufacturer family drawing supplied by TME](https://www.tme.eu/Document/b7dfa41fdd99fc196639e656c769ae20/lcb.pdf) specifies 600 Ω at 100 MHz, ±25%, 2 A maximum and 0.1 Ω maximum DCR. The 0805 body is nominally 2.0 × 1.25 × 0.9 mm, maximum 2.2 × 1.45 × 1.1 mm. Custom pads are 1.6 × 1.8 mm, at 2.3 mm centre spacing, exposing approximately 0.85 mm beyond each end of the maximum body for iron access. Use tweezers and flux; solder these before tall THT parts. Do not substitute a wound inductor solely by its inductance value.

The ferrites occupy the same two series connections in the MIDI current loop. At 5 mA, the pair's maximum DCR drop is 1 mV, versus 0.2 mV for the former pair. This is an RF-filter substitution, not an electrically identical drop-in: impedance changes from 800 Ω to 600 Ω at 100 MHz and its frequency curve differs. Long-cable operation and EMC/ESD behavior remain bench-test items. Order the specified LCBB-601; the supplier lists more than one manufacturer, so an unreviewed Yageo substitute is not automatically approved.

**U201: Everlight H11L1, [Hestore 100.435.31](https://www.hestore.hu/prod_10043531.html).** The [Everlight datasheet](https://www.hestore.hu/prod_getfile.php?id=14421) confirms the same pins (1 anode, 2 cathode, 3 NC, 4 output, 5 GND, 6 VCC), 3–15 V operating supply, 1.6 mA maximum turn-on threshold and 4 µs maximum turn-on/off times at the stated test conditions. The receiver keeps its 5 V supply and 1 kΩ pull-up to 3.3 V. Standard DIP-6 uses 7.62 mm row spacing and 2.54 mm pin pitch. Its maximum 6.8 × 7.42 mm body fits the existing 9.82 × 8.21 mm courtyard; maximum lead width is 0.6 mm for the 0.8 mm holes. Check notch/pin-1 orientation.

Everlight's **M** option has 10.16 mm wide leads; choose the standard H11L1 without that option. This suffix convention differs from onsemi H11L1M. A **6N138** has an eight-pin package, a different pinout and a Darlington output stage; it requires a circuit/footprint/timing review and is not a substitute for this board.

Sourcing evidence covers these three changed references and a one-board shopping quantity only. Other parts retain the existing family sourcing audit; its older stock figures were not refreshed. Stock, pricing and exact incoming manufacturer identity need checking when ordering. The draft sourcing validator remains BLOCKED because manufacturer lifecycle declarations and formal optocoupler substitution approval are not recorded; this blocks production qualification, not the CAD checks reported below. No order, purchase or production approval is implied.

## Saved-state checks

- KiCad 10.0.8: zero ERC violations, zero DRC violations at all severities, zero raw unconnected items, zero schematic parity issues and no DRC exclusions.
- All 42 physical numbered pads match the native schematic; pin/net partitions and unused-pin semantics match the previous board. Only ferrite value/package and optocoupler purchasing metadata change.
- All 20 embedded assembly courtyards, including mounting holes, are disjoint and at least 0.25 mm inside the outline.
- Two filled GND planes, all GND pads joined to host J101.3, no extra ground stitches and no vias. Tracks remain 0.2 mm for signals/supplies and 0.6 mm for chassis. The 3 mm isolation rule is unchanged and checks filled copper as well as tracks/pads.
- The fixed LED-cathode route avoids both the other diode terminal and the opposite optocoupler input. The router work area is inset 0.3 mm so its 0.2 mm clearance respects the PCB's 0.5 mm copper-edge rule; the real outline and fabrication rules are not relaxed.
- Fresh Gerber/drill/position exports and ZIP contents match this revision. The four manual SMD references are D203, D204, FB201 and FB202. Full family regeneration has not been rerun; its historical MIDI result is invalidated.

The top/bottom/isometric renders use generic KiCad bodies for standard parts and the ferrites. **The custom axial and SMC TVS bodies have no 3D models**; their complete maximum-body outlines and courtyards are reserved separately. Missing bodies in 3D are not empty board space. This remains an unbuilt engineering candidate: verify enclosure/socket height and connector access, then test real MIDI sources and long cables at 31250 baud.
