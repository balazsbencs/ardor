import json,re
from pathlib import Path
p=Path(__file__).resolve().parent
rows=[]
for d in sorted(p.iterdir()):
 required=['final-telemetry.txt','playback-hw-params.txt','performance.txt','start-process-status.txt','final-process-status.txt']
 if not d.is_dir() or not all((d/f).exists() for f in required):continue
 t=dict(re.findall(r'(\S+?)=(\S+)',(d/'final-telemetry.txt').read_text()))
 hw=dict(line.split(':',1) for line in (d/'playback-hw-params.txt').read_text().splitlines() if ':' in line)
 perf=(d/'performance.txt').read_text(); duration=re.search(r'sample:\s+(\d+)s',perf).group(1)
 cpu=re.search(r'^  CPU2\s+(\S+)%',perf,re.M)
 rss={}
 for key in ['start','final']:
  rss[key]={x:int(y) for x,y in re.findall(r'^(VmRSS|VmHWM):\s+(\d+)',(d/f'{key}-process-status.txt').read_text(),re.M)}
 xrun=sum('__snd_pcm_xrun' in l for l in (d/'alsa-xrun-trace.txt').read_text().splitlines()) if (d/'alsa-xrun-trace.txt').exists() else None
 row=dict(case=d.name,duration_seconds=int(duration),native_period=int(hw['period_size']),native_buffer=int(hw['buffer_size']),telemetry=t,xruns=xrun,cpu2_busy=cpu.group(1) if cpu else None,rss_kib=rss)
 rows.append(row)
 print(d.name,duration+'s','period='+str(row['native_period']),'avg='+t['avg'],'max='+t['max'],'over='+t['over'],'gaps='+t['gaps'],'xrun='+str(xrun),'bypass='+t['bypassed'],'RSS='+str(rss))
(p/'live-summary.json').write_text(json.dumps(rows,indent=2)+'\n')
