# SMD audio coupling capacitors

C401, C402, C502, C601 and C602 use **Rubycon 16MU225KB23225**, replacing WIMA MKS2C042201K00KSSD. Each remains 2.2 µF ±10%, nonpolar film. The new voltage rating is 16 V, sufficient for the intended 5 V signal circuitry. The nominal audio high-pass response is unchanged.

The [manufacturer datasheet](https://www.rubycon.co.jp/wp-content/uploads/catalog-pmlcap/MU.pdf) specifies a 3.2 × 2.5 mm body, 1.8 mm nominal height and ±0.2 mm height tolerance. Compared with each old 7.2 × 7.2 mm body, nominal body area falls by **84.6%**. This is a component-body comparison; the overall PCB outline remains **90 × 64 mm**.

The part is stocked by an authorized distributor at the time of selection, 7 September 2026. It is a premium component: [DigiKey's listing](https://www.digikey.com/en/products/detail/rubycon/16MU225KB23225/9951728) showed USD 6.39 at quantity one and USD 4.694 at quantity ten, before applicable charges. Check current pricing and supply before procurement. No components have been ordered.

## Footprint and assembly

`Ardor_Capacitor:C_Rubycon_MU_3225` is included under `footprints/`. It uses Rubycon's [recommended reflow lands](https://www.rubycon.co.jp/wp-content/uploads/catalog/pml-spec3.pdf), not a generic ceramic 1210 footprint:

- Inner pad gap A: 1.8 mm; total outer span B: 3.6 mm; pad width C: 2.3 mm.
- Two rectangular 0.9 × 2.3 mm pads centered at x = ±1.35 mm.
- Courtyard: 4.1 × 3.3 mm, including maximum body tolerance and a 0.25 mm margin.
- No polarity marking; numbered pads preserve the schematic net assignments.

Use the manufacturer's moisture-handling and reflow instructions. Its soldering profile allows at most two reflow cycles, a 260°C maximum body-surface peak, and 30–60 seconds above 230°C. [Handling instructions](https://www.rubycon.co.jp/wp-content/uploads/catalog/pml-cautions.pdf) also restrict direct iron contact and reuse of removed capacitors. Confirm assembly-house compatibility.

## Layout and checks

The five smaller footprints are centered within the previous capacitor locations. Bottom-layer connections formerly made through capacitor leads now use routing vias. The +5V_PI trace that ran underneath the former C401 body has been rerouted around the new lands. Ground fills and printed connector/testpoint labels are retained.

The current schematic, BOM and schematic generator use the new part and footprint. Final results are in `drc.json`, `connectivity-audit.json`, `smd-capacitor-audit.json` and `../verification/erc.rpt`. The capacitor audit checks footprint geometry, physical SMD pad type, BOM/schematic agreement and unchanged net assignments. Hardware audio/ESD qualification remains as described in `../DESIGN_NOTES.md`.

`design/convert_film_smd.py` implements the legacy-footprint conversion. `design/route_smd_update.py` exports the local routing repair to `smd-update.dsn`; `smd-update.ses` is its local Freerouting result. Full placement regeneration through `compact_pcb.py` also uses the updated SMD positions.
