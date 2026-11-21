# Expression compact layout — 21 November 2026

The carrier is now **56 × 44 mm**, down from 72 × 50 mm: 2464 mm² instead of 3600 mm², a **31.6% area reduction**. This is a dense layout with the existing horizontal THT packages, not a proof of the mathematical minimum outline.

The schematic, netlist, BOM, component footprints and connector pin assignments are unchanged. The large TVS diodes and ferrite sit beside the complete ADC breakout envelope. Filtering, clamps and jumpers occupy the lower half. All axial parts remain flat and accessible from the solder side. The board retains two 2.2 mm M2 holes, at (3, 41) and (53, 3) mm from its top-left corner; the old mounting pattern no longer fits.

KiCad 9.0.2 checks on the final saved board:

- Zero DRC violations at all severities, zero unconnected items, zero schematic parity issues, no DRC exclusions.
- All 74 numbered pads match the unchanged schematic netlist; host and pedal interfaces match the original contract.
- All 31 embedded assembly courtyards (including mounting hardware and the complete ADC body) are disjoint and remain at least 0.25 mm inside the outline. The smallest courtyard-to-courtyard gap is 0.01 mm; courtyards already include the package clearance allowance.
- Two filled GND planes; all GND pads connect to the host ground. The ground-component check needed no additional stitching vias. The address jumper retains thermal relief, with diagonal spokes to avoid starving the connection.
- One 0.6/0.3 mm through-via; component drills remain 0.8, 0.9, 1.0 and 1.3 mm, plus the two 2.2 mm mounting holes. Signal/supply tracks remain 0.2 mm and chassis tracks 0.6 mm.
- All 29 components have front value/type labels or connector IDs, and all 20 connector/jumper pin numbers remain present. Each column of two BAT85S clamps shares one 0.8 mm label between the bodies; other labels use 0.9 mm text. Several labels are inside component outlines and disappear beneath fitted parts. Use the assembly drawing after population. Diode cathode bands remain visible in the drawings.
- Fresh front/back/assembly drawings and matching engineering Gerber/drill exports replace the previous geometry. Archive CRC, archive contents, drill origin and outline dimensions pass the package checks.

Top, bottom and isometric 3D views use the standard KiCad component models. **The custom ADS1115-M, TVS and ferrite bodies have no 3D models**; their dimensions are checked through the embedded courtyards and fabrication outlines instead. Do not interpret their absence in the 3D view as free space.

This remains an unbuilt engineering candidate. The enclosure, ten-pin socket height, module clearance and jumper access need physical confirmation. A new full-family regeneration comparison has not been run; the historical expression result is invalidated. No production-release or live supplier DFM approval is claimed.

Reproduce from `design/` using the updated `compact_layouts.py`, `build_pcbs.py`, frozen `expression/routing/board.ses`, and the existing finishing scripts. `expression/verification/design.json` records the same placements. The schematic generator is unchanged.

## Value/type labels

Component IDs on the front and assembly drawing are replaced by resistor/capacitor values and semiconductor/ferrite types. Internal references, schematic values and the BOM remain unchanged. Connector IDs still identify the complete back-side pin maps. `verification/value-labels.json` confirms that copper, filled zones, pads, footprints and board outline are unchanged by this label edit.
