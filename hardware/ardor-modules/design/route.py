"""Configure ground pours / uniform widths and run local Freerouting 2.1.0.

The router supplies candidate signal routes only. Final KiCad DRC, schematic
parity and explicit net/pad validation decide whether a board can be released.
"""
from pathlib import Path
import json, subprocess, sys, copy
from concurrent.futures import ThreadPoolExecutor
import sexpdata as sx
from cad import ROOT, key, get, S

def configure(folder):
    spec=json.loads((folder/'verification/design.json').read_text());mapping=json.loads((folder/'verification/logical-net-map.json').read_text())
    dsn=sx.loads((folder/'routing/board.dsn').read_text().replace('(string_quote ")','(string_quote double_quote)').replace('[','__LB__').replace(']','__RB__'));network=get(dsn,'network')
    network[:]=[v for v in network if not (key(v)=='class' and str(v[1]) in ['Ground','Chassis'])]
    default=next(v for v in network if key(v)=='class')
    split={mapping['GND']:'Ground'}
    if 'CHASSIS' in mapping:split[mapping['CHASSIS']]='Chassis'
    # Class member strings precede the class options. Keep ordinary copper .2 mm.
    default[:]=[v for i,v in enumerate(default) if i<2 or isinstance(v,list) or str(v) not in split]
    rule=get(default,'rule');get(rule,'width')[1]=200;get(rule,'clearance')[1]=200
    for net,cls in split.items():
        opts=[copy.deepcopy(v) for v in default[2:] if isinstance(v,list)]
        new=[S('class'),S(cls),net,*opts];get(get(new,'rule'),'width')[1]=600 if cls=='Chassis' else 400
        network.append(new)
    (folder/'routing/board.dsn').write_text(sx.dumps(dsn).replace('(string_quote double_quote)','(string_quote ")').replace('__LB__','[').replace('__RB__',']')+'\n')

def route(folder, jar):
    configure(folder)
    args=['java','-Xmx1g','-jar',jar,'-de',str(folder/'routing/board.dsn'),'-do',str(folder/'routing/board.ses'),'-mp','15','-mt','1','-inc','Ground','-da','--gui.enabled=false',f'--user_data_path=/tmp/ardor-router/{folder.name}']
    with (folder/'routing/router.log').open('w') as log:subprocess.run(args,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=300)
    assert (folder/'routing/board.ses').exists(),folder
    output=(folder/'routing/board.ses').read_text();log=(folder/'routing/router.log').read_text()
    assert '(wire' in output and 'NullPointerException' not in log and 'number expected' not in log,(folder,'invalid routing result')
    print(folder.name,'routed')

if __name__=='__main__':
    jar=sys.argv[1];folders=[ROOT/s for s in sys.argv[2:]] or [p for p in ROOT.iterdir() if (p/'verification/design.json').exists()]
    with ThreadPoolExecutor(max_workers=2) as pool:list(pool.map(lambda f:route(f,jar),folders))
