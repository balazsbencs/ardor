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
        elif stage in ('forward_fft', 'interpretation'):
            assert size in (1024, 2048, 4096) and channel in (0, 1)
            assert stamp % (size // 8) == 0 and age == channel
        elif stage.endswith('_attack'):
            assert channel == -2 and voice == -1 and control != 0
            assert age == (8 if stage == 'short_attack' else 1)
            assert size == {'short_attack': 1024, 'long_attack': 2048, 'low_attack': 4096}[stage]
            assert stamp % (size // 8) == 0
        elif stage.startswith('attack_'):
            assert channel == -2 and voice == -1 and control != 0
            assert size in (1024, 2048, 4096) and stamp % (size // 8) == 0
            assert age == (8 if size == 1024 else 1)
        else:
            raise AssertionError(stage)
    assert len(events[core, frames]) == receipts[core, frames] == 58816
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

attack_rows = defaultdict(list)
for line in source.splitlines():
    row=next(csv.reader([line]))
    if row and row[0]=='attack_counts':
        assert len(row)==13
        attack_rows[int(row[2])].append(tuple(map(int,row[3:])))
assert attack_rows[64]==attack_rows[128] and len(attack_rows[64])==2622
labels=('attack_observations','attack_group_scoring','attack_group_families','attack_reservation','attack_partials','attack_canonical_index')
for frames in (64,128):
    by_job=defaultdict(dict)
    for e in events[core,frames]:
        stage,sample,stamp,size,channel,voice,control,inclusive,exclusive=e
        if stage.endswith('_attack') or stage in labels:
            assert stage not in by_job[sample,stamp,size]
            by_job[sample,stamp,size][stage]=(control,inclusive,exclusive)
    assert len(by_job)==2622
    for sample,stamp,resolution,control,left,right,observations,candidates,families,births in attack_rows[frames]:
        size=(2048,1024,4096)[resolution]
        assert 0<=left<=256 and 0<=right<=256 and 0<=observations<=256
        assert 0<=candidates<=64 and candidates<=observations and 0<=families<=16 and 0<=births<=observations
        job=by_job[sample,stamp,size]; parent=('long_attack','short_attack','low_attack')[resolution]
        assert set(job)==set(labels)|{parent}
        assert all(x[0]==control for x in job.values())
        assert job[parent][1]-job[parent][2]==sum(job[label][1] for label in labels)
with (root/(prefix+'-attack-counts.csv')).open('w') as output:
    writer=csv.writer(output,lineterminator='\n')
    writer.writerow(['sample','frame_stamp','resolution','Attack_control_bits','left_regions','right_regions','observations','candidates','families','births','modeled_support_classifications'])
    for row in attack_rows[64]:writer.writerow([*row,row[6]*row[7]])
print('All 2622 Attack records match both callback partitions; six child stages exactly account for each parent; work-count bounds pass.')

micro=defaultdict(dict)
for line in source.splitlines():
    row=next(csv.reader([line]))
    if row and row[0]=='attack_micro':
        assert len(row)==10
        _,workload,frames,sample,stamp,resolution,stage,calls,inclusive,exclusive=row
        key=int(frames),int(sample),int(stamp),int(resolution)
        assert stage not in micro[key]
        micro[key][stage]=tuple(map(int,(calls,inclusive,exclusive)))
assert len(micro)==5244
with (root/(prefix+'-micro-summary.csv')).open('w') as output:
    writer=csv.writer(output,lineterminator='\n');writer.writerow(['resolution','stage','updates','calls','inclusive_mean_us_per_update','exclusive_mean_us_per_update'])
    totals=defaultdict(lambda:[0,0,0,0])
    partial_events={(frames,e[1],e[2],e[3]):e[-2] for frames in (64,128) for e in events[core,frames] if e[0]=='attack_partials'}
    for frames in (64,128):
        for sample,stamp,resolution,control,left,right,observations,candidates,families,births in attack_rows[frames]:
            values=micro[frames,sample,stamp,resolution]
            assert set(values)=={'owner','envelope','canonical','binding_birth_remainder'}
            assert values['owner'][0]==values['envelope'][0]<=observations
            assert values['binding_birth_remainder'][0]==observations
            assert values['canonical'][0]==(0 if resolution==2 else values['owner'][0])
            assert all(0<=v[2]<=v[1] for v in values.values())
            assert values['binding_birth_remainder'][1]-values['binding_birth_remainder'][2]==sum(values[x][1] for x in ('owner','envelope','canonical'))
            assert values['binding_birth_remainder'][1]<=partial_events[frames,sample,stamp,(2048,1024,4096)[resolution]]
            if frames==64:
                for stage,value in values.items():
                    total=totals[resolution,stage];total[0]+=1
                    for j in range(3):total[j+1]+=value[j]
    for (resolution,stage),v in sorted(totals.items()):
        writer.writerow([resolution,stage,v[0],v[1],v[2]/v[0]/1000,v[3]/v[0]/1000])
print('Detailed per-partial nested costs and counts audited; their timings include dense diagnostic overhead and are never optimization measurements.')
