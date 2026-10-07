#!/usr/bin/env python3
"""Two callback-size cost replay; predictions only, never optimization timings."""
import csv
import gzip
from pathlib import Path
import sys
source, output=map(Path,sys.argv[1:])
layouts={
 'original':[17,38,81,102,129,145,161,177,193,209,225,241],
 'trial1_rejected':[65,80,96,111,127,129,141,153,166,178,191,193],
 'refined':[65,85,106,127,129,141,153,166,178,191,193,241]}
wall={64:{},128:{}};jobs={64:[],128:[]}
for line in gzip.decompress((source/'profile.log.gz').read_bytes()).decode().splitlines():
 row=next(csv.reader([line]))
 if len(row)<3 or row[1]!='matched_freeze_off' or row[2] not in ('64','128'):continue
 frames=int(row[2])
 if row[0]=='phase_callback':wall[frames][int(row[3])]=float(row[5])
 if row[0]=='phase_event' and row[3]=='long_render':
  _,sample,stamp,_,channel,voice,_,inclusive,_=row[3:]
  jobs[frames].append((int(sample),int(stamp),int(voice)*2+int(channel),int(inclusive)/1000))
rows=[]
for frames in (64,128):
 assert set(wall[frames])==set(range(192000//frames)) and len(jobs[frames])==8988
 base=[wall[frames][i] for i in range(192000//frames)];residual=base.copy()
 for sample,stamp,job,cost in jobs[frames]:residual[(sample-1)//frames]-=cost
 assert min(residual)>=0
 for label,ages in layouts.items():
  assert len(ages)==12 and all(a<b for a,b in zip(ages,ages[1:])) and 9<min(ages) and max(ages)<256
  times=residual.copy()
  for sample,stamp,job,cost in jobs[frames]:
   if frames==128 and label=='refined':assert (sample-1)//128==(stamp+ages[job]-1)//128
   times[(stamp+ages[job]-1)//frames]+=cost
  assert abs(sum(times)-sum(base))<.00001
  delta=max(abs(a-b) for a,b in zip(times,base))
  if label=='original' or (label=='refined' and frames==128):assert delta<1e-8
  ordered=sorted(times);over=sum(t>frames/.048 for t in times)
  rows.append([label,frames,over,ordered[int(.99*(len(times)-1))],max(times),delta])
  print(f'{label} {frames}: modeled_over={over}, p99={rows[-1][3]:.3f}, max={max(times):.3f}, max_callback_work_delta={delta:.6f}')
with (output/'schedule-model.csv').open('w') as file:
 writer=csv.writer(file,lineterminator='\n');writer.writerow(['layout','frames','modeled_over_count','modeled_p99_us','modeled_max_us','max_callback_work_delta_us']);writer.writerows(rows)
print('Refined layout assigns every recorded long job to its original 128-frame callback. Both sizes preserve total recorded work. Cache/changed costs/instrumentation remain unmodeled.')
