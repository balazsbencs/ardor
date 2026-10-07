#!/usr/bin/env python3
"""Audit modeled geometry; never treat instrumented timings as benchmark data."""
import csv
from pathlib import Path
import sys

root = Path(sys.argv[1])
fields = ['workload', 'callback_frames', 'width', 'regions', 'integer_sources',
          'batches', 'batch_sources', 'single_interior_sources', 'edge_sources',
          'modeled_interior_pairs', 'original_interior_pairs']
rows = []
for line in (root / 'coverage.log').read_text().splitlines():
    if line.startswith('scatter_geometry,'):
        values = next(csv.reader([line]))[1:]
        assert len(values) == len(fields)
        rows.append(dict(zip(fields, [values[0], *map(int, values[1:])])))
assert len(rows) == 12
indexed = {(r['workload'], r['callback_frames'], r['width']): r for r in rows}
assert len(indexed) == len(rows)
for row in rows:
    width = row['width']
    assert width in (2, 4)
    assert row['batch_sources'] == width * row['batches']
    interior = row['batch_sources'] + row['single_interior_sources']
    assert row['original_interior_pairs'] == 24 * interior
    assert row['modeled_interior_pairs'] == (24 + width - 1) * row['batches'] + 24 * row['single_interior_sources']
    peer = indexed[row['workload'], row['callback_frames'], 6 - width]
    for name in ('regions', 'integer_sources', 'edge_sources', 'original_interior_pairs'):
        assert row[name] == peer[name]
    other_callback = indexed[row['workload'], 192 - row['callback_frames'], width]
    for name in fields[2:]:
        assert row[name] == other_callback[name]
    if row['workload'] != 'matched_freeze_off':
        assert all(row[name] == 0 for name in fields[3:])
    row['batched_interior_percent'] = 100 * row['batch_sources'] / interior if interior else 0
    row['modeled_interior_pair_reduction_percent'] = 100 * (1 - row['modeled_interior_pairs'] / row['original_interior_pairs']) if interior else 0

fields += ['batched_interior_percent', 'modeled_interior_pair_reduction_percent']
with (root / 'coverage-summary.csv').open('w') as output:
    writer = csv.DictWriter(output, fieldnames=fields, lineterminator='\n')
    writer.writeheader()
    for row in rows:
        writer.writerow({k: f'{v:.6f}' if isinstance(v, float) else v for k, v in row.items()})
        if row['workload'] == 'matched_freeze_off' and row['callback_frames'] == 64:
            print(f"width={row['width']}: batches={row['batches']}, "
                  f"batched interior={row['batched_interior_percent']:.3f}%, "
                  f"modeled pair reduction={row['modeled_interior_pair_reduction_percent']:.3f}%")
print('Geometry agrees across callback sizes and accounts for the same source set at both widths.')
