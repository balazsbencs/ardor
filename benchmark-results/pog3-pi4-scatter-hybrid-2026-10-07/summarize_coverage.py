#!/usr/bin/env python3
"""Audit modeled four-source/hybrid geometry; exclude instrumented CPU timing."""
import csv
from pathlib import Path
import sys

root = Path(sys.argv[1])
source = root / (sys.argv[2] if len(sys.argv) > 2 else 'coverage.log')
fields = ['workload', 'callback_frames', 'variant', 'regions', 'integer_sources',
          'four_batches', 'pair_batches', 'single_interior_sources', 'edge_sources',
          'modeled_interior_pairs', 'original_interior_pairs']
rows = []
for line in source.read_text().splitlines():
    if line.startswith('scatter_geometry,'):
        values = next(csv.reader([line]))[1:]
        assert len(values) == len(fields)
        rows.append(dict(zip(fields, [values[0], int(values[1]), values[2], *map(int, values[3:])])))
assert len(rows) == 12
indexed = {(r['workload'], r['callback_frames'], r['variant']): r for r in rows}
assert len(indexed) == len(rows)
for row in rows:
    assert row['variant'] in ('four', 'hybrid')
    interior = row['four_batches'] * 4 + row['pair_batches'] * 2 + row['single_interior_sources']
    assert row['original_interior_pairs'] == 24 * interior
    assert row['modeled_interior_pairs'] == 27 * row['four_batches'] + 25 * row['pair_batches'] + 24 * row['single_interior_sources']
    baseline = indexed[row['workload'], row['callback_frames'], 'four']
    assert baseline['pair_batches'] == 0
    for name in ('regions', 'integer_sources', 'four_batches', 'edge_sources', 'original_interior_pairs'):
        assert row[name] == baseline[name]
    assert baseline['single_interior_sources'] == row['single_interior_sources'] + row['pair_batches'] * 2
    assert baseline['modeled_interior_pairs'] - row['modeled_interior_pairs'] == row['pair_batches'] * 23
    other_callback = indexed[row['workload'], 192 - row['callback_frames'], row['variant']]
    for name in fields[3:]:
        assert row[name] == other_callback[name]
    if row['workload'] != 'matched_freeze_off':
        assert all(row[name] == 0 for name in fields[3:])
    row['batched_interior_percent'] = 100 * (interior - row['single_interior_sources']) / interior if interior else 0
    row['modeled_reduction_from_four_percent'] = 100 * (1 - row['modeled_interior_pairs'] / baseline['modeled_interior_pairs']) if interior else 0

fields += ['batched_interior_percent', 'modeled_reduction_from_four_percent']
prefix = 'host-' if source.name.startswith('host-') else ''
with (root / (prefix + 'coverage-summary.csv')).open('w') as output:
    writer = csv.DictWriter(output, fieldnames=fields, lineterminator='\n')
    writer.writeheader()
    for row in rows:
        writer.writerow({k: f'{v:.6f}' if isinstance(v, float) else v for k, v in row.items()})
        if row['workload'] == 'matched_freeze_off' and row['callback_frames'] == 64:
            print(f"{row['variant']}: four batches={row['four_batches']}, pairs={row['pair_batches']}, "
                  f"interior coverage={row['batched_interior_percent']:.3f}%, "
                  f"modeled reduction from four={row['modeled_reduction_from_four_percent']:.3f}%")
print('Same source accounting, four-source batches and callback geometry; savings equal 23 accesses per pair.')
