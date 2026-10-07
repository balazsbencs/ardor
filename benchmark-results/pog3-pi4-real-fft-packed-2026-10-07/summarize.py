from pathlib import Path
import csv,sys,statistics
root=Path(sys.argv[1])
data={}
for label in ('baseline-1','candidate-1','candidate-2','baseline-2'):
 rows=list(csv.DictReader((root/(label+'.csv')).open()))
 assert len(rows)==14
 data[label]={(r['workload'],r['callback_frames']):r for r in rows}
 assert all(int(r['callback_allocations'])==0 for r in rows)
assert all(set(rows)==set(data['baseline-1']) for rows in data.values())
keys=list(data['baseline-1'])
fields=['workload','callback_frames','baseline_mean_demand_min_percent','baseline_mean_demand_max_percent','candidate_mean_demand_min_percent','candidate_mean_demand_max_percent','two_run_mean_reduction_percent','baseline_p99_min_us','baseline_p99_max_us','candidate_p99_min_us','candidate_p99_max_us','baseline_worst_us','candidate_worst_us']
with (root/'comparison.csv').open('w') as f:
 w=csv.DictWriter(f,fieldnames=fields,lineterminator="\n");w.writeheader()
 for key in keys:
  b=[data[label][key] for label in ('baseline-1','baseline-2')]
  c=[data[label][key] for label in ('candidate-1','candidate-2')]
  bd=[100*float(x['mean_us'])/float(x['budget_us']) for x in b];cd=[100*float(x['mean_us'])/float(x['budget_us']) for x in c]
  reduction=100*(1-statistics.mean(float(x['mean_us']) for x in c)/statistics.mean(float(x['mean_us']) for x in b))
  row=dict(zip(fields,[key[0],key[1],min(bd),max(bd),min(cd),max(cd),reduction,min(float(x['p99_us']) for x in b),max(float(x['p99_us']) for x in b),min(float(x['p99_us']) for x in c),max(float(x['p99_us']) for x in c),max(float(x['max_us']) for x in b),max(float(x['max_us']) for x in c)]))
  w.writerow({k:f'{v:.6f}' if isinstance(v,float) else v for k,v in row.items()})
  if key[0].startswith(('static','expression','freeze')):
   print(f'| {key[0]} | {key[1]} | {min(bd):.2f}–{max(bd):.2f}% | {min(cd):.2f}–{max(cd):.2f}% | {reduction:.2f}% | {max(float(x["p99_us"]) for x in c):.3f} | {max(float(x["max_us"]) for x in c):.3f} |')
