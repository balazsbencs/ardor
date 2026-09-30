"""Audit the reviewed ground-track removal after refilling and running DRC."""
from pathlib import Path
import hashlib
import json
import math

import sexpdata as sx
from routing_widths import plans

ROOT = Path(__file__).resolve().parents[1]
board_path = ROOT / 'Ardor_IO.kicad_pcb'
board = sx.loads(board_path.read_text())
plan = json.loads((ROOT / 'design/ground-plane-routing.json').read_text())
_, _, width_changes, _ = plans()


def key(item):
    return str(item[0]) if isinstance(item, list) and item else ''


def get(item, name):
    return next((value for value in item if key(value) == name), None)


nets = {item[1]: item[2] for item in board if key(item) == 'net'}
tracks = [item for item in board if key(item) in ('segment', 'via')]
ground_segments = [item for item in tracks
                   if key(item) == 'segment' and nets[get(item, 'net')[1]] == 'GND']
ground_vias = [item for item in tracks
               if key(item) == 'via' and nets[get(item, 'net')[1]] == 'GND']
assert {get(item, 'uuid')[1] for item in ground_segments} == set(plan['retained_segments'])
assert {get(item, 'uuid')[1] for item in ground_vias} == set(plan['retained_ground_via_uuids'])
assert not {get(item, 'uuid')[1] for item in tracks} & set(plan['removed_segment_uuids'])
assert len(ground_segments) + len(plan['removed_segment_uuids']) == plan['before_segment_count']

# Net numbers and the generated mono-bridge UUIDs can change on regeneration.
# Reverse the separately audited width edits for comparison with the baseline.
# Everything else in every non-ground track and every via must remain identical.
other_routing = []
for item in tracks:
    name = nets[get(item, 'net')[1]]
    if key(item) == 'segment' and name == 'GND':
        continue
    values = [value for value in item if key(value) not in ('net', 'uuid')]
    uid = get(item, 'uuid')[1]
    if uid in width_changes:
        assert name == width_changes[uid]['net']
        assert get(item, 'width')[1] == width_changes[uid]['after_width_mm']
        values = [[value[0], width_changes[uid]['before_width_mm']]
                  if key(value) == 'width' else value for value in values]
    other_routing.append(sx.dumps(
        values
        + [[sx.Symbol('net_name'), name]]))
other_hash = hashlib.sha256('\n'.join(sorted(other_routing)).encode()).hexdigest()
assert other_hash == plan['unchanged_other_routing_sha256']

zones = [item for item in board if key(item) == 'zone' and not get(item, 'keepout')]
assert len(zones) == 2
assert {get(item, 'layer')[1] for item in zones} == {'F.Cu', 'B.Cu'}
assert all(get(item, 'net_name')[1] == 'GND' and get(item, 'filled_polygon') for item in zones)
drc = json.loads((ROOT / 'routing/drc.json').read_text())
assert not any(drc[name] for name in ('violations', 'unconnected_items', 'schematic_parity'))
remaining_length = sum(math.dist(get(item, 'start')[1:], get(item, 'end')[1:])
                       for item in ground_segments)
report = {
    'source_commit': plan['source_commit'],
    'before_ground_segments': plan['before_segment_count'],
    'after_ground_segments': len(ground_segments),
    'removed_ground_segments': len(plan['removed_segment_uuids']),
    'before_ground_track_length_mm': round(plan['before_length_mm'], 4),
    'after_ground_track_length_mm': round(remaining_length, 4),
    'removed_ground_track_length_mm': round(plan['before_length_mm'] - remaining_length, 4),
    'ground_vias_preserved': len(ground_vias),
    'all_other_tracks_and_vias_unchanged_except_reviewed_widths': True,
    'retained_local_connections': plan['retained_segments'],
    'filled_ground_layers': ['F.Cu', 'B.Cu'],
    'drc_violations': 0,
    'unconnected_items': 0,
    'schematic_parity_issues': 0,
    'drc_date': drc['date'],
    'outline_mm': [68, 46],
    'hardware_measured': False,
    'board_sha256': hashlib.sha256(board_path.read_bytes()).hexdigest(),
}
(ROOT / 'verification/ground-routing-audit.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
