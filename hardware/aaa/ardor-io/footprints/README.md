# Embedded package geometry

These four footprint definitions are snapshots of the package geometry already embedded in the supplied board. They preserve the original pad, thermal-via, courtyard and silkscreen geometry; no IC land pattern was resized to compact the board.

KiCad 9.0.2's installed libraries differ from the supplied board's package outlines. `fp-lib-table` resolves these three library namespaces locally, while keeping their existing schematic footprint identifiers. Other footprints use the standard KiCad 9 libraries.

U601 retains its original 0.20 mm thermal-via drills. Verify the selected fabricator supports these holes and the assembly process for the exposed pad.

`Ardor_Capacitor.pretty/C_Rubycon_MU_3225.kicad_mod` is a new manufacturer-specific footprint for the Rubycon SMD film replacements. It is built from Rubycon’s recommended reflow lands; see `../routing/SMD_CAPACITORS.md`.
