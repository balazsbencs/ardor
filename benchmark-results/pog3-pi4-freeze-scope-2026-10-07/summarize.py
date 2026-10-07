#!/usr/bin/env python3
"""Regenerate matched-mode summaries without pooling older fixtures."""
from pathlib import Path
import csv
import statistics
import sys

root = Path(sys.argv[1])
data = {}
for label in ('enabled-1', 'disabled-1', 'disabled-2', 'enabled-2'):
    with (root / (label + '.csv')).open() as source:
        rows = list(csv.DictReader(source))
    assert len(rows) == (10 if label.startswith('enabled') else 6)
    assert all(int(row['callback_allocations']) == 0 for row in rows)
    assert all(0 <= int(row['over_budget_callbacks']) <= int(row['callbacks']) for row in rows)
    data[label] = {(row['workload'], row['callback_frames']): row for row in rows}
    assert len(data[label]) == len(rows)
for build in ('enabled', 'disabled'):
    assert set(data[build + '-1']) == set(data[build + '-2'])

fields = ['build', 'workload', 'callback_frames', 'mean_demand_min_percent', 'mean_demand_max_percent',
          'two_run_mean_us', 'p99_min_us', 'p99_max_us', 'worst_us', 'over_budget_min_count',
          'over_budget_max_count', 'over_budget_min_percent', 'over_budget_max_percent',
          'preparation_allocated_bytes']
with (root / 'summary.csv').open('w') as output:
    writer = csv.DictWriter(output, fieldnames=fields, lineterminator='\n')
    writer.writeheader()
    for build in ('disabled', 'enabled'):
        for key in sorted(data[build + '-1']):
            rows = [data[f'{build}-{i}'][key] for i in (1, 2)]
            demand = [100 * float(r['mean_us']) / float(r['budget_us']) for r in rows]
            over = [int(r['over_budget_callbacks']) for r in rows]
            over_percent = [100 * int(r['over_budget_callbacks']) / int(r['callbacks']) for r in rows]
            assert rows[0]['preparation_allocated_bytes'] == rows[1]['preparation_allocated_bytes']
            values = [build, *key, min(demand), max(demand), statistics.mean(float(r['mean_us']) for r in rows),
                      min(float(r['p99_us']) for r in rows), max(float(r['p99_us']) for r in rows),
                      max(float(r['max_us']) for r in rows), min(over), max(over), min(over_percent), max(over_percent),
                      rows[0]['preparation_allocated_bytes']]
            writer.writerow({field: f'{value:.6f}' if isinstance(value, float) else value
                             for field, value in zip(fields, values)})
            if key[0] not in ('matched_granular_control', 'matched_spectral_control'):
                print(f'{build} {key}: demand {min(demand):.2f}–{max(demand):.2f}%, '
                      f'p99 {max(float(r["p99_us"]) for r in rows):.3f}, '
                      f'worst {max(float(r["max_us"]) for r in rows):.3f}, over {min(over)}–{max(over)}')

with (root / 'comparison.csv').open('w') as output:
    writer = csv.writer(output, lineterminator='\n')
    writer.writerow(['workload', 'callback_frames', 'enabled_two_run_mean_us', 'disabled_two_run_mean_us',
                     'disabled_reduction_percent'])
    for key in sorted(data['disabled-1']):
        enabled = statistics.mean(float(data[f'enabled-{i}'][key]['mean_us']) for i in (1, 2))
        disabled = statistics.mean(float(data[f'disabled-{i}'][key]['mean_us']) for i in (1, 2))
        writer.writerow([*key, f'{enabled:.6f}', f'{disabled:.6f}', f'{100 * (1 - disabled / enabled):.6f}'])
