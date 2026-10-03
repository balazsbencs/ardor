"""Run native KiCad schematic exports and verify every intended connection."""
from pathlib import Path
import json, subprocess, xml.etree.ElementTree as ET
from concurrent.futures import ThreadPoolExecutor

ROOT=Path(__file__).resolve().parents[1]
def run(args):subprocess.run(['kicad-cli',*map(str,args)],check=True,stdout=subprocess.DEVNULL)
def export(folder):
    spec=json.loads((folder/'verification/design.json').read_text());sch=folder/(spec['name']+'.kicad_sch')
    run(['sch','export','netlist','--format','kicadxml','-o',folder/'verification/netlist.xml',sch])
    run(['sch','erc','--format','json','--severity-all','-o',folder/'verification/erc.json',sch])
    run(['sch','export','pdf','-o',folder/'review/schematic.pdf',sch])
    run(['sch','export','svg','-o',folder/'review',sch])
    x=ET.parse(folder/'verification/netlist.xml');found={};logical={};net_members={}
    for net in x.findall('.//nets/net'):
        name=net.attrib['name'];members={n.attrib['ref']+'.'+n.attrib['pin'] for n in net.findall('node')}
        targets={spec['pins'][p] for p in members if p in spec['pins']}
        assert len(targets)<=1,(folder.name,'SHORT',name,targets)
        if targets:
            target=targets.pop();assert target not in logical,(folder.name,'OPEN',target)
            logical[target]=name;net_members[target]=members
        for pin in members:found[pin]=name
    assert set(found)==set(spec['pins'])|set(spec['nc']),(folder.name,'missing/extra pins',set(found)^set(spec['pins'])^set(spec['nc']))
    for pin,net in spec['pins'].items():assert found[pin]==logical[net],(pin,net)
    for pin in spec['nc']:assert sum(v==found[pin] for v in found.values())==1,('NC connected',pin)
    erc=json.loads((folder/'verification/erc.json').read_text())
    violations=[v for s in erc['sheets'] for v in s['violations']]
    (folder/'verification/logical-net-map.json').write_text(json.dumps(logical,indent=2)+'\n')
    print(folder.name,'netlist PASS',len(found),'pins, ERC',len(violations),[(v['type'],v['description']) for v in violations[:8]])
    return len(violations)==0

if __name__=='__main__':
    folders=[p for p in ROOT.iterdir() if (p/'verification/design.json').exists()]
    with ThreadPoolExecutor(max_workers=3) as pool: result=list(pool.map(export,folders))
    assert all(result),'Resolve ERC violations before routing'
