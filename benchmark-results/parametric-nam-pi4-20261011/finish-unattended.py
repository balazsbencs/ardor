"""Complete the authorized isolated Pi validation, collect evidence, and update PR 121."""
import csv, json, math, re, subprocess, sys, time, traceback
from pathlib import Path
REPO=Path('/home/bbalazs/projects/ardor')
EVID=REPO/'benchmark-results/parametric-nam-pi4-20261011'
REPORT=REPO/'docs/parametric-nam-pi4-validation-2026-10-11.md'
ROOT='/tmp/ardor-hw-20261011'
SSH=['ssh','-S','/tmp/ardor-hw-ssh-%C','-o','BatchMode=yes','-o','ConnectTimeout=10','root@192.168.88.12']
def run(args,**kw):
 return subprocess.run(args,check=True,text=True,capture_output=True,**kw).stdout
def remote(script): return run(SSH+['sh','-s'],input=script,timeout=180)
def collect():
 with subprocess.Popen(SSH+['tar -C '+ROOT+'/results -cf - .'],stdout=subprocess.PIPE) as p:
  subprocess.run(['tar','-C',str(EVID),'-xf','-'],stdin=p.stdout,check=True)
  if p.wait(): raise RuntimeError('evidence transfer failed')
def log(message): print(time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),message,flush=True)
def restore():
 remote('set -eu\nroot='+ROOT+'\ntrace="$root/tracing/instances/ardor-parametric-hw"\nif [ -d "$trace" ]; then echo 0 > "$trace/tracing_on"; echo nop > "$trace/current_tracer"; rmdir "$trace"; fi\numount "$root/tracing"\ntouch "$root/finished"\n')
 for _ in range(90):
  if 'RESTORED' in remote('cat '+ROOT+'/results/guardian-recovery.log'): break
  time.sleep(1)
 else: raise RuntimeError('restoration guardian did not finish')
 time.sleep(5)
 check=remote('set -eu\nsha256sum -c '+ROOT+'/results/user-files-before.sha256\nsha256sum /usr/bin/ardor-pedal /etc/ardor-pedal.env\ncat /opt/ardor-pedal/runtime/active-preset.json\ncat /proc/asound/card0/pcm0p/sub0/hw_params\ncat /run/ardor-pedal.telemetry\ncat /sys/class/leds/ardor:audio-output-enable/brightness\namixer get Headphone\nps -eo pid,args\n')
 (EVID/'restoration.log').write_text(check)
 if 'bac7586876709a0b3ed6782da3457f43d9f04611deaa4562724e1a0f5cb87649' not in check: raise RuntimeError('installed binary changed')
 if not re.search(r'"bank"\s*:\s*0',check) or not re.search(r'"slot"\s*:\s*0',check): raise RuntimeError('original slot not restored')
 if 'period_size: 128' not in check or 'rate: 48000' not in check: raise RuntimeError('original audio configuration not restored')
 if not re.search(r'^\s*400\s+.*ardor-managerd',check,re.M): raise RuntimeError('production manager PID changed')
 health=remote('sh '+ROOT+'/device-performance-remote.sh 30 1 ardor-pedal 2 3 0 /run/ardor-pedal.telemetry')
 (EVID/'restoration-performance.txt').write_text(health)
 if 'RESULT: PASS' not in health: raise RuntimeError('restored audio health failed')
 subprocess.run(SSH[:-1]+['-O','cancel','-L','127.0.0.1:4189:127.0.0.1:18080',SSH[-1]],capture_output=True)
 return check

def write_report(restored,errors):
 live=[]
 for name in ['soak-clone-amp-cab-final','soak-film-five-isolated-final','extended-clone-amp-cab','extended-film-four','extended-wdw-four','extended-snapshot-two','amp-manager-integration-final','public-amp-manager-integration-final']:
  d=EVID/name
  if not (d/'final-telemetry.txt').exists():
   live.append(f'| {name} | Incomplete | — | — | — | — | — | — | — |'); continue
  t=dict(re.findall(r'(\S+?)=(\S+)',(d/'final-telemetry.txt').read_text()))
  perf=(d/'performance.txt').read_text(); duration=re.search(r'sample:\s+(\d+)s',perf).group(1)
  xr=sum('__snd_pcm_xrun' in l for l in (d/'alsa-xrun-trace.txt').read_text().splitlines())
  start=dict(re.findall(r'(\S+?)=(\S+)',(d/'start-telemetry.txt').read_text()))
  worker_bad=any(int(t.get(k,0))!=int(start.get(k,0)) for k in ['worker_over','worker_underflow','worker_submit_miss'])
  passed='RESULT: PASS' in perf and not worker_bad and xr==0
  live.append(f"| {name} | {duration} s | {t['callbacks']} | {t['avg']} / {t['max']} | {t['over']} / {t['gaps']} | {t.get('worker_over','0')} / {t.get('worker_underflow','0')} / {t.get('worker_submit_miss','0')} | {xr} | {t['nonfinite']} / {t['block_mismatch']} / {t['bypassed']} | {'PASS' if passed else 'FAIL'} |")
 content='\n'.join(['| Live case | Duration | Lifetime callbacks | Mean / maximum | Processing overruns / gaps | Worker over / underflow / submit miss | Kernel XRUNs | Nonfinite / mismatch / bypass | Performance check |','|---|---:|---:|---:|---:|---:|---:|---:|---|',*live])
 content+='\n\nDurations are requested minimum probe windows; polling overhead slightly extends actual elapsed time, which the CPU probe measures independently. Lifetime counters also include startup and settling. Five FiLM Nano blocks at 64 frames had five processing overruns in ten minutes, despite zero kernel XRUNs and no bypass. Its offline five-block result is a compute qualification only; it is not a zero-fault live recommendation. Consult the four-block extended run above for the more conservative live result. The two-snapshot case is a boundary experiment, not an endorsement of two enabled amps in the normal serial manager workflow.\n\n'
 content+='The real manager API recorded 269 observations, including four supported-format uploads, duplicate handling, malformed/unsupported assets, saved controls, rename propagation, 40 alternating control/tier applications, and rejection without changing the active preset. Forty-two successful applies measured 123.7 ms minimum, 195.6 ms median, 245.7 ms p95 and 916.9 ms maximum. Eight desktop/phone browser workflows used the real Pi API without mocks; two additional workflows verified amp classification and the one-enabled-amp constraint. The standalone FiLM amp import and final runtime apply are retained separately.\n\n'
 content+='The deliberately overloaded two-FiLM-Standard positive control recorded three kernel XRUNs, 33 processing overruns and latched bypass. Its very low CPU usage after bypass is not a healthy capacity result. Fresh Clone Nano recovery passed with zero faults. Native default-device empty and concat Standard runs at 32/64/128 frames passed; the initial indexed-device burst case is excluded as described below. Trace-buffer statistics are retained per case.\n\n'
 rows=list(csv.DictReader((EVID/'thermal.csv').open()))
 temps=[int(x['temperature_mC'])/1000 for x in rows]; clocks=[int(x['clock_kHz']) for x in rows]; alarms=[int(x['undervoltage_alarm']) for x in rows]
 content+=f"Across {len(rows)} five-second thermal observations, sampled temperature ranged from {min(temps):.1f} to {max(temps):.1f} °C, CPU clock from {min(clocks)/1000:.0f} to {max(clocks)/1000:.0f} MHz, and undervoltage alarms totaled {sum(alarms)}. The thermal logger continued through the idle period after the overnight suite, until manual completion resumed. Per-case thermal ranges in the probes identify the actual loaded runs. These counters do not independently establish every possible firmware throttle reason. Each live case retains start/final RSS and thread scheduling records.\n\n"
 for name in ['extended-clone-amp-cab','extended-film-four','extended-wdw-four','extended-snapshot-two']:
  d=EVID/name
  if not (d/'final-process-status.txt').exists():continue
  values=[]
  for phase in ['start','final']:
   m=re.search(r'^VmRSS:\s+(\d+)',(d/f'{phase}-process-status.txt').read_text(),re.M);values.append(m.group(1))
  content+=f"- `{name}` process RSS: {values[0]} → {values[1]} KiB.\n"
 resource_rows=list(csv.DictReader((EVID/'resources.csv').open()))
 content+='\nThirty-second resource samples supplement the endpoint RSS checks. Sampling started partway through the first extended run.\n\n| Extended case | Resource samples | RSS range (KiB) | Temperature range (°C) | CPU clock range (MHz) | Undervoltage samples |\n|---|---:|---:|---:|---:|---:|\n'
 for name in ['extended-clone-amp-cab','extended-film-four','extended-wdw-four','extended-snapshot-two']:
  observed=[x for x in resource_rows if x['case']==name]
  if not observed:continue
  rss=[int(x['rss_kib']) for x in observed];temp=[int(x['temperature_mC'])/1000 for x in observed];clock=[int(x['clock_kHz'])/1000 for x in observed]
  content+=f"| {name} | {len(observed)} | {min(rss)}–{max(rss)} | {min(temp):.1f}–{max(temp):.1f} | {min(clock):.0f}–{max(clock):.0f} | {sum(int(x['undervoltage_alarm']) for x in observed)} |\n"
 content+='\nThe separate listening session is described under limitations; subjective feedback was positive. The post-listening UI-only quiet-preview build was checked separately on ARM and does not change these DSP workloads.\n'
 if (EVID/'public-model-arm-sweep.csv').exists():
  public=list(csv.DictReader(line for line in (EVID/'public-model-arm-sweep.csv').read_text().splitlines() if not line.startswith('#')))
  content+='\n### Published NAMix models on ARM\n\nAll twelve released files were additionally loaded on this Pi across all 27 tiers, at 64 and 128 frames: 54 cases × 3,000 measured blocks, following 300 warm-up blocks. All sampled checksums were finite. This adds 162,000 measured blocks to the earlier 1,140,000-block suite. These are single-processor measurements with default learned controls, not full-chain capacity or a guarantee across the entire control range. Every case, including deadline misses, is retained in `public-model-arm-sweep.csv`.\n\n| Published amp / tier | 64-frame mean / p99.9 budget | Misses / 3,000 | 128-frame mean / p99.9 budget | Misses / 3,000 |\n|---|---:|---:|---:|---:|\n'
  amps={'british-800-preamp.nam','cali-duo-reverb-85watt.nam','special-bundle-overdrive-183-rock-channel.nam','special-bundle-overdrive-183-jazz-channel.nam'}
  for row in public:
   if Path(row['model']).name not in amps or row['block_frames']!='64':continue
   other=next(x for x in public if x['model']==row['model'] and x['tier']==row['tier'] and x['block_frames']=='128')
   content+=f"| {Path(row['model']).stem} / {row['tier']} | {float(row['mean_budget_pct']):.1f}% / {float(row['p999_budget_pct']):.1f}% | {row['deadline_misses']} | {float(other['mean_budget_pct']):.1f}% / {float(other['p999_budget_pct']):.1f}% | {other['deadline_misses']} |\n"
  if (EVID/'public-amp-api-results.json').exists():
   outcome=json.loads((EVID/'public-amp-api-results.json').read_text())
   content+='\nThe published British 800 Preamp also passed real manager upload, amp classification, save/readback, and four acknowledged runtime applies covering minimum/maximum Gain and Nano/Standard tiers. Its 90-second duplex run appears above. Output remained muted; no listening claim is made for this amp. This test imports into the temporary manager only and does not bundle the model.\n' if outcome['passed'] else '\nPublic amp API validation failed.\n'
 s=REPORT.read_text().replace('<!-- LIVE_RESULTS -->',content)
 restoration='The installed service was restarted with the original Init preset at bank 0 / slot 0, 48 kHz / 128 frames. All saved preset/settings hashes and the installed pedal executable matched their pre-test values. The production manager retained PID 400. Temporary audio/manager processes and tracing were stopped, the SSH forwarding was canceled, and the restored production audio passed a 30-second performance probe. See `restoration.log`, `restoration-performance.txt`, `guardian-extended.log` and `guardian-recovery.log` in the evidence directory.' if restored else 'Restoration could not be fully verified; inspect `unattended-status.json` and the restoration logs before treating validation as complete.'
 s=s.replace('<!-- RESTORATION -->',restoration).replace('> Extended unattended soaks are running. Final live results and restoration checks will be added after they finish.\n\n','')
 if errors:s+='\n## Unattended completion errors\n\n'+ '\n'.join('- '+e for e in errors)+'\n'
 REPORT.write_text(s)

errors=[];restored=False
try:
 log('Waiting for extended suite')
 deadline=time.monotonic()+3*3600
 while time.monotonic()<deadline:
  try: status=remote('cat '+ROOT+'/results/extended-suite.log')
  except subprocess.SubprocessError:time.sleep(20);continue
  if 'SUITE_EXIT' in status:
   if 'SUITE_COMPLETE' not in status:errors.append('Extended suite exited before all cases completed.')
   break
  time.sleep(20)
 else:raise RuntimeError('extended suite did not finish within three hours')
 log('Benchmarking all published NAMix tiers on ARM')
 public=run(SSH+['sh','-s'],input='set -eu\nroot='+ROOT+'\nchrt -f 60 "$root/bin/pedal-nam-bench" --cpu 2 --block-sizes 64,128 --iterations 3000 --warmup 300 --tiers all "$root/public-models" > "$root/results/public-model-arm-sweep.csv" 2> "$root/results/public-model-arm-sweep.log"\n',timeout=900)
 sweep=remote('cat '+ROOT+'/results/public-model-arm-sweep.csv')
 cases=list(csv.DictReader(line for line in sweep.splitlines() if not line.startswith('#')))
 if len(cases)!=54 or not all(math.isfinite(float(x['checksum'])) for x in cases):raise RuntimeError('public ARM sweep did not produce 54 finite sampled checksums')
 log('Public ARM sweep: 54 cases completed')
 log('Running quiet-control ARM regression smokes')
 smoke=remote('set -eu\nroot='+ROOT+'\nARDOR_TEST_SOURCE_DIR="$root/source" ARDOR_TEST_NAM_EXAMPLE_MODEL="$root/source/example-lstm.nam" "$root/bin/pedal-parametric-nam-smoke"\n"$root/bin/pedal-parametric-trainer-compat-smoke"\n"$root/bin/pedal-ui-model-smoke"\n')
 (EVID/'quiet-controls-arm-smokes.log').write_text(smoke)
 remote('set -eu\nroot='+ROOT+'\ncp "$root/bin/ardor-hw-pedal-quiet" "$root/bin/ardor-hw-pedal"\nnohup sh "$root/run-live.sh" amp-manager-integration-final 128 90 "$root/data/presets/empty.json" ui > "$root/results/amp-manager-integration-final-launch.log" 2>&1 < /dev/null &\n')
 for _ in range(30):
  ready=remote('test -r '+ROOT+'/results/amp-manager-integration-final/start-telemetry.txt && echo READY || true')
  if 'READY' in ready:break
  time.sleep(1)
 else:raise RuntimeError('final amp runtime did not start')
 log(run([sys.executable,str(EVID/'validate-amp.py'),'--apply'],timeout=90).strip())
 for _ in range(150):
  if 'PASS' in remote('cat '+ROOT+'/results/amp-manager-integration-final/validation-status.txt 2>/dev/null || true'):break
  time.sleep(1)
 else:raise RuntimeError('final amp integration did not complete')
 time.sleep(3)
 log('Checking published British 800 amp on real manager/runtime')
 remote('set -eu\nroot='+ROOT+'\nnohup sh "$root/run-live.sh" public-amp-manager-integration-final 128 90 "$root/data/presets/empty.json" ui > "$root/results/public-amp-manager-integration-final-launch.log" 2>&1 < /dev/null &\n')
 for _ in range(30):
  ready=remote('test -r '+ROOT+'/results/public-amp-manager-integration-final/start-telemetry.txt && echo READY || true')
  if 'READY' in ready:break
  time.sleep(1)
 else:raise RuntimeError('public amp runtime did not start')
 log(run([sys.executable,str(EVID/'validate-public-amp.py'),'--model','/tmp/ardor-public-parametric-models/british-800-preamp.nam'],timeout=90).strip())
 for _ in range(150):
  if 'PASS' in remote('cat '+ROOT+'/results/public-amp-manager-integration-final/validation-status.txt 2>/dev/null || true'):break
  time.sleep(1)
 else:raise RuntimeError('public amp integration did not complete')
 time.sleep(3)
except Exception as e:
 errors.append(str(e));log(traceback.format_exc())
finally:
 try:restore();restored=True;log('Original service restored and verified')
 except Exception as e:errors.append('Restoration: '+str(e));log(traceback.format_exc())
 try:collect();write_report(restored,errors)
 except Exception as e:errors.append('Report: '+str(e));log(traceback.format_exc())
 (EVID/'unattended-status.json').write_text(json.dumps(dict(restored=restored,errors=errors,finished_utc=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())),indent=2)+'\n')
 if restored and not errors:
  try:
   log(run([sys.executable,str(EVID/'live-summary.py')]))
   if run(['git','branch','--show-current'],cwd=REPO).strip()!='feat/parametric-nam-models':raise RuntimeError('branch changed; leaving report uncommitted')
   run(['git','diff','--cached','--quiet'],cwd=REPO)
   paths=['.gitattributes','CMakeLists.txt','docs/parametric-nam.md','docs/parametric-nam-pi4-validation-2026-10-11.md','tests/parametric_trainer_compat_smoke.cpp','tests/nam_chain_bench.cpp','scripts/validate-parametric-device.py','services/managerd/internal/webui/dist','benchmark-results/parametric-nam-pi4-20261011']
   run(['git','add','--',*paths],cwd=REPO);run(['git','diff','--cached','--check'],cwd=REPO)
   run(['git','commit','-m','test: qualify parametric NAM on Raspberry Pi 4'],cwd=REPO)
   run(['git','push','origin','feat/parametric-nam-models'],cwd=REPO)
   body=json.loads(run(['gh','pr','view','121','--json','body'],cwd=REPO))['body']
   body=body.replace('- [ ] Device qualification remains outstanding: Raspberry Pi CPU/latency and physical touchscreen behavior were not tested on hardware.','- [x] Raspberry Pi 4 ARM correctness, manager/browser integration, live audio, user listening and extended soaks completed. [Detailed measurements and limits](https://github.com/balazsbencs/ardor/blob/feat/parametric-nam-models/docs/parametric-nam-pi4-validation-2026-10-11.md). Analog round-trip latency and physical touchscreen gestures remain unmeasured. Parameter changes now complete without success toasts.')
   bodyfile=Path('/tmp/ardor-hw-local/pr-body-final.md');bodyfile.write_text(body)
   run(['gh','pr','edit','121','--body-file',str(bodyfile)],cwd=REPO)
   log('Report and evidence pushed to PR 121')
  except Exception as e:
   errors.append('PR update: '+str(e));log(traceback.format_exc())
   (EVID/'unattended-status.json').write_text(json.dumps(dict(restored=restored,errors=errors),indent=2)+'\n')
 log('Finished')
