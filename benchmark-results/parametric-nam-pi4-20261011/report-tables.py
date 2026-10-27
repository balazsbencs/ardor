import csv
from pathlib import Path
p=Path(__file__).resolve().parent
def rows(pattern):
 return [r for f in p.glob(pattern) for r in csv.DictReader(l for l in f.read_text().splitlines() if not l.startswith('#'))]
def label(r):
 n=Path(r['model']).stem
 return {'Ardor-Parametric-Overdrive-Demo':'Open LSTM demo','JCM800':'Resident JCM800 A2','a2-dual-0':'Trainer concat A2','film-dual-0':'Trainer FiLM A2','clone-overdrive':'Published Clone Overdrive'}.get(n,n)+(' Nano' if float(r['tier'])==0 else ' Standard' if n!='Ardor-Parametric-Overdrive-Demo' else '')
q=rows('qualified-*.csv');screen=rows('chain-*.csv')
s=['| Model | 64-frame count | Mean / p99.9 budget | 128-frame count | Mean / p99.9 budget | Largest screened zero-miss count, 64 / 128 |','|---|---:|---:|---:|---:|---:|']
for key in sorted({label(r) for r in q}):
 a,b=[next(r for r in q if label(r)==key and r['block_frames']==str(f)) for f in (64,128)]
 m=[max(int(r['nam_count']) for r in screen if label(r)==key and r['block_frames']==str(f) and int(r['deadline_misses'])==0) for f in (64,128)]
 s.append(f"| {key} | {a['nam_count']} | {float(a['mean_budget_pct']):.1f}% / {float(a['p999_budget_pct']):.1f}% | {b['nam_count']} | {float(b['mean_budget_pct']):.1f}% / {float(b['p999_budget_pct']):.1f}% | {m[0]} / {m[1]} |")
(p/'capacity-table.md').write_text('\n'.join(s)+'\n')
s=['| Pedal model / tier | Pedals + amp = NAM count | 64-frame mean / p99.9 | Misses / 5,000 | 128-frame mean / p99.9 | Misses / 5,000 |','|---|---:|---:|---:|---:|---:|']
mx=rows('mixed-*.csv')
for key in sorted({(label(r),int(r['nam_count'])) for r in mx}):
 a,b=[next(r for r in mx if (label(r),int(r['nam_count']))==key and r['block_frames']==str(f)) for f in (64,128)]
 s.append(f"| {key[0]} | {key[1]} | {float(a['mean_budget_pct']):.1f}% / {float(a['p999_budget_pct']):.1f}% | {a['deadline_misses']} | {float(b['mean_budget_pct']):.1f}% / {float(b['p999_budget_pct']):.1f}% | {b['deadline_misses']} |")
(p/'mixed-table.md').write_text('\n'.join(s)+'\n')
print((p/'capacity-table.md').read_text())
print('Measured blocks:',len(rows('model-sweep.csv'))*5000,sum(int(r['iterations']) for r in screen),sum(int(r['iterations']) for r in q),sum(int(r['iterations']) for r in mx))
