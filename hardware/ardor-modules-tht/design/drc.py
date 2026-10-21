"""Run native PCB DRC including schematic parity, without exclusions."""
from pathlib import Path
import json, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
ROOT=Path(__file__).resolve().parents[1]
def check(folder):
    spec=json.loads((folder/'verification/design.json').read_text())
    subprocess.run(['kicad-cli','pcb','drc','--schematic-parity','--format','json','--severity-all','-o',str(folder/'verification/drc.json'),str(folder/(spec['name']+'.kicad_pcb'))],check=True,stdout=subprocess.DEVNULL)
    x=json.loads((folder/'verification/drc.json').read_text())
    print(folder.name,'violations',len(x['violations']),'unconnected',len(x['unconnected_items']),'parity',len(x['schematic_parity']))
    for v in x['violations']:
        if v['type'] not in ['via_dangling']:
            print(' ',v['type'],[i['description'] for i in v['items']])
    return not x['violations'] and not x['unconnected_items'] and not x['schematic_parity']
if __name__=='__main__':
    folders=[ROOT/s for s in sys.argv[1:]] or [p for p in ROOT.iterdir() if (p/'verification/design.json').exists()]
    with ThreadPoolExecutor(max_workers=2) as pool:result=list(pool.map(check,folders))
    sys.exit(0 if all(result) else 1)
