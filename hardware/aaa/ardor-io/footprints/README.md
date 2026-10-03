# Embedded package geometry

These four footprint definitions are snapshots of the package geometry already embedded in the supplied board. They preserve the original pad, thermal-via, courtyard and silkscreen geometry; no IC land pattern was resized to compact the board.

KiCad 9.0.2's installed libraries differ from the supplied board's package outlines. `fp-lib-table` resolves these three library namespaces locally, while keeping their existing schematic footprint identifiers. Other footprints use the standard KiCad 9 libraries.

U601 retains its original 0.20 mm thermal-via drills. Verify the selected fabricator supports these holes and the assembly process for the exposed pad.

`Ardor_Capacitor.pretty/C_Rubycon_MU_3225.kicad_mod` is retained for the superseded Rubycon film conversion. The five coupling capacitors now use standard `Capacitor_SMD:C_1206_3216Metric` lands for Samsung CL31B106KBHNNNE; see `../routing/SMD_CAPACITORS.md`.

## C501: Panasonic FK size D

`Ardor_Capacitor.pretty/CP_Elec_Panasonic_FK_D6.3_H5.8.kicad_mod` is the manufacturer-specific footprint for EEEFK1C470P (47 µF / 16 V). Panasonic's [FK land drawing](https://industrial.panasonic.com/cdbs/www-data/pdf/RDE0000/ABA0000C1181.pdf), standard size D, specifies 3.2 × 1.6 mm rectangular pads separated by 1.8 mm (5.0 mm center pitch). Pin 1 is positive. Body height is 5.8 ±0.3 mm; the inherited 6.3 mm body outline/courtyard is retained. The old 5.4 mm-height 3D model is omitted so it cannot give a misleading stack-clearance result.
