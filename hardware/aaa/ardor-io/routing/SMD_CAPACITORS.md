# SMD audio coupling capacitors

C401, C402, C502, C601 and C602 use **Samsung CL31B106KBHNNNE**, **10 µF / 50 V / X7R / ±10%**, nonpolar ceramic in **1206**. JLCPCB part number **C89632** is included in the schematic, PCB fields and BOM for these five parts. Blank JLCPCB fields on other BOM rows mean those parts have not been mapped by this change.

## Selection and sourcing

The [Samsung specification](https://media.digikey.com/pdf/Data%20Sheets/Samsung%20PDFs/CL31B106KBHNNNE_Spec.pdf) gives a nominal body of 3.2 × 1.6 × 1.6 mm, with ±0.2 mm dimensional tolerance. The [JLCPCB listing](https://jlcpcb.com/partdetail/90812-CL31B106KBHNNNE/C89632), checked 8 September 2026, showed 359,276 in stock, 289,372 available to order, and a starting price of USD 0.3750 each. It is an Extended part supported for SMT assembly. These are sourcing observations, not reserved inventory or a confirmed assembly quotation; assembly charges are additional. No order was placed.

This replaces the earlier Rubycon 16MU225KB23225 film selection (C3774818), whose limited supplier/preorder availability was unsuitable for straightforward stocked assembly. The original design used WIMA 2.2 µF through-hole film capacitors. Nominal component body area is now 90.1% smaller than the original 7.2 × 7.2 mm WIMA body. The board remains 90 × 64 mm.

## Electrical implications

The change from 2.2 µF to 10 µF is deliberate. Increasing coupling capacitance reduces its audio-band impedance and the AC voltage across the ceramic; see [TI's audio capacitor guidance](https://www.ti.com/lit/an/slyt796a/slyt796a.pdf). It does not guarantee film-equivalent distortion or microphonic performance.

Using nominal capacitance and the existing input resistances, the approximate high-pass corners become 0.16 Hz at the 100 kΩ AUX inputs and 0.60 Hz at the headphone amplifier's typical 26.4 kΩ input resistance. The capacitor time constants increase by 10/2.2 ≈ 4.55, so prototype startup, mute and plug/unplug settling should also be checked. These input coupling capacitors do not drive the headphone load directly.

The circuit has approximately 2.5 V internal audio bias and 5 V supplies. The selected 50 V rating provides voltage headroom, but is not an external connector rating or a guarantee of effective capacitance. The exact DC-bias curve has not been verified; nominal 10 µF and ±10% tolerance do not establish capacitance under bias, temperature and aging. Prototype bass response, distortion and sensitivity to mechanical vibration remain release checks.

## Footprint and routing

The five parts use standard KiCad 9 `Capacitor_SMD:C_1206_3216Metric` reflow footprints: two 1.15 × 1.8 mm rounded pads, centers 2.95 mm apart, inner gap 1.8 mm and total outer span 4.1 mm. These replace the Rubycon-specific lands, not just the BOM entry. Component centers and orientations are preserved. Connected front-layer trace endpoints move to the new pad centers, and both ground pours are refilled. The existing layer-transition vias and C401 power crossover remain.

`design/convert_coupling_mlcc.py` performs this conversion idempotently on the earlier Rubycon board. The schematic generator and current BOM specify Samsung. `convert_film_smd.py` and `route_smd_update.py` describe the historical THT-to-film stage; they are not the current part-selection workflow.

## Verification

`drc.json` records the current KiCad DRC and schematic parity checks. `connectivity-audit.json` compares every numbered PCB pad/net against the schematic. `smd-capacitor-audit.json` checks all five Samsung part identities, JLCPCB numbers, values, pad geometry and unchanged net assignments. The schematic ERC report is in `../verification/erc.rpt`.
