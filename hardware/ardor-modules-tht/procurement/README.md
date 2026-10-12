# Hestore purchasing audit — compact T1.1

Supplier product pages refreshed **12 October 2026** against all five current hand-assembly BOMs and accessory lists: **136 component positions and 22 accessory entries**, for one of each board. Exact source-file hashes are recorded in the snapshot. M1 SMD production BOMs are outside this audit.

- [Every component by board/reference](hestore-bom.csv)
- [Grouped component quantities](hestore-shopping-groups.csv)
- [Assembly accessories](hestore-accessories.csv)
- [Dated supplier facts and source-BOM hashes](supplier-snapshot.json)

Stock figures are displayed lower bounds, not reserved quantities. `Raktáron` means on hand; `3 munkanap` and `10–15 munkanap` are supplier lead times in workdays. `not listed` means no on-hand stock was published. Recheck before buying. “Not found” refers to catalog searches, not manufacturer discontinuation. Generic listings with missing identity/body dimensions remain conditional.

## Selected and implemented parts

T1.1 adopts the three agreed Hestore choices in its authoritative BOMs, compact layouts and manufacturing files:

| Part | Positions | Package / assembly |
|---|---|---|
| [ADS1115-M, 100.417.98](https://www.hestore.hu/prod_10041798.html) | Expression U301 | New carrier reserves the complete 27.5 × 17 mm module and maps its single ten-pin 2.54 mm row. Use one straight male header and [ZL262-10SG female socket](https://www.hestore.hu/prod_10028054.html). Onboard I²C pull-ups and ADDR pull-down remain. **Confirm the supplied ADC identity**: the product says ADS1115, but its schematic labels ADS1015. |
| [Nichicon UES1H100MPM, 100.524.69](https://www.hestore.hu/prod_10052469.html) | Nine bipolar coupling positions in mixer, line-out and headphones | 10 µF / 50 V bipolar, 3.5 mm pitch; nominal 8 × 11.5 mm can. Custom footprints reserve maximum 8.5 mm diameter; allow maximum 12.5 mm can height. Either orientation valid; nominal capacitance unchanged. Three-workday supplier stock is listed; no on-hand stock published at audit. |
| [Littelfuse SMDJ24CA, 100.461.56](https://www.hestore.hu/prod_10046156.html) | MIDI D203/D204 | Bidirectional large SMC/DO-214AB. Custom 3.3 × 2.5 mm hand-solder pads at 6.8 mm centre spacing. Retains 24 V standoff class; different capacitance/pulse behavior remains a bench-test item. Do not fit the low-voltage 1.5KE6.8CA here. |

The [module drawing](https://www.hestore.hu/prod_getfile.php?id=16090), [supplier schematic](https://www.hestore.hu/prod_getfile.php?id=13058), [Nichicon datasheet](https://www.hestore.hu/prod_getfile.php?id=24516) and [Littelfuse datasheet](https://www.hestore.hu/prod_getfile.php?id=16974) support these packages. See [sources and exact module pin translation](../SOURCES.md). CAD cannot establish which chip is fitted to a purchased module: commission with known inputs and conversion-timing checks. Adafruit and M2MINI do not fit the new carrier.

The exact [OPA1656ID](https://www.hestore.hu/prod_10043729.html), [MCP6022-I/P](https://www.hestore.hu/prod_10037299.html), [G5V-2-H1](https://www.hestore.hu/prod_10034426.html), [G5V-1](https://www.hestore.hu/prod_10041522.html) and [DC Components 1.5KE6.8CA](https://www.hestore.hu/prod_10043071.html) are listed on hand. Keep the H1 high-sensitivity headphone relay variant.

## Remaining gaps and conditional alternatives

| Requirement | Hestore candidate / limitation |
|---|---|
| Six axial Würth 7427501 ferrites, 800 Ω @ 100 MHz / 3 A / 20 mΩ | No verified THT equivalent found. [LCBB-601](https://www.hestore.hu/prod_10032786.html) is 0805 / 600 Ω / 2 A / 100 mΩ; larger [YI321611U101-4R0T](https://www.hestore.hu/prod_10043952.html) is 1206 / 100 Ω / 4 A. Neither matches package/RF characteristics. **Keep Würth from another supplier** to preserve THT. Adoption needs new SMD pads and per-net filtering/current review. |
| Headphone 100 pF / 50 V C0G, 2.5 mm-thick envelope | [CT40805N101J500F3R](https://www.hestore.hu/prod_10052526.html) is electrically suitable THT, but [maximum thickness is 3.8 mm](https://www.hestore.hu/prod_getfile.php?id=24624). Not approved for current footprint. A future adoption needs moving/reviewing the local feedback cluster. Source a confirmed thinner C0G part elsewhere for this PCB. |
| MIDI 100 pF / 1 kV C0G, 5 mm body | [CCH-100P/1000V](https://www.hestore.hu/prod_10044974.html) retains value/voltage but [uses SL dielectric, ±10%, maximum 7 mm diameter](https://www.hestore.hu/prod_getfile.php?id=15898). Requires package and RF/temperature/surge review. No exact match found; do not replace with a 50 V part. |
| Ordinary X7R bypass capacitors and small headphone C641 | [RM5](https://www.hestore.hu/prod_10044815.html) and [RM2.5](https://www.hestore.hu/prod_10023472.html) descriptions specify X7R/pitch but omit body drawings. Confirm dimensions, especially C641 ≤3.8 × 2.6 mm. Preserve its short supply track. [CC-100N](https://www.hestore.hu/prod_10025992.html) is Y5V, not the required X7R. |
| Four expression BAT85S clamps | [BAT85 listing](https://www.hestore.hu/prod_10022238.html) attaches a [Vishay BAT85S datasheet](https://www.hestore.hu/prod_getfile.php?id=785). Confirm supplied variant or review its clamp/leakage behavior. DO-35 fits; cathode band to pad 1. |
| Five resistor values lacking dimensions | All eleven nonzero values are stocked 1% metal film / 0.4 W. Six specify smaller 3.7 × 1.9 mm bodies. For 10 Ω, 33 Ω, 2.2 kΩ, 20 kΩ and 1 MΩ, confirm body ≤6.3 × 2.5 mm and compatible leads; do not use larger parts in compact placements. |
| M2 nonconductive hardware | No verified replacement found. [M2 brass](https://www.hestore.hu/prod_10043196.html) and [metal](https://www.hestore.hu/prod_10040560.html) do not meet insulation requirements. The [nylon kit](https://www.hestore.hu/prod_10051822.html) has M2.5/M3, incompatible with 2.2 mm board holes. Source M2 nylon elsewhere. |

These remaining candidates have not been adopted. Alternative links are review choices, not extra required quantities. The least disruptive purchasing route uses Hestore for selected/compatible parts, another supplier for exact axial ferrites, suitably small C0G capacitors and M2 nylon hardware, plus confirmation of generic passives and supplied module/clamp identities.

## Brand alternatives and accessories

[Semtech 2N3904](https://www.hestore.hu/prod_10041447.html) matches E1/B2/C3 in its [manufacturer drawing](https://www.hestore.hu/prod_getfile.php?id=14196); form leads to the existing wide pitch. Standard [Everlight H11L1](https://www.hestore.hu/prod_10043531.html) matches the onsemi functional pinout and supports the 5 V MIDI receiver's threshold/timing on paper per its [datasheet](https://www.hestore.hu/prod_getfile.php?id=14421). Everlight's M suffix means wider leads: use standard 7.62 mm DIP. [onsemi/Fairchild 1N4148](https://www.hestore.hu/prod_10023064.html) is a DO-35 alternative to the Vishay brand. Incoming identity and bench operation remain checks.

Selected polarized electrolytics retain value, meet/exceed voltage ratings and match published diameter/pitch. Observe polarity and check height. The small [10 kΩ / 0.4 W](https://www.hestore.hu/prod_10025387.html) fits upright headphone feedback footprints at 2.54 mm pitch; horizontal resistors use 10.16 mm pitch. R101 may be a formed wire link.

DIP sockets, shunts, headers and the new female ten-pin socket are stocked. Header quantities count **cut segments**, not whole 40-pin strips; include module accessories when summing strip requirements. Check the complete module/socket stack height and support the module during insertion/removal.

Panel-socket options and hookup wire are listed in the accessories CSV. Confirm enclosure fit and contact identification; leave unused switch contacts open. The [DIN5P180](https://www.hestore.hu/prod_10043421.html) listing does not establish insulated-shell mounting. **Wire only DIN contacts 4/5; leave DIN 1/2/3 and shell unwired. Bond the enclosure to J203.1**, following the MIDI guide. This corrects the earlier audit's mistaken DIN pin-2/shell bonding note.

Exact 32 Ω / ≥0.5 W dummy loads were not found. [33 Ω / 1% / 0.6 W](https://www.hestore.hu/prod_10020482.html) parts are practical external preliminary loads for this circuit; calculate at 33 Ω and retain actual 32 Ω/cable testing. These are test accessories, not PCB components.
