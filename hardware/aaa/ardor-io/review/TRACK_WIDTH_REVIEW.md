# Mid-route track width review

Reviewed and corrected 29 September 2026.

## Cleanup completed

The current PCB includes the width cleanup and refilled ground pours. [Before/after close-ups](track-width-cleanup.png) show three representative changes; [verification](track-width-cleanup-verification.json) records the exact input and output board hashes.

- Widened 36 complete segments to match adjoining wider runs.
- Shortened the narrow portions of 22 longer segments, extending existing wide runs toward their clearance obstacles. These changes are limited to existing mid-route transitions; they do not add wide stubs to otherwise uniform routes.
- Width-change junctions decreased from **127 to 111**. Approximately **93.224 mm** of routing was widened, with no width reductions.
- Removed the right-headphone-output step and the tiny straight-run ground step illustrated below. The 8.51 mm ground neckdown is now 6.71 mm; the 8.46 mm ADC-supply neckdown is now 4.32 mm. Clearance along the remaining route prevents simply widening those entire segments.
- Full KiCad 9.0.2 DRC, all track errors, all severities and schematic parity: **zero violations, zero unconnected items, zero parity issues**. All 276 pad/net assignments still match the schematic.
- Every footprint, pad, via, board setting, zone definition and outline is unchanged. Route centerlines and total routing length are preserved. Splitting segments to localize neckdowns increases the segment count from 739 to 764; the via count remains 66.

Remaining width changes include constrained neckdowns, pad/via entries and branches. Further uniformity would require rerouting or reducing some wider sections.

The exact pre-cleanup board is archived as [width-baseline/Ardor_IO.kicad_pcb.gz](width-baseline/Ardor_IO.kicad_pcb.gz). The reviewed migration is [design/track-width-cleanup.json](../design/track-width-cleanup.json), applied by [apply_track_width_cleanup.py](../design/apply_track_width_cleanup.py). The script requires an exact input-board hash and writes a separate candidate; it is not a general routing regeneration stage. Run [verify_track_width_cleanup.py](../design/verify_track_width_cleanup.py) after a fresh DRC to compare the current board against the archived input and approved geometry changes.

## Original inspection, before cleanup

The width changes are real and many are consequences of segment-level widening and rollback, rather than deliberately positioned neckdowns. A clearance obstacle anywhere along a segment caused the entire segment to return to its old width. When both conflicting tracks were narrowed, the script did not revisit whether one could now be widened.

[Annotated examples](track-width-examples.png) · [Vector examples](track-width-examples.svg) · [All junctions and historical evidence](track-width-audit.json)

### Scope and counts before cleanup

The audit groups track endpoints by net, copper layer and exact coordinates, then identifies differing widths. It checks whether each junction point lies inside same-net pad/via copper, or has an additional same-net track centerline crossing it.

| Classification | Junctions |
| --- | ---: |
| All endpoint junctions with differing widths | 127 |
| Junction point inside pad or via copper | 36 |
| Branch junction outside pad/via copper | 13 |
| Nonbranching junction outside pad/via copper | 78 |
| Of those 78, more than 1 mm from any pad/via copper on that layer | 8 |
| Of those 78, between collinear segments forming a straight run | 3 |

“Outside pad/via copper” includes some junctions close to pads; it does not mean all 78 are in open board space. Each of the 78 has a narrower segment matching the saved pre-widening width and appearing in the saved widening-stage DRC report. Counts are junctions, not independent nets or faults. This endpoint audit does not enumerate width overlaps that occur solely inside another segment.

### Specific examples before cleanup

Coordinates are KiCad board coordinates in millimetres.

| Net / layer | Width-change point | Widths | Finding |
| --- | --- | --- | --- |
| GND / B.Cu | X 89.5573, Y 81.9108 | 0.40 / 0.20 mm | The 8.5126 mm diagonal segment was narrowed in full. The historical DRC identifies a headphone input via near the far end: widening leaves only 0.0858 mm copper clearance against the required 0.15 mm. A real obstacle exists, but it does not by itself justify starting the neckdown at this distant segment boundary. |
| +3V3_ADC / B.Cu | X 80.2106, Y 83.7503 | 0.40 / 0.20 mm | An 8.4583 mm segment was narrowed because of a PI_SDA via. Historical widened copper clearance is 0.0825 mm. The transition point is about 2.01 mm from the nearest pad/via copper. |
| HP_R / F.Cu | X 108.2592, Y 89.4408 | 0.30 / 0.20 mm | The 0.9631 mm narrow segment originally conflicted with widened CHASSIS tracks. Those were subsequently narrowed too. Against current copper geometry, widening this HP_R segment to 0.30 mm leaves approximately 0.3092 mm clearance, making it a strong candidate for removing an unnecessary step. |
| GND / F.Cu | X 70.4761, Y 84.2501 | 0.20 / 0.15 mm | A 0.0814 mm segment forms a width step along a straight run. The script tried 0.40 mm, encountered C301.1, and reverted to 0.15 mm. Matching the neighbouring 0.20 mm segment instead has an estimated 0.1992 mm copper clearance. |

### Original assessment and limits

These transitions were identified as routing quality issues. The evidence did not establish an electrical failure caused by a width step. Many narrow segments had genuine clearance constraints, which were retained in the completed cleanup.

A preliminary geometric check finds 13 of the 78 junctions where widening the adjacent narrow segment to the neighbouring width appears to meet 0.15 mm copper and 0.25 mm foreign-hole clearance individually. Some are almost exactly on the limit. This is a candidate list, not approval to apply all changes together.

The original estimates used buffered line segments and pad/via shapes and excluded filled zones. Historical evidence is from `routing/refine-drc.json` and `routing/widths-before.json`. The **pre-cleanup** PCB hash is recorded in `track-width-audit.json`; the original annotated examples above also show that pre-cleanup board. The completed cleanup was subsequently checked by KiCad after zone refill, as recorded at the top of this report. CAD checks do not replace the existing hardware qualification requirements.
