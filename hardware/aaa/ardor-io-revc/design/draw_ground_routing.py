"""Draw actual ground routes before/after cleanup, with pours hidden for clarity."""
from pathlib import Path
import gzip
import json
import math
import sexpdata as sx

ROOT = Path(__file__).resolve().parents[1]
plan = json.loads((ROOT / 'design/ground-plane-routing.json').read_text())
old = sx.loads(gzip.decompress((ROOT / 'design/rev2-layout-baseline.kicad_pcb.gz').read_bytes()).decode())
new = sx.loads((ROOT / 'Ardor_IO.kicad_pcb').read_text())


def key(item):
    return str(item[0]) if isinstance(item, list) and item else ''


def get(item, name):
    return next((value for value in item if key(value) == name), None)


before_ids = set(plan['removed_segment_uuids']) | set(plan['retained_segments'])
before = [item for item in old if key(item) == 'segment' and get(item, 'uuid')[1] in before_ids]
after = [item for item in new if key(item) == 'segment' and get(item, 'uuid')[1] in plan['retained_segments']]
assert len(before) == 195 and len(after) == 3
nets = {item[1]: item[2] for item in new if key(item) == 'net'}
svg = ['<svg xmlns="http://www.w3.org/2000/svg" width="1280" height="620" viewBox="0 0 1280 620">',
       '<rect width="1280" height="620" fill="#ffffff"/>',
       '<g font-family="sans-serif" fill="#172b3a">',
       '<text x="40" y="38" font-size="25">Rev C ground routing</text>',
       '<text x="40" y="67" font-size="16">Copper pours hidden to show track removal. Both ground pours remain filled in the PCB.</text>']
for x, title, segments in [(40, 'Before: 195 segments / 328.78 mm', before),
                           (660, 'After: 3 segments / 4.65 mm', after)]:
    svg.append(f'<text x="{x}" y="108" font-size="21">{title}</text>')
    svg.append(f'<g transform="translate({x},130) scale(8) translate(-50,-50)">')
    svg.append('<rect x="50" y="50" width="68" height="46" fill="#f8fafc" stroke="#172b3a" stroke-width="0.15"/>')
    for track in new:
        if key(track) != 'segment' or nets[get(track, 'net')[1]] == 'GND':
            continue
        start, end = get(track, 'start')[1:], get(track, 'end')[1:]
        svg.append(f'<path d="M {start[0]} {start[1]} L {end[0]} {end[1]}" stroke="#c4cdd5" stroke-width="0.12" fill="none"/>')
    for footprint in new:
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
            diameter = min(get(pad, 'size')[1:3])
            svg.append(f'<circle cx="{cx}" cy="{cy}" r="{diameter / 2}" fill="#d9e0e6"/>')
    for track in segments:
        start, end = get(track, 'start')[1:], get(track, 'end')[1:]
        color = '#007d56' if get(track, 'layer')[1] == 'F.Cu' else '#2167bd'
        svg.append(f'<path d="M {start[0]} {start[1]} L {end[0]} {end[1]}" stroke="{color}" stroke-width="{get(track, "width")[1]}" stroke-linecap="round" fill="none"/>')
    for via in new:
        if key(via) == 'via' and nets[get(via, 'net')[1]] == 'GND':
            vx, vy = get(via, 'at')[1:]
            svg.append(f'<circle cx="{vx}" cy="{vy}" r="0.3" stroke="#172b3a" stroke-width="0.15" fill="#ffffff"/>')
    svg.append('</g>')
svg += ['<text x="40" y="540" font-size="17" fill="#007d56">Green: front GND tracks</text>',
        '<text x="360" y="540" font-size="17" fill="#2167bd">Blue: back GND tracks</text>',
        '<text x="660" y="540" font-size="17">Circles: 20 retained ground vias</text>',
        '<text x="40" y="577" font-size="16">68 × 46 mm outline and all component positions unchanged. Pads are simplified; use KiCad/Gerbers for fabrication.</text>',
        '</g></svg>']
(ROOT / 'routing/ground-routing.svg').write_text('\n'.join(svg) + '\n')
