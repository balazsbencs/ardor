#!/usr/bin/env python3
"""Audit actual diagnostic work counts; never infer CPU gains from timers."""
from pathlib import Path
import csv
import gzip
import struct
import sys

root = Path(sys.argv[1])
records = {}
counts = {}
for build in ('baseline', 'candidate'):
    samples = {64: [], 128: []}
    gains = {64: [], 128: []}
    for line in gzip.decompress((root / f'counts-{build}.log.gz').read_bytes()).decode().splitlines():
        row = next(csv.reader([line]))
        if row and row[0] == 'attack_counts':
            assert len(row) == 13 and row[1] == 'matched_freeze_off'
            samples[int(row[2])].append(tuple(map(int, row[3:])))
        elif row and row[0] == 'attack_gain_counts':
            assert len(row) == 10 and row[1] == 'matched_freeze_off'
            gains[int(row[2])].append(tuple(map(int, row[3:])))
    assert samples[64] == samples[128] and len(samples[64]) == 2622
    assert gains[64] == gains[128] and len(gains[64]) == 2622
    for metadata, work in zip(samples[64], gains[64]):
        assert metadata[:4] == work[:4]
        sample, stamp, resolution, control, calls, ramps, replacements = work
        seconds = struct.unpack('<f', struct.pack('<I', control))[0]
        assert seconds > 0  # This matched-core fixture uses active Attack.
        assert 0 <= ramps <= 4 * calls
        assert 0 <= replacements <= metadata[6]
        if resolution == 2:
            assert replacements == 0
        assert 0 <= calls <= metadata[6] * (2 if build == 'baseline' else 1)
    records[build] = samples[64]
    counts[build] = gains[64]
assert records['baseline'] == records['candidate']
for baseline, candidate in zip(counts['baseline'], counts['candidate']):
    assert baseline[:4] == candidate[:4]
    assert baseline[6] == candidate[6]
    assert baseline[4] - candidate[4] == baseline[6]
    assert baseline[5] >= candidate[5]

with (root / 'gain-count-summary.csv').open('w') as output:
    writer = csv.writer(output, lineterminator='\n')
    writer.writerow(['resolution', 'updates', 'baseline_gain_calls', 'candidate_gain_calls',
                     'canonical_replacements', 'baseline_sine_ramps', 'candidate_sine_ramps',
                     'avoided_sine_ramps', 'gain_call_reduction_percent', 'sine_ramp_reduction_percent'])
    for resolution in (0, 1, 2):
        before = [r for r in counts['baseline'] if r[2] == resolution]
        after = [r for r in counts['candidate'] if r[2] == resolution]
        calls_before, calls_after = sum(r[4] for r in before), sum(r[4] for r in after)
        ramps_before, ramps_after = sum(r[5] for r in before), sum(r[5] for r in after)
        replacements = sum(r[6] for r in before)
        writer.writerow([resolution, len(before), calls_before, calls_after, replacements,
                         ramps_before, ramps_after, ramps_before-ramps_after,
                         100 * (1-calls_after/calls_before), 100 * (1-ramps_after/ramps_before)])
    before, after = counts['baseline'], counts['candidate']
    calls_before, calls_after = sum(r[4] for r in before), sum(r[4] for r in after)
    ramps_before, ramps_after = sum(r[5] for r in before), sum(r[5] for r in after)
    writer.writerow(['all', len(before), calls_before, calls_after, sum(r[6] for r in before),
                     ramps_before, ramps_after, ramps_before-ramps_after,
                     100 * (1-calls_after/calls_before), 100 * (1-ramps_after/ramps_before)])
print('2622 ordered Attack updates match both partitions and both builds; every omitted gain equals one canonical replacement. Sine-ramp counts are actual branches, not a timing estimate.')
