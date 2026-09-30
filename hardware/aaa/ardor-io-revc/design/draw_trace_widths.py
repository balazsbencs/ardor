"""Draw the reviewed all-net width change, and render its per-net review table."""
from pathlib import Path
import json
import math
import sexpdata as sx

ROOT = Path(__file__).resolve().parents[1]
board = sx.loads((ROOT / 'Ardor_IO.kicad_pcb').read_text())
plan = json.loads((ROOT / 'design/trace-width-plan.json').read_text())
audit = json.loads((ROOT / 'verification/trace-width-audit.json').read_text())


def key(item):
    return str(item[0]) if isinstance(item, list) and item else ''


def get(item, name):
    return next((value for value in item if key(value) == name), None)


previous = {item['uuid']: item['before_width_mm'] for item in plan['changes']}
colors = {0.15: '#9b3095', 0.20: '#465e70', 0.25: '#ae6510', 0.40: '#00815d', 0.60: '#236dcc'}
svg = ['<svg xmlns="http://www.w3.org/2000/svg" width="1280" height="620" viewBox="0 0 1280 620">',
       '<rect width="1280" height="620" fill="#ffffff"/>',
       '<g font-family="sans-serif" fill="#172b3a">',
       '<text x="40" y="38" font-size="25">Rev C: complete trace-width audit</text>',
       '<text x="40" y="67" font-size="16">Both copper layers shown. Pours hidden so actual track widths are visible.</text>']
for x, before, title in [(40, True, 'Before: 7 mixed-width nets / 40 transitions'),
                         (660, False, 'After: CHASSIS only / 11 transitions')]:
    svg.append(f'<text x="{x}" y="108" font-size="20">{title}</text>')
    svg.append(f'<g transform="translate({x},130) scale(8) translate(-50,-50)">')
    svg.append('<rect x="50" y="50" width="68" height="46" fill="#f8fafc" stroke="#172b3a" stroke-width="0.15"/>')
    for footprint in board:
        if key(footprint) != 'footprint':
            continue
        pos = get(footprint, 'at')[1:]
        angle = math.radians(pos[2] if len(pos) > 2 else 0)
        for pad in footprint:
            if key(pad) != 'pad':
                continue
            px, py = get(pad, 'at')[1:3]
            cx = pos[0] + px * math.cos(angle) + py * math.sin(angle)
            cy = pos[1] - px * math.sin(angle) + py * math.cos(angle)
            radius = min(get(pad, 'size')[1:3]) / 2
            svg.append(f'<circle cx="{cx}" cy="{cy}" r="{radius}" fill="#d9e0e6"/>')
    for layer in ('B.Cu', 'F.Cu'):
        for track in board:
            if key(track) != 'segment' or get(track, 'layer')[1] != layer:
                continue
            uid = get(track, 'uuid')[1]
            width = previous.get(uid, get(track, 'width')[1]) if before else get(track, 'width')[1]
            start, end = get(track, 'start')[1:], get(track, 'end')[1:]
            svg.append(f'<path d="M {start[0]} {start[1]} L {end[0]} {end[1]}" stroke="{colors[width]}" stroke-width="{width}" stroke-linecap="round" fill="none"/>')
    for via in board:
        if key(via) == 'via':
            vx, vy = get(via, 'at')[1:]
            svg.append(f'<circle cx="{vx}" cy="{vy}" r="0.3" stroke="#172b3a" stroke-width="0.1" fill="#ffffff"/>')
    svg.append('</g>')
for i, (width, color) in enumerate(colors.items()):
    svg.append(f'<text x="{40 + 220 * i}" y="540" font-size="18" fill="{color}">{width:.2f} mm tracks</text>')
svg += ['<text x="40" y="577" font-size="16">467 segments / 41 nets / 51 vias. Uniform signal and normal power widths; wide CHASSIS ESD returns with constrained corridors.</text>',
        '</g></svg>']
(ROOT / 'routing/trace-widths.svg').write_text('\n'.join(svg) + '\n')

lines = ['# Rev C complete trace-width review', '',
         'The audit covers all **41 routed nets, 467 track segments and 51 vias**. Seven nets had mixed widths; the completed cleanup leaves only CHASSIS with mixed widths. The count of width-change junctions falls from **40 to 11**. See the [before/after view](trace-widths.svg) and [machine-readable audit](../verification/trace-width-audit.json).', '',
         'All ordinary signal, supply and relay routes now use **0.20 mm** throughout. The three short GND links use **0.40 mm**. CHASSIS uses **0.60 mm** where it fits and explicitly reviewed **0.20 mm** corridors where necessary. This cleanup changes 69 segment widths: 63 narrower, 6 wider (four analog-supply segments and two CHASSIS segments). It follows the earlier uniform 3.3 V change, whose 37 edits remain independently checked. Junction counts compare widths at shared same-net, same-layer segment endpoints; pad/via and filled-zone geometry are excluded.', '',
         '## Electrical assessment', '',
         '- The 5 V supply carries this IO board\'s loads, including the [nominal 30 mA relay coil](https://omronfs.omron.com/en_US/ecb/products/pdf/en-g5v_1.pdf). Screening it at the documented 100 mA board budget gives a conservative 23.28 mV trace drop, summing all branches; the actual source-to-load path is shorter.',
         '- The relay return is screened at 50 mA: 1.73 mV trace drop. The analog supply is screened at 10 mA: 1.51 mV. [TLV9002 quiescent current](https://www.ti.com/lit/ds/symlink/tlv9002.pdf) is 60 µA per channel typically, plus output loads. Audio, MIDI, expression, I²C and GPIO routes have small normal operating currents and no controlled-impedance requirement in this board.',
         '- Resistance estimates assume nominal 35 µm copper and room-temperature copper resistivity, sum all branches, and exclude component/via resistance. Screening currents are engineering checks rather than new circuit current ratings; startup, fault and assembled-enclosure behavior still require qualification.', '',
         '## CHASSIS exception', '',
         'CHASSIS is the return for TVS and RF-protection components. Its fast ESD pulses require a low-impedance return. [TI\'s ESD layout guide](https://www.ti.com/lit/an/slva680a/slva680a.pdf) explains the importance of minimizing TVS return inductance. Retaining broad copper where space permits is preferable to narrowing the entire return for visual consistency.', '',
         'A trial with all 45 CHASSIS segments at 0.60 mm produced 46 DRC findings, all involving CHASSIS: shorts, insufficient copper/hole clearance and corresponding solder-mask bridges. Twenty-four segments conflict with neighboring pads/tracks/vias. One additional segment stays at 0.20 mm to avoid inserting a short wide patch between the constrained ends of the JP301/R101 corridor. Two adjoining segments were safely widened to 0.60 mm. The final CHASSIS route has 20 wide and 25 constrained narrow segments. Their UUIDs, obstacles and reasons are recorded in [trace-width-plan.json](../design/trace-width-plan.json).', '',
         'Making CHASSIS uniformly wide requires rerouting its pad/via corridors. Its existing routing topology is retained in this cleanup; assembled-enclosure ESD performance is not claimed.', '',
         '## Validation', '',
         'KiCad 9.0.2, all-severity DRC with all-track and schematic-parity checks: **zero violations, zero unconnected items and zero parity findings**, with no rule relaxation or new exclusions. All 219 electrical pad/net assignments and 199 intended non-NC connections match. Retained route centerlines, copper layers, vias, footprint/pad geometry, outline and isolation keepouts match the frozen layout. Every width is checked against the two explicit plans and the CHASSIS exception list. The two mono-buffer bridges have stable UUIDs for reproducible regeneration.', '',
         '## Every routed net', '',
         '| Net | Segments | Widths before (mm) | Widths after (mm) | Changed segments |',
         '| --- | ---: | --- | --- | ---: |']
for name, row in audit['nets'].items():
    before = ' / '.join(f'{float(width):.2f}' for width in row['width_counts_before'])
    after = ' / '.join(f'{float(width):.2f}' for width in row['width_counts_after'])
    lines.append(f'| {name} | {row["segments"]} | {before} | {after} | {row["changed_segments"]} |')
(ROOT / 'routing/TRACE_WIDTH_REVIEW.md').write_text('\n'.join(lines) + '\n')
