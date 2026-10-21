import csv
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
p=Path(__file__).resolve().parent
def rows(pattern):return [r for f in p.glob(pattern) for r in csv.DictReader(l for l in f.read_text().splitlines() if not l.startswith('#'))]
plt.rcParams.update({'font.size':10,'axes.spines.top':False,'axes.spines.right':False,'figure.dpi':150})
sel=[('Ardor-Parametric-Overdrive-Demo.nam',1,'LSTM demo'),('JCM800.nam',0,'Snapshot Nano'),('JCM800.nam',1,'Snapshot Standard'),('clone-overdrive.nam',0,'Clone Nano'),('clone-overdrive.nam',1,'Clone Standard'),('a2-dual-0.nam',0,'Concat Nano'),('a2-dual-0.nam',1,'Concat Standard'),('film-dual-0.nam',0,'FiLM Nano'),('film-dual-0.nam',1,'FiLM Standard')]
rs=rows('model-sweep.csv')
print(rs[0].keys())
# Microbenchmark calls its tiers fixed/nano/full; avoid treating labels as integers.
def is_tier(r,t):return r['tier']==('nano' if t==0 else ('fixed' if Path(r['model']).name=='Ardor-Parametric-Overdrive-Demo.nam' else 'full'))
fig,ax=plt.subplots(figsize=(9,5))
for i,(m,t,name) in enumerate(sel):
 r=next(r for r in rs if Path(r['model']).name==m and is_tier(r,t) and r['block_frames']=='128')
 ax.barh(i,float(r['mean_budget_pct']),color='#316b9a',height=.6)
 ax.scatter(float(r['p999_budget_pct']),i,color='#cd5a27',marker='|',s=160,zorder=3)
ax.set_yticks(range(len(sel)),[s[2] for s in sel]);ax.invert_yaxis();ax.set_xlim(0,100);ax.axvline(70,color='#888',ls='--',lw=1);ax.set_xlabel('Single-processor time / 128-frame deadline (%)');ax.set_title('Pi 4 at 1.5 GHz • 48 kHz • 5,000 blocks per model\nBlue: mean; orange: p99.9; dashed: 70% headroom criterion');fig.tight_layout();fig.savefig(p/'single-model-128.png');plt.close(fig)
rs=rows('chain-*.csv')
fig,axes=plt.subplots(1,2,figsize=(11,4.8),sharey=True)
colors={'clone-overdrive.nam':'#316b9a','a2-dual-0.nam':'#ab4e2c','film-dual-0.nam':'#5c833c','JCM800.nam':'#8b589f','Ardor-Parametric-Overdrive-Demo.nam':'#777'}
for ax,t,title in zip(axes,[0,1],['Nano tiers (LSTM demo included)','Standard tiers']):
 for m,c in colors.items():
  if m.startswith('Ardor') and t==1:continue
  group=[r for r in rs if r['model']==m and float(r['tier'])==(1 if m.startswith('Ardor') else t) and r['block_frames']=='128']
  # Combine fill/screen repeats using worst quantile per count.
  by={}
  for r in group:by[int(r['nam_count'])]=max(by.get(int(r['nam_count']),0),float(r['p999_budget_pct']))
  ax.plot(sorted(by),[by[k] for k in sorted(by)],'-o',ms=4,color=c,label={'clone-overdrive.nam':'Clone','a2-dual-0.nam':'Concat','film-dual-0.nam':'FiLM','JCM800.nam':'Snapshot','Ardor-Parametric-Overdrive-Demo.nam':'LSTM demo'}[m])
 ax.axhline(70,color='#888',ls='--',lw=1);ax.axhline(100,color='#b33',ls=':',lw=1.2);ax.set_title(title);ax.set_xlabel('Serial NAM instances');ax.set_ylim(0,155);ax.set_xlim(.8,13.2 if t==0 else 3.2);ax.legend(fontsize=9,loc='upper left')
axes[0].set_ylabel('Full PedalEngine p99.9 / 128-frame deadline (%)');fig.suptitle('Measured chain cost is nonlinear • dashed 70%; dotted 100% deadline');fig.tight_layout();fig.savefig(p/'serial-chain-capacity-128.png');plt.close(fig)
