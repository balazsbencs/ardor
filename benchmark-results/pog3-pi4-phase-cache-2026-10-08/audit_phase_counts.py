#!/usr/bin/env python3
"""Audit actual arg calls against independently shadowed previous-frame reuse."""
import csv
from pathlib import Path
import sys
root = Path(sys.argv[1])
a = list(csv.DictReader((root / 'profile-baseline-frame-counts.csv').open()))
b = list(csv.DictReader((root / 'profile-candidate-frame-counts.csv').open()))
assert len(a) == len(b) == 5247
for old, new in zip(a, b):
    for field in old:
        if field != 'previous_args':
            assert old[field] == new[field], (field, old, new)
    phase, reuse = int(old['phase']), int(old['phase_reuse'])
    assert int(old['current_args']) == int(new['current_args']) == phase
    assert int(old['previous_args']) == phase
    assert int(new['previous_args']) == phase - reuse
phase = sum(int(r['phase']) for r in a)
reuse = sum(int(r['phase_reuse']) for r in a)
assert (phase, reuse) == (844543, 533169)
print(f'frames={len(a)} phase_events={phase} actual_previous_args={phase}->{phase-reuse}')
print(f'total_arg_calls={2*phase}->{2*phase-reuse} reduction_percent={100*reuse/(2*phase):.6f}')
print('Original peak/track/birth/history/region/snapshot counts and both callback partitions match. Diagnostic timings excluded from ordinary retention decision.')
