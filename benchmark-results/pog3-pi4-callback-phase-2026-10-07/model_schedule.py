#!/usr/bin/env python3
"""Replay recorded long-job costs into bounded due-age layouts; not real timing."""
import csv
import gzip
from pathlib import Path
import statistics
import sys

root = Path(sys.argv[1])
wall = {}
jobs = []
for line in gzip.decompress((root / 'profile.log.gz').read_bytes()).decode().splitlines():
    row = next(csv.reader([line]))
    if len(row) < 3 or row[1:3] != ['matched_freeze_off', '64']:
        continue
    if row[0] == 'phase_callback': wall[int(row[3])] = float(row[5])
    if row[0] == 'phase_event' and row[3] == 'long_render':
        stage, sample, stamp, size, channel, voice, control, inclusive, exclusive = row[3:]
        jobs.append((int(sample), int(stamp), int(voice)*2+int(channel), int(inclusive)/1000))
assert set(wall) == set(range(3000)) and len(jobs) == 8988
base = [wall[i] for i in range(3000)]
residual = base.copy()
for sample, stamp, job, cost in jobs: residual[(sample-1)//64] -= cost
assert min(residual) >= 0

def due_ages(counts):
    result = []
    for bucket, count in enumerate(counts):
        first = 9 if bucket == 0 else bucket*64+1
        last = bucket*64+63
        for index in range(count):
            result.append(first + (last-first)*index//max(1,count-1))
    assert len(result)==12 and all(a<b for a,b in zip(result,result[1:]))
    assert result[0]>=9 and result[-1]<256
    return result

results = []
for first in range(13):
    for second in range(13-first):
        for third in range(13-first-second):
            counts = first, second, third, 12-first-second-third
            ages = due_ages(counts)
            times = residual.copy()
            for sample, stamp, job, cost in jobs: times[(stamp+ages[job]-1)//64] += cost
            assert abs(sum(times)-sum(base)) < .00001
            ordered = sorted(times)
            over = sum(t>64/.048 for t in times)
            results.append((over, ordered[int(.99*(len(times)-1))], max(times), counts, ages))
results.sort()
with (root/'schedule-model.csv').open('w') as output:
    writer=csv.writer(output,lineterminator='\n')
    writer.writerow(['rank','modeled_over_period_callbacks','modeled_p99_us','modeled_max_us','long_jobs_per_relative_64_interval','due_ages'])
    for rank,(over,p99,maximum,counts,ages) in enumerate(results,1):
        writer.writerow([rank,over,f'{p99:.6f}',f'{maximum:.6f}',' '.join(map(str,counts)),' '.join(map(str,ages))])
for result in results[:5]: print(result)
print(f'Original instrumented callbacks: over={sum(t>64/.048 for t in base)}, p99={sorted(base)[int(.99*(len(base)-1))]:.3f}, max={max(base):.3f}')
print('Model preserves recorded total work and long voice/channel order. Cache effects, changed job cost and instrumentation are unmodeled; validate a selected candidate on device.')
