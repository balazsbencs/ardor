"""Check the frozen layout and every explicitly reviewed width-only change."""
from collections import Counter, defaultdict
from pathlib import Path
import gzip
import hashlib
import json
import math

import sexpdata as sx
from routing_widths import plans

ROOT = Path(__file__).resolve().parents[1]
old = sx.loads(gzip.decompress((ROOT / 'design/rev2-layout-baseline.kicad_pcb.gz').read_bytes()).decode())
board_path = ROOT / 'Ardor_IO.kicad_pcb'
new = sx.loads(board_path.read_text())
rail_plan, trace_plan, changes, exceptions = plans()


def key(item):
    return str(item[0]) if isinstance(item, list) and item else ''


def get(item, name):
    return next((value for value in item if key(value) == name), None)


def footprints(board):
    return {get(item, 'property')[2]: item for item in board if key(item) == 'footprint'}


def footprint_geometry(item):
    return ([v for v in item if key(v) not in ('property', 'path', 'sheetname', 'sheetfile', 'pad')]
            + [[v for v in pad if key(v) != 'net'] for pad in item if key(pad) == 'pad'])


def tracks(board):
    return {get(item, 'uuid')[1]: item for item in board if key(item) in ('segment', 'via')}


def zones(board):
    return {get(item, 'uuid')[1]: [v for v in item if key(v) not in ('net', 'filled_polygon', 'fill_segments')]
            for item in board if key(item) == 'zone'}


def junctions(items):
    ends = defaultdict(set)
    for item in items:
        for field in ('start', 'end'):
            ends[(get(item, 'net')[1], get(item, 'layer')[1], *get(item, field)[1:])].add(get(item, 'width')[1])
    return sum(len(widths) > 1 for widths in ends.values())


old_fps, new_fps = footprints(old), footprints(new)
assert all(footprint_geometry(old_fps[ref]) == footprint_geometry(item)
           for ref, item in new_fps.items())
a, b = tracks(old), tracks(new)
names = {item[1]: item[2] for item in new if key(item) == 'net'}
bridges = {item['uuid']: item for item in trace_plan['mono_bridge_segments']}
assert set(b) - set(a) == set(bridges), 'Unexpected added routing'
for uid, change in changes.items():
    assert uid in b and key(b[uid]) == 'segment'
    assert names[get(b[uid], 'net')[1]] == change['net']
    assert get(b[uid], 'width')[1] == change['after_width_mm']
    assert get(b[uid], 'layer')[1] == change['layer']
    assert all(get(b[uid], field)[1:] == change[field] for field in ('start', 'end'))
    if uid in a:
        assert get(a[uid], 'width')[1] == change['before_width_mm']
    else:
        assert change['before_width_mm'] == bridges[uid]['before_width_mm']
for uid, item in b.items():
    if uid in a:
        ignored = ('net', 'width') if uid in changes else ('net',)
        assert ([v for v in a[uid] if key(v) not in ignored]
                == [v for v in item if key(v) not in ignored])
    else:
        assert key(item) == 'segment' and names[get(item, 'net')[1]] == 'MONO_BUF'
        assert get(item, 'layer')[1] == bridges[uid]['layer']
        assert all(get(item, field)[1:] == bridges[uid][field] for field in ('start', 'end'))
assert [v for v in old if key(v) == 'gr_line'] == [v for v in new if key(v) == 'gr_line']
assert zones(old) == zones(new)
by_net = defaultdict(list)
for item in b.values():
    if key(item) == 'segment':
        by_net[names[get(item, 'net')[1]]].append(item)
assert set(by_net) == set(trace_plan['target_widths_mm'])
for name, items in by_net.items():
    for item in items:
        uid = get(item, 'uuid')[1]
        if uid in exceptions:
            assert name == exceptions[uid]['net'] == 'CHASSIS'
            expected = exceptions[uid]['width_mm']
        else:
            expected = trace_plan['target_widths_mm'][name]
        assert get(item, 'width')[1] == expected, (uid, name, expected)
assert set(exceptions) <= set(b)
board_hash = hashlib.sha256(board_path.read_bytes()).hexdigest()
report = {
    'all_retained_footprint_and_pad_geometry_unchanged': True,
    'all_retained_track_centerlines_and_via_geometry_unchanged': True,
    'track_widths_match_baseline_and_reviewed_plans': True,
    'reviewed_3v3_segments_narrowed': len(rail_plan['changes']),
    'other_reviewed_width_changes': len(trace_plan['changes']),
    'zone_outlines_settings_and_isolation_keepouts_unchanged': True,
    'outline_unchanged': True,
    'original_routing_items': len(a),
    'retained_routing_items': len(set(a) & set(b)),
    'new_buffer_bridge_segments': len(bridges),
    'removed_routing_items': len(set(a) - set(b)),
    'board_sha256': board_hash,
}
(ROOT / 'verification/geometry-audit.json').write_text(json.dumps(report, indent=2) + '\n')


def resistance(items):
    # Sum all branches for a conservative bound, assuming nominal 35 um copper.
    return sum(1.724e-8 * math.dist(get(t, 'start')[1:], get(t, 'end')[1:]) * 1e-3
               / (get(t, 'width')[1] * 1e-3 * 35e-6) for t in items)


rail_report = {'source_commit': rail_plan['source_commit'], 'target_width_mm': rail_plan['target_width_mm'],
               'changed_segments': len(rail_plan['changes']), 'route_centerlines_and_vias_unchanged': True,
               'rails': {}, 'board_sha256': board_hash}
for name in rail_plan['nets']:
    items = by_net[name]
    rail_report['rails'][name] = {
        'segments': len(items),
        'segments_narrowed': sum(c['net'] == name for c in rail_plan['changes']),
        'distinct_track_widths_mm': sorted({get(t, 'width')[1] for t in items}),
        'total_track_length_mm': round(sum(math.dist(get(t, 'start')[1:], get(t, 'end')[1:]) for t in items), 4),
        'assumed_copper_thickness_um': 35,
        'sum_all_branch_track_resistances_ohm': round(resistance(items), 5),
        'screening_current_ma': 10,
        'track_voltage_drop_upper_bound_at_screening_current_mv': round(resistance(items) * 10, 4),
        'vias_and_ferrite_excluded_from_resistance_estimate': True,
    }
(ROOT / 'verification/3v3-width-audit.json').write_text(json.dumps(rail_report, indent=2) + '\n')

# Reconstruct the width distribution immediately before this all-net cleanup.
previous_widths = {c['uuid']: c['before_width_mm'] for c in trace_plan['changes']}
all_segments = [t for t in b.values() if key(t) == 'segment']
assert not any(key(item) == 'arc' for item in new), 'Arc routing requires inclusion in this audit'
drc = json.loads((ROOT / 'routing/drc.json').read_text())
assert not any(drc[field] for field in ('violations', 'unconnected_items', 'schematic_parity'))
trace_report = {
    'source_commit': trace_plan['source_commit'], 'audited_nets': len(by_net),
    'audited_track_segments': len(all_segments), 'audited_vias': sum(key(t) == 'via' for t in b.values()),
    'segments_changed': len(trace_plan['changes']),
    'segments_narrowed': sum(c['after_width_mm'] < c['before_width_mm'] for c in trace_plan['changes']),
    'segments_widened': sum(c['after_width_mm'] > c['before_width_mm'] for c in trace_plan['changes']),
    'width_junctions_before': trace_plan['before_width_junctions'],
    'width_junctions_after': junctions(all_segments),
    'junction_count_method': 'Differing widths at shared same-net, same-layer segment endpoints; pad/via and filled-zone geometry excluded.',
    'constrained_chassis_segments': len(exceptions),
    'route_centerlines_layers_vias_and_placements_unchanged': True,
    'assumed_copper_thickness_um': 35, 'resistance_estimates_exclude_vias_and_components': True,
    'drc_violations': 0, 'unconnected_items': 0, 'schematic_parity_issues': 0,
    'drc_date': drc['date'], 'hardware_measured': False, 'nets': {}, 'board_sha256': board_hash,
}
for name, items in sorted(by_net.items()):
    before = Counter(previous_widths.get(get(t, 'uuid')[1], get(t, 'width')[1]) for t in items)
    after = Counter(get(t, 'width')[1] for t in items)
    current = 100 if name == '+5V_PI' else 50 if name == 'RELAY_LOW' else None if name in ('CHASSIS', 'GND') else 10
    trace_report['nets'][name] = {
        'segments': len(items), 'width_counts_before': dict(sorted(before.items())),
        'width_counts_after': dict(sorted(after.items())), 'width_junctions_after': junctions(items),
        'total_track_length_mm': round(sum(math.dist(get(t, 'start')[1:], get(t, 'end')[1:]) for t in items), 4),
        'changed_segments': sum(c['net'] == name for c in trace_plan['changes']),
        'screening_current_ma': current,
        'sum_all_branch_track_resistances_ohm': round(resistance(items), 5),
        'track_voltage_drop_upper_bound_at_screening_current_mv': round(resistance(items) * current, 4) if current is not None else None,
    }
trace_report['mixed_width_nets_before'] = sum(len(n['width_counts_before']) > 1 for n in trace_report['nets'].values())
trace_report['mixed_width_nets_after'] = sum(len(n['width_counts_after']) > 1 for n in trace_report['nets'].values())
assert trace_report['width_junctions_after'] == trace_plan['after_width_junctions']
assert [name for name, n in trace_report['nets'].items() if len(n['width_counts_after']) > 1] == ['CHASSIS']
(ROOT / 'verification/trace-width-audit.json').write_text(json.dumps(trace_report, indent=2) + '\n')
print('PASS:', trace_report['audited_nets'], 'nets,', len(all_segments), 'segments;',
      trace_report['width_junctions_before'], '->', trace_report['width_junctions_after'], 'width junctions;',
      'only CHASSIS retains reviewed width changes')
