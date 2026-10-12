import ast,json,time
from pathlib import Path
p=Path('/tmp/ardor-hw-local/finish-unattended.py');tree=ast.parse(p.read_text());ns={}
selected=[x for x in tree.body if isinstance(x,(ast.Import,ast.ImportFrom,ast.FunctionDef)) or (isinstance(x,ast.Assign) and any(isinstance(t,ast.Name) and t.id in {'REPO','EVID','REPORT','ROOT','SSH'} for t in x.targets))]
exec(compile(ast.Module(body=selected,type_ignores=[]),str(p),'exec'),ns)
remote=ns['remote'];root=ns['ROOT'];log=ns['log'];evid=ns['EVID']
for _ in range(60):
 if 'PASS' in remote('cat '+root+'/results/public-amp-manager-integration-final/validation-status.txt 2>/dev/null || true'):break
 time.sleep(1)
else:raise RuntimeError('public amp live run did not finish')
time.sleep(3)
log('Final live amp checks complete; restoring original service')
restored=False;errors=[]
try:
 ns['restore']();restored=True;log('Restoration and production audio health verified')
except Exception as e:
 errors.append(str(e));log('Restoration verification failed: '+str(e))
ns['collect']()
ns['write_report'](restored,errors)
(evid/'unattended-status.json').write_text(json.dumps(dict(restored=restored,errors=errors,manual_completion=True,host_completion_interrupted=True,finished_utc=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())),indent=2)+'\n')
log('Evidence and report collected; awaiting manual review before committing')
