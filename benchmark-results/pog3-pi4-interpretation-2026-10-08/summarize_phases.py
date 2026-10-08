#!/usr/bin/env python3
"""Audit diagnostic events and summarize callback phases without claiming gains."""
from collections import Counter, defaultdict
import csv
import gzip
from pathlib import Path
import statistics
import sys

root = Path(sys.argv[1])
prefix = sys.argv[2] if len(sys.argv) > 2 else 'profile'
path = root / (prefix + '.log.gz')
source = gzip.decompress(path.read_bytes()).decode() if path.exists() else (root / (prefix + '.log')).read_text()
callbacks, stages, events, receipts = {}, {}, defaultdict(list), {}
for line in source.splitlines():
    row = next(csv.reader([line]))
    if not row:
        continue
    if row[0] == 'phase_callback':
        assert len(row) == 8
        _, workload, frames, block, phase, wall, over, exclusive = row
        key = workload, int(frames), int(block)
        assert key not in callbacks
        callbacks[key] = (int(phase), float(wall), int(over), int(exclusive))
    elif row[0] == 'phase_stage':
        assert len(row) == 9
        _, workload, frames, block, phase, stage, calls, inclusive, exclusive = row
        key = workload, int(frames), int(block), stage
        assert key not in stages
        stages[key] = (int(phase), int(calls), int(inclusive), int(exclusive))
    elif row[0] == 'phase_event':
        assert len(row) == 12
        _, workload, frames, stage, *numbers = row
        events[workload, int(frames)].append((stage, *map(int, numbers)))
    elif row[0] == 'phase_receipt':
        assert len(row) == 5 and row[4] == 'overflow=0'
        key = row[1], int(row[2])
        assert key not in receipts
        receipts[key] = int(row[3])

by_callback = defaultdict(dict)
for (workload, frames, block, stage), value in stages.items():
    by_callback[workload, frames, block][stage] = value
phase_counts = Counter((workload, frames, value[0]) for (workload, frames, block), value in callbacks.items())

csv_rows = list(csv.DictReader((root / (prefix + '.csv')).open()))
assert len(csv_rows) == 6 and len(receipts) == 6
core = 'matched_freeze_off'
for row in csv_rows:
    workload, frames = row['workload'], int(row['callback_frames'])
    selected = {block: value for (name, size, block), value in callbacks.items() if (name, size) == (workload, frames)}
    assert set(selected) == set(range(int(row['callbacks'])))
    assert int(row['callback_allocations']) == 0
    assert sum(v[2] for v in selected.values()) == int(row['over_budget_callbacks'])
    assert abs(statistics.mean(v[1] for v in selected.values()) - float(row['mean_us'])) < .02
    assert abs(max(v[1] for v in selected.values()) - float(row['max_us'])) < .02
    assert sum(v[1] for key, v in stages.items() if key[:2] == (workload, frames)) == receipts[workload, frames]
    fft_counts = []
    for block, value in selected.items():
        phase, wall, over, exclusive = value
        assert phase == ((block + 1) * frames) % 512 and over in (0, 1)
        members = list(by_callback[workload, frames, block].values())
        assert all(v[0] == phase and 0 <= v[3] <= v[2] for v in members)
        assert sum(v[3] for v in members) == exclusive
        assert exclusive / 1000 <= wall + .02
        fft_counts.append(sum(v[1] for stage, v in by_callback[workload, frames, block].items() if stage in ('forward_fft', 'inverse_fft')))
    assert max(fft_counts) == int(row['max_transforms_per_callback'])

for frames in (64, 128):
    totals = defaultdict(lambda: [0, 0, 0])
    render_keys, inverse_keys = Counter(), Counter()
    for stage, sample, stamp, size, channel, voice, control, inclusive, exclusive in events[core, frames]:
        assert 0 < sample <= 192000 and 0 <= stamp <= sample and 0 <= exclusive <= inclusive
        block = (sample - 1) // frames
        total = totals[block, stage]
        total[0] += 1; total[1] += inclusive; total[2] += exclusive
        age = sample - stamp
        if stage in ('long_render', 'short_render'):
            assert channel in (0, 1)
            if stage == 'long_render':
                assert size == 2048 and 0 <= voice < 6 and stamp % 256 == 0
                assert age == (65, 85, 106, 127, 129, 141, 153, 166, 178, 191, 193, 241)[voice * 2 + channel]
            else:
                assert size == 1024 and voice in (4, 5) and stamp % 128 == 0
                assert age == 65 + ((voice - 4) * 2 + channel) * 16
            render_keys[sample, stamp, size, channel, voice] += 1
        elif stage == 'inverse_fft':
            inverse_keys[sample, stamp, size, channel, voice] += 1
        elif stage in ('forward_fft', 'interpretation') or stage.startswith('frame_'):
            assert size in (1024, 2048, 4096) and channel in (0, 1)
            assert stamp % (size // 8) == 0 and age == channel
        elif stage.endswith('_attack'):
            assert channel == -2 and voice == -1 and control != 0
            assert age == (8 if stage == 'short_attack' else 1)
            assert size == {'short_attack': 1024, 'long_attack': 2048, 'low_attack': 4096}[stage]
            assert stamp % (size // 8) == 0
        else:
            raise AssertionError(stage)
    assert len(events[core, frames]) == receipts[core, frames] == 74566
    assert render_keys == inverse_keys
    for (block, stage), total in totals.items():
        assert tuple(total) == stages[core, frames, block, stage][1:]
tags = lambda frames: [e[:-2] for e in events[core, frames]]
assert tags(64) == tags(128), 'event metadata changes with callback partition'

def percentile(values, fraction):
    values = sorted(values)
    return values[int(fraction * (len(values) - 1))]

with (root / (prefix + '-phases.csv')).open('w') as output:
    fields = ['workload', 'callback_frames', 'end_mod_512', 'callbacks', 'mean_us', 'median_us', 'p99_us', 'max_us', 'over_budget_callbacks', 'stage_exclusive_mean_us', 'residual_mean_us']
    writer = csv.writer(output, lineterminator='\n'); writer.writerow(fields)
    grouped = defaultdict(list)
    for (workload, frames, block), value in callbacks.items(): grouped[workload, frames, value[0]].append(value)
    for (workload, frames, phase), values in sorted(grouped.items()):
        walls = [v[1] for v in values]
        mean = statistics.mean(walls)
        exclusive = statistics.mean(v[3] / 1000 for v in values)
        writer.writerow([workload, frames, phase, len(values), f'{mean:.6f}', f'{percentile(walls, .5):.6f}', f'{percentile(walls, .99):.6f}', f'{max(walls):.6f}', sum(v[2] for v in values), f'{exclusive:.6f}', f'{mean-exclusive:.6f}'])
        if workload == core and frames == 64: print(f'phase {phase:3}: mean={mean:.3f}, p99={percentile(walls,.99):.3f}, max={max(walls):.3f}, over={sum(v[2] for v in values)}/{len(values)}')

with (root / (prefix + '-stage-phases.csv')).open('w') as output:
    writer = csv.writer(output, lineterminator='\n')
    writer.writerow(['workload', 'callback_frames', 'end_mod_512', 'stage', 'calls', 'inclusive_mean_us', 'exclusive_mean_us'])
    grouped = defaultdict(lambda: [0, 0, 0])
    for (workload, frames, block, stage), (phase, calls, inclusive, exclusive) in stages.items():
        total = grouped[workload, frames, phase, stage]
        total[0] += calls; total[1] += inclusive; total[2] += exclusive
    for (workload, frames, phase, stage), total in sorted(grouped.items()):
        number = phase_counts[workload, frames, phase]
        writer.writerow([workload, frames, phase, stage, total[0], f'{total[1]/number/1000:.6f}', f'{total[2]/number/1000:.6f}'])

with (root / (prefix + '-jobs.csv')).open('w') as output:
    writer = csv.writer(output, lineterminator='\n')
    writer.writerow(['stage', 'frame_size', 'channel', 'voice', 'calls', 'mean_inclusive_us', 'mean_exclusive_us'])
    grouped = defaultdict(list)
    for stage, sample, stamp, size, channel, voice, control, inclusive, exclusive in events[core, 64]:
        grouped[stage, size, channel, voice].append((inclusive, exclusive))
    for key, values in sorted(grouped.items()):
        writer.writerow([*key, len(values), f'{statistics.mean(v[0] for v in values)/1000:.6f}', f'{statistics.mean(v[1] for v in values)/1000:.6f}'])
print('All callback/stage/event accounting, render deadlines, input stamps, Attack control tags and 64/128 event identity pass.')

frame_rows=defaultdict(list)
frame_receipts={}
for line in source.splitlines():
    row=next(csv.reader([line]))
    if row and row[0]=='frame_counts':
        assert len(row) in (18,20) and row[1]==core
        values=tuple(map(int,row[3:]))
        frame_rows[int(row[2])].append(values+(0,0) if len(row)==18 else values)
    elif row and row[0]=='frame_receipt':
        assert len(row)==5 and row[4]=='overflow=0'
        key=row[1],int(row[2]); assert key not in frame_receipts
        frame_receipts[key]=int(row[3])
assert len(frame_receipts)==6
assert frame_rows[64]==frame_rows[128] and len(frame_rows[64])==5247
labels=('frame_magnitudes','frame_peak_frequency','frame_candidate_sort','frame_predictions','frame_association','frame_finalize')
for frames in (64,128):
    by_job=defaultdict(dict)
    for e in events[core,frames]:
        stage,sample,stamp,size,channel,voice,control,inclusive,exclusive=e
        if stage=='interpretation' or stage in labels:
            key=sample,stamp,size,channel
            assert stage not in by_job[key]
            by_job[key][stage]=(inclusive,exclusive)
    assert len(by_job)==frame_receipts[core,frames]==5247
    for row in frame_rows[frames]:
        sample,stamp,size,channel,bins,peaks,phase,logarithmic,reuse,predicted,matched,births,first,fallback,selected,snapshots,snapshot_reads=row
        assert bins==(37 if size==4096 else size//2+1) and 0<=reuse<=phase<=peaks<=bins
        assert phase+logarithmic<=peaks and 0<=predicted<=256 and 0<=selected<=256
        assert matched+births==min(peaks,256) and selected<=matched+births
        if snapshots:
            assert snapshots==1 and births>0 and snapshot_reads==512 and first==fallback==0
        else:
            assert snapshot_reads==0 and births<=first<=births*256
        assert 0<=fallback<=births*256 and fallback%256==0
        job=by_job[sample,stamp,size,channel]
        assert set(job)==set(labels)|{'interpretation'}
        assert job['interpretation'][0]-job['interpretation'][1]==sum(job[label][0] for label in labels)
with (root/(prefix+'-frame-counts.csv')).open('w') as output:
    writer=csv.writer(output,lineterminator='\n')
    writer.writerow(['sample','frame_stamp','frame_size','channel','bins','peaks','phase','logarithmic','phase_reuse','predicted','matched','births','first_steps','fallback_steps','selected','snapshots','snapshot_reads'])
    writer.writerows(frame_rows[64])
print('All 5247 frame records match both callback partitions; six child stages exactly account for each parent; work-count bounds pass.')
for size in (1024,2048,4096):
    rows=[r for r in frame_rows[64] if r[2]==size]
    totals=[sum(r[i] for r in rows) for i in range(4,17)]
    print('N=',size,'frames=',len(rows),'totals bins/peaks/phase/log/reuse/predicted/matched/births/first/fallback/selected/snapshots/reads=',totals)
    print('reusable phase %=',100*totals[4]/totals[2] if totals[2] else 0)
