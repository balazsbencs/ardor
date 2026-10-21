"""Real manager AMP fixture check; isolated test root only, writes bank 97."""
import argparse,json,time,urllib.request,urllib.error
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--base-url',default='http://127.0.0.1:4189');p.add_argument('--model',type=Path);p.add_argument('--apply',action='store_true');a=p.parse_args()
results=[]
def request(method,path,body=None,kind='application/json',allowed=(200,)):
 if body is not None and not isinstance(body,bytes):body=json.dumps(body).encode()
 r=urllib.request.Request(a.base_url+path,data=body,method=method,headers={'Content-Type':kind})
 try:
  with urllib.request.urlopen(r,timeout=60) as f:status=f.status;payload=json.load(f)
 except urllib.error.HTTPError as f:status=f.code;payload=json.load(f)
 assert status in allowed,(path,status,payload)
 results.append(dict(method=method,path=path,status=status));return payload
if a.model:
 boundary='ardor-amp-validation';body=(f'--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="Hardware FiLM amp.nam"\r\nContent-Type: application/octet-stream\r\n\r\n').encode()+a.model.read_bytes()+f'\r\n--{boundary}--\r\n'.encode()
 asset=request('POST','/api/assets/models',body,'multipart/form-data; boundary='+boundary,(201,))
 assert asset['nam']['gearType']=='amp' and asset['nam']['supported'],asset
 assert len(asset['nam']['parameters'])==2,asset
 preset=dict(version=1,name='Hardware FiLM amp',routing='serial',blocks=[dict(id='amp',type='nam',enabled=True,asset=asset['path'],params=dict(useNano=False,modelParameters=dict(Drive=10,Tone=-5)))])
 preset['global']=dict(inputGainDb=0,outputGainDb=-30,safetyLimitDb=-1)
 request('PUT','/api/presets/banks/97/slots/0',preset)
 results.append(dict(asset=asset,preset=preset))
if a.apply:
 started=time.monotonic();queued=request('POST','/api/presets/banks/97/slots/0/apply',allowed=(202,))
 for _ in range(600):
  state=request('GET','/api/runtime/apply/'+queued['id'],allowed=(200,202))
  if state['state']!='pending':
   assert state['state']=='applied',state;break
  time.sleep(.1)
 else:raise AssertionError('apply timeout')
 active=request('GET','/api/device')['active'];assert active['bank']==97 and active['slot']==0,active
 results.append(dict(applied=True,milliseconds=1000*(time.monotonic()-started),active=active))
path=Path(__file__).resolve().parent/('amp-apply-results.json' if a.apply else 'amp-import-results.json');path.write_text(json.dumps(dict(passed=True,results=results),indent=2)+'\n')
print('PASS amp', 'apply' if a.apply else 'import')
