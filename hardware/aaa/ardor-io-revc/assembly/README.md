# Rev C — JLCPCB upload package

Select **2 assembled boards**, **top side only**, with SMT assembly. All **60 SMT placements** map to 24 exact parts: **16 Basic and 8 Extended**. Public JLCPCB stock checked **30 September 2026** covers the fitted quantities and published loss/minimum allowances. Stock is not reserved.

Upload:

1. [Ardor_IO_Gerbers.zip](Ardor_IO_Gerbers.zip): two-layer, 68 × 46 mm, 1.6 mm FR-4; separate plated/non-plated drills.
2. [JLCPCB_BOM.csv](JLCPCB_BOM.csv): 24 rows, exact JLCPCB codes. Keep one board's designator list; choose assembled quantity two in the order.
3. [JLCPCB_CPL.csv](JLCPCB_CPL.csv): 60 placements, millimetres, top side. Absolute KiCad origin matches the Gerbers; negative Y is intentional. Rotations are counterclockwise and match KiCad.

**Check JLCPCB's placement preview before ordering.** Its per-part zero-angle conventions have not been verified in a live order. Compare [assembly-top.pdf](assembly-top.pdf) and [polarity-reference.csv](polarity-reference.csv), especially the op-amps, ADC, MOSFET, polarized electrolytic and diodes. No guessed rotation corrections have been applied.

Choose bare-board quantity separately: if fabrication starts at five boards, request two assembled and three bare. [hand-assembly.csv](hand-assembly.csv) lists the nine through-hole components and three panel connectors per board, plus two expression-selector shunts. Headers, H11L1M and G5V-1 DC5 are fitted by hand. J101 stacking height and panel mechanics remain your enclosure selections. Audio jack sleeves bond the aluminium enclosure; the MIDI socket shell stays insulated.

[inventory.csv](inventory.csv) and [inventory-snapshot.json](inventory-snapshot.json) preserve all stock/source/timestamp details. Estimated charged quantity is `max(fitted + published loss, published minimum placements)`. Checkout can differ. [cost-comparison.json](cost-comparison.json) estimates a $43.98 reduction for two boards in components and Economic feeder fees relative to the full revision, excluding other order costs. The earlier $108 quote is not reproduced here.

[verification.json](verification.json) checks reference coverage, part identity, packages, capacitor ratings/dielectrics, resistor tolerances, stock allowances, CPL coordinates/rotations and zero-error ERC/DRC/parity. [Connectivity audit](../verification/connectivity-audit.json) separately checks all 219 PCB pad/net assignments. [package-sha256.txt](package-sha256.txt) identifies the CAD and upload artifacts.

Regenerate netlist/ERC/DRC and `kicad-smt-positions.csv` with KiCad 9, then run `design/export_jlcpcb_assembly.py`. It uses the saved inventory snapshot; it does not fetch stock. Refresh public stock before ordering later. These are prototype manufacturing files; audio measurements and assembled-enclosure qualification remain outstanding.
