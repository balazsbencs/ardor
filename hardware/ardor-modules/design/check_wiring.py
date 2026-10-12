"""Independent geometric check of drawn wires, labels and symbol pin locations."""
from pathlib import Path
import json, math
import sexpdata as sx
from cad import ROOT, key, get

def check(folder):
    spec=json.loads((folder/'verification/design.json').read_text())
    sch=sx.loads((folder/(spec['name']+'.kicad_sch')).read_text())
    defs={x[1]:x for x in get(sch,'lib_symbols')[1:]};parents={};pins={};labels={}
    def root(p):
        parents.setdefault(p,p)
        if parents[p]!=p:parents[p]=root(parents[p])
        return parents[p]
    def union(a,b): parents[root(a)]=root(b)
    for item in sch:
        if key(item)=='wire':
            a,b=[tuple(v[1:]) for v in get(item,'pts')[1:]];union(a,b)
        if key(item)=='label':
            pt=tuple(get(item,'at')[1:3]);net=item[1]
            if net in labels:union(pt,labels[net])
            else:labels[net]=pt
        if key(item)=='symbol':
            ref=next(v[2] for v in item if key(v)=='property' and v[1]=='Reference')
            if ref.startswith('#'):continue
            unit=get(item,'unit')[1];x,y,rot=get(item,'at')[1:];r=math.radians(rot)
            for sub in defs[get(item,'lib_id')[1]]:
                if key(sub)=='symbol' and int(sub[1].split('_')[-2]) in [0,unit]:
                    for pin in sub:
                        if key(pin)!='pin':continue
                        n=str(get(pin,'number')[1]);dx,dy,*_=get(pin,'at')[1:]
                        p=round(x+dx*math.cos(r)-dy*math.sin(r),4),round(y-dx*math.sin(r)-dy*math.cos(r),4)
                        pins[ref+'.'+n]=p
    roots={};names={}
    for pin,net in spec['pins'].items():
        point=root(pins[pin]);roots.setdefault(point,{}).setdefault(net,[]).append(pin)
        names.setdefault(net,{}).setdefault(point,[]).append(pin)
    shorts=[v for v in roots.values() if len(v)>1]
    opens={k:list(v.values()) for k,v in names.items() if len(v)>1}
    nc_short=[pin for pin in spec['nc'] if root(pins[pin]) in roots]
    if shorts or opens or nc_short:
        print(folder.name,'SHORTS',shorts,'OPENS',opens,'NC_CONNECTED',nc_short);return False
    print(folder.name,'wiring OK:',len(pins),'pins,',len(names),'nets');return True

if __name__=='__main__':
    result=[check(p) for p in sorted(ROOT.iterdir()) if (p/'verification/design.json').exists()]
    assert all(result),'Drawn wiring differs from intended circuit'
