"""Audition public amp import/control/tier applies on the isolated Pi manager."""
import argparse,json,time,urllib.request,urllib.error
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--base-url',default='http://127.0.0.1:4189');p.add_argument('--model',type=Path,required=True);a=p.parse_args()
results=[]
def request(method,path,body=None,kind='application/json',allowed=(200,)):
 if body is not None and not isinstance(body,bytes):body=json.dumps(body).encode()
 req=urllib.request.Request(a.base_url+path,data=body,method=method,headers={'Content-Type':kind})
 try:
  with urllib.request.urlopen(req,timeout=60) as f:status=f.status;payload=json.load(f)
 except urllib.error.HTTPError as f:status=f.code;payload=json.load(f)
 assert status in allowed,(path,status,payload)
 results.append(dict(method=method,path=path,status=status));return payload
boundary='ardor-public-amp-validation'
body=(f'--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="Hardware British 800 Preamp.nam"\r\nContent-Type: application/octet-stream\r\n\r\n').encode()+a.model.read_bytes()+f'\r\n--{boundary}--\r\n'.encode()
asset=request('POST','/api/assets/models',body,'multipart/form-data; boundary='+boundary,(201,))
assert asset['nam']['gearType']=='amp' and asset['nam']['supported'],asset
specs=asset['nam']['parameters'];assert [x['name'] for x in specs]==['Gain'],specs
for slot,(nano,level) in enumerate([(True,'min'),(False,'max'),(False,'min'),(True,'max')]):
 params=dict(useNano=nano,modelParameters={x['name']:x[level] for x in specs})
 preset=dict(version=1,name='Hardware public amp '+str(slot),routing='serial',blocks=[dict(id='amp',type='nam',enabled=True,asset=asset['path'],params=params)])
 preset['global']=dict(inputGainDb=0,outputGainDb=-30,safetyLimitDb=-1)
 request('PUT',f'/api/presets/banks/96/slots/{slot}',preset)
 saved=request('GET',f'/api/presets/banks/96/slots/{slot}')['preset'];assert saved['blocks'][0]['params']==params,saved
 start=time.monotonic();queued=request('POST',f'/api/presets/banks/96/slots/{slot}/apply',allowed=(202,))
 for _ in range(600):
  state=request('GET','/api/runtime/apply/'+queued['id'],allowed=(200,202))
  if state['state']!='pending':
   assert state['state']=='applied',state;break
  time.sleep(.1)
 else:raise AssertionError('apply timeout')
 active=request('GET','/api/device')['active'];assert active['bank']==96 and active['slot']==slot,active
 results.append(dict(applied=True,slot=slot,parameters=params,milliseconds=1000*(time.monotonic()-start),active=active))
 time.sleep(1)
Path(__file__).resolve().parent.joinpath('public-amp-api-results.json').write_text(json.dumps(dict(passed=True,asset=asset,results=results),indent=2)+'\n')
print('PASS public British 800 amp: import, saved controls, four live control/tier applies')
