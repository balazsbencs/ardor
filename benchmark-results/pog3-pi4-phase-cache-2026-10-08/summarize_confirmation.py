#!/usr/bin/env python3
"""Summarize only this experiment's two identical-fixture ABBA rounds."""
import csv
from pathlib import Path
import statistics
import sys

root = Path(sys.argv[1])
rounds = (root, root / 'confirmation')
assert (rounds[0] / 'shared.wisdom').read_bytes() == (rounds[1] / 'shared.wisdom').read_bytes()
data = {}
for directory in rounds:
    for build in ('baseline', 'candidate'):
        for number in (1, 2):
            rows = list(csv.DictReader((directory / f'{build}-{number}.csv').open()))
            assert len(rows) == 6
            keys = {(r['workload'], r['callback_frames']) for r in rows}
            assert len(keys) == len(rows)
            for row in rows:
                assert int(row['callback_allocations']) == 0
                assert int(row['callbacks']) == (3000 if row['callback_frames'] == '64' else 1500)
                assert 0 <= int(row['over_budget_callbacks']) <= int(row['callbacks'])
                key = row['workload'], row['callback_frames']
                data.setdefault((build, key), []).append(row)
keys = sorted({key for build, key in data})
fields = ['build', 'workload', 'callback_frames', 'runs', 'mean_demand_min_percent',
          'mean_demand_max_percent', 'four_run_mean_us', 'p99_min_us', 'p99_max_us',
          'worst_us', 'over_budget_min_count', 'over_budget_max_count',
          'preparation_allocated_bytes']
with (root / 'combined-summary.csv').open('w') as output:
    writer = csv.DictWriter(output, fieldnames=fields, lineterminator='\n')
    writer.writeheader()
    for build in ('baseline', 'candidate'):
        for key in keys:
            rows = data[build, key]
            assert len(rows) == 4
            for field in ('budget_us',):
                assert len({row[field] for row in rows + data['candidate' if build == 'baseline' else 'baseline', key]}) == 1
            assert len({row['preparation_allocated_bytes'] for row in rows}) == 1
            other = data['candidate' if build == 'baseline' else 'baseline', key]
            delta = int(data['candidate', key][0]['preparation_allocated_bytes']) - int(data['baseline', key][0]['preparation_allocated_bytes'])
            assert delta == (16028 if key[0] == 'matched_freeze_off' else 0)
            assert len({row['max_transforms_per_callback'] for row in rows + other}) == 1
            demand = [100 * float(r['mean_us']) / float(r['budget_us']) for r in rows]
            over = [int(r['over_budget_callbacks']) for r in rows]
            values = [build, *key, len(rows), min(demand), max(demand),
                      statistics.mean(float(r['mean_us']) for r in rows),
                      min(float(r['p99_us']) for r in rows), max(float(r['p99_us']) for r in rows),
                      max(float(r['max_us']) for r in rows), min(over), max(over),
                      rows[0]['preparation_allocated_bytes']]
            writer.writerow({f: f'{v:.6f}' if isinstance(v, float) else v for f, v in zip(fields, values)})

with (root / 'combined-comparison.csv').open('w') as output:
    writer = csv.writer(output, lineterminator='\n')
    writer.writerow(['workload', 'callback_frames', 'baseline_four_run_mean_us',
                     'candidate_four_run_mean_us', 'candidate_reduction_percent'])
    for key in keys:
        baseline = statistics.mean(float(r['mean_us']) for r in data['baseline', key])
        candidate = statistics.mean(float(r['mean_us']) for r in data['candidate', key])
        reduction = 100 * (1 - candidate / baseline)
        writer.writerow([*key, f'{baseline:.6f}', f'{candidate:.6f}', f'{reduction:.6f}'])
        print(f'{key}: four-run mean reduction={reduction:.6f}%')
print('48 ordinary rows audited; identical wisdom, zero callback C++ allocations, stable FFT bounds across builds, stable +16028-byte core preparation increase; controls unchanged.')
