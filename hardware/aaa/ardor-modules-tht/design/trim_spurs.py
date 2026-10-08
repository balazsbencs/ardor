"""Peel only native-DRC identified dangling router branches.

Requires a fully connected board before each removal, then repeats DRC. A real
missing route cannot be mistaken for a harmless leaf. No fixed UUID assumptions.
"""
from pathlib import Path
import json,sys,subprocess
import sexpdata as sx
ROOT=Path(__file__).resolve().parents[1];folder=ROOT/sys.argv[1]
spec=json.loads((folder/'verification/design.json').read_text());file=folder/(spec['name']+'.kicad_pcb')
key=lambda v:str(v[0]) if isinstance(v,list) and v else ''
get=lambda v,k:next((x for x in v if key(x)==k),None)
removed=[]
for attempt in range(30):
    subprocess.run(['kicad-cli','pcb','drc','--schematic-parity','--format','json','--severity-all','-o',str(folder/'verification/drc.json'),str(file)],check=True,stdout=subprocess.DEVNULL)
    drc=json.loads((folder/'verification/drc.json').read_text())
    assert not drc['unconnected_items'] and not drc['schematic_parity'],(folder,'Cannot trim an incomplete board')
    leaves={v['items'][0]['uuid'] for v in drc['violations'] if v['type']=='track_dangling'}
    assert all(v['type']=='track_dangling' for v in drc['violations']),(folder,'Unexpected DRC violation')
    if not leaves:break
    data=sx.loads(file.read_text());kept=[]
    for item in data:
        if key(item)=='segment' and get(item,'uuid')[1] in leaves:
            removed.append({'uuid':get(item,'uuid')[1],'start_mm':get(item,'start')[1:],'end_mm':get(item,'end')[1:],'width_mm':get(item,'width')[1]})
        else:kept.append(item)
    assert len(data)-len(kept)==len(leaves)
    file.write_text(sx.dumps(kept)+'\n')
else:raise RuntimeError('Router spur trimming did not converge')
if removed:
    subprocess.run(['xvfb-run','-a',sys.executable,str(ROOT/'design/refill.py'),folder.name],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
(folder/'verification/router-spur-cleanup.json').write_text(json.dumps({'removed_dangling_segments':removed,'iterations':attempt},indent=2)+'\n')
print(folder.name,'trimmed',len(removed),'dangling segments; all circuits remained connected')
