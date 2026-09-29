# JLCPCB assembly — two boards, SMT only

All **76 SMT placements per board** are mapped to **28 exact JLCPCB catalogue parts**. Live public JLCPCB part-detail data checked on **29 September 2026** shows enough available stock for two boards, including published assembly minimums and loss quantities. There are **13 Basic** and **15 Extended** part numbers. Stock is not reserved; no order has been placed.

## Upload files

1. **[Ardor_IO_Gerbers.zip](Ardor_IO_Gerbers.zip)** — two-layer copper, masks, silkscreen, paste, outline, and separate plated/non-plated drill files. Board: 68 × 46 mm, 1.6 mm thickness.
2. **[JLCPCB_BOM.csv](JLCPCB_BOM.csv)** — exact JLCPCB codes; 28 rows covering 76 references. Choose **2 assembled boards**, top side only. Do not double the designator list for two boards.
3. **[JLCPCB_CPL.csv](JLCPCB_CPL.csv)** — 76 placements, millimetres, positive rotation counterclockwise. Uses KiCad's absolute origin, matching the supplied Gerber/drill files; negative Y coordinates are intentional.

These files are ready for upload and component matching. **JLCPCB's placement preview still needs verification before ordering.** CPL rotations match KiCad; JLCPCB's per-part zero-angle conventions have not been verified in a live order. Compare [assembly-top.pdf](assembly-top.pdf) and [polarity-reference.csv](polarity-reference.csv), especially C501, Q501, U301/U401/U402/U501/U601 and the six unidirectional diodes. No unverified rotation offsets were guessed. See JLCPCB's [orientation guidance](https://github.com/JLCPCB/JLCPCB-SMT-Assembly-Components-orientation-fix).

Select bare-board quantity separately from assembled quantity: if fabrication starts at five boards, request two assembled and leave the remainder bare. JLCPCB describes assembly quantities starting at two in its [PCBA guide](https://jlcpcb.com/blog/build-first-custom-pcba).

## Inventory and selections

[inventory.csv](inventory.csv) lists every part, references, quantities, assembly loss/minimum, estimated charged quantity, stock, timestamp and direct JLCPCB link. [inventory-snapshot.json](inventory-snapshot.json) preserves factual fields from the live pages. Stock is JLCPCB's **available order quantity**, not LCSC stock or search snippets.

| Device | References | JLCPCB code | Available at check | Fitted on two boards |
| --- | --- | --- | ---: | ---: |
| TI OPA2320AIDR, SOIC-8 | U401, U402, U501 | [C2863402](https://jlcpcb.com/partdetail/C2863402) | 834 | 6 |
| TI TPA6132A2RTER, WQFN-16 | U601 | [C69901](https://jlcpcb.com/partdetail/C69901) | 277 | 2 |
| TI ADS1115IDGSR, VSSOP-10 | U301 | [C37593](https://jlcpcb.com/partdetail/C37593) | 28,369 | 2 |
| Panasonic EEEFK1C470P | C501 | [C336261](https://jlcpcb.com/partdetail/C336261) | 203 | 2 |
| Alpha & Omega AO3400A | Q501 | [C20917](https://jlcpcb.com/partdetail/C20917) | 891,037 | 2 |

Specified active parts, ferrites, protection diodes and the electrolytic retain their packages. Previously unspecified passives now have exact MPNs. Nominal capacitances/resistances are unchanged. The ten 100 nF bypass capacitors are now specified as 50 V X7R, C103/C403 as 10 µF / 16 V X7R, and C603/C605/C606/C607 as 2.2 µF / 25 V X7R; their earlier voltage requirements were lower. C202/C203 retain 100 pF / 1 kV C0G, R403/R404 retain 10 kΩ / 0.1%, and the five audio couplers retain Samsung C89632.

Estimated charged quantity is `max(fitted + published loss, published assembly minimum)`; checkout is authoritative. Private-library **pre-order purchase minimums are separate** and can be much higher. Do not pre-purchase thousands of resistors for this stocked two-board assembly. JLCPCB's [matching guidance](https://jlcpcb.com/help/article/common-bom-and-cpl-matching-issues-and-explanations) explains minimums and attrition. C501's listing flags an assembly fixture; fixture and Extended-part setup fees are determined in the quote.

## Hand assembly

[hand-assembly.csv](hand-assembly.csv) covers **10 through-hole components per board**, four panel connectors, and two JP301 shunts per board. J101/J102/J202/J302/J502/J503/J602/JP301, U201 and K501 are excluded from the SMT BOM/CPL. Nine bare test pads and four mounting-hole footprints require no placed component.

Manual parts are outside the SMT inventory claim. Retain H11L1M DIP-6 and Omron G5V-1 DC5. J101 mating/stack height and panel jack mechanics remain enclosure choices. Audio jack sleeves/bushings bond the aluminium enclosure; keep the MIDI DIN shell insulated and unconnected.

## Verification and regeneration

[verification.json](verification.json) checks MPN/code/package agreement across BOM, schematic and PCB; passive values/ratings/dielectrics/tolerances; complete SMT reference coverage; placement coordinates/angles; stock allowances; and ERC/DRC/parity results. All **276 pad/net assignments** still match. ERC, DRC, unconnected and parity counts are zero. This sourcing update changes no physical footprint geometry or routing.

`parts.json` is the selection input used by the schematic builder and synchronization/export scripts. With KiCad 9.0.2 Python and `sexpdata`, run from the project directory:

```sh
python3 design/apply_jlcpcb_parts.py
kicad-cli sch export netlist --format kicadxml -o review/fresh-netlist.xml Ardor_IO.kicad_sch
kicad-cli sch erc --severity-all --exit-code-violations --format json -o review/fresh-erc.json Ardor_IO.kicad_sch
kicad-cli pcb drc --schematic-parity --all-track-errors --severity-all --exit-code-violations --format json -o routing/drc.json Ardor_IO.kicad_pcb
kicad-cli pcb export pos --format csv --units mm --smd-only -o assembly/kicad-smt-positions.csv Ardor_IO.kicad_pcb
python3 design/export_jlcpcb_assembly.py
```

Do not use `--exclude-fp-th` for positions: U601 is SMT with through-hole thermal vias in its exposed-pad footprint. The exporter verifies its inclusion. Refresh inventory before a later order; exporting from the saved snapshot does not recheck live stock. `package-sha256.txt` identifies source CAD, BOM inputs and upload files. The assembled hardware still needs the original design's bench qualification.
