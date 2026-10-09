"""Replace owned back-silkscreen legends; number every connector/jumper pin.

Number markers stay beside the matching pad. Referenced pin-map blocks explain
all terminal functions and directions. Existing copper/footprints are untouched.
"""
import wx
app=wx.App(False)
import pcbnew as p
from pathlib import Path
import json,sys,shutil,math
import sexpdata as sx
from shapely.geometry import box
from interfaces import contract
ROOT=Path(__file__).resolve().parents[1];folder=ROOT/sys.argv[1]
spec=json.loads((folder/'verification/design.json').read_text());guide=contract(folder.name,spec);file=folder/(spec['name']+'.kicad_pcb')
key=lambda v:str(v[0]) if isinstance(v,list) and v else ''
get=lambda v,k:next((x for x in v if key(x)==k),None)
reportfile=folder/'verification/silkscreen.json'
old={v['text'] for v in json.loads(reportfile.read_text())['legends']} if reportfile.exists() else set()
data=sx.loads(file.read_text());data[:]=[v for v in data if not(key(v)=='gr_text' and v[1] in old and get(v,'layer')[1]=='B.SilkS')]
file.write_text(sx.dumps(data)+'\n')
b=p.LoadBoard(str(file));w,h=spec['size_mm'];blocks=[];placed=[];numbers={};maps={}
def rect(bb):return box(p.ToMM(bb.GetLeft()),p.ToMM(bb.GetTop()),p.ToMM(bb.GetRight()),p.ToMM(bb.GetBottom()))
for f in b.GetFootprints():
    for a in f.Pads():
        if a.GetDrillSize().x:blocks.append(rect(a.GetBoundingBox()).buffer(.18))
for a in b.GetTracks():
    if isinstance(a,p.PCB_VIA):blocks.append(rect(a.GetBoundingBox()).buffer(.18))
for item in b.GetDrawings():
    if isinstance(item,p.PCB_TEXT) and item.GetLayer()==p.B_SilkS:blocks.append(rect(item.GetBoundingBox()).buffer(.08))
area=box(50.5,50.5,50+w-.5,50+h-.5)
def label(text,candidates,size=.8,meta=None):
    t=p.PCB_TEXT(b);t.SetText(text);t.SetLayer(p.B_SilkS);t.SetMirrored(True);t.SetTextSize(p.VECTOR2I(p.FromMM(size),p.FromMM(size)));t.SetTextThickness(p.FromMM(.15))
    for x,y in candidates:
        t.SetPosition(p.VECTOR2I(p.FromMM(x),p.FromMM(y)));shape=rect(t.GetBoundingBox()).buffer(.08)
        if area.contains(shape) and not any(shape.intersects(v) for v in blocks):
            b.Add(t);blocks.append(shape);record={'text':text,'position_mm':[round(x,4),round(y,4)],**(meta or {})};placed.append(record);return record
    if meta and meta.get('optional'):return None
    raise AssertionError((folder.name,'No readable silk position',text))
fps={f.GetReference():f for f in b.GetFootprints()}
# Place the most constrained markers first: six-terminal polarity jumper.
refs=sorted(guide['ports'],key=lambda r:(r!='JP301',r))
for ref in refs:
    f=fps[ref];pads=sorted([a for a in f.Pads() if a.GetNumber()],key=lambda a:int(a.GetNumber()));xs=[p.ToMM(a.GetPosition().x) for a in pads];xmid=(min(xs)+max(xs))/2
    for a in pads:
        x,y=p.ToMM(a.GetPosition().x),p.ToMM(a.GetPosition().y);direction=(-1 if x<xmid else 1) if max(xs)-min(xs)>1 else (1 if xmid<50+w/2 else -1)
        candidates=[(x+direction*d,y+dy) for dy in [0,-.25,.25,-.5,.5] for d in [1.7,1.9,2.1,2.3]]
        candidates += [(x-direction*d,y) for d in [1.7,1.9,2.1,2.3]]
        name=ref+'.'+a.GetNumber();numbers[name]=label(a.GetNumber(),candidates,meta={'pin_number_for':name})
    ys=[p.ToMM(a.GetPosition().y) for a in pads]
    candidates=[(xmid+dx,y) for y in [min(ys)-2.5,max(ys)+2.5,min(ys)-3.5,max(ys)+3.5] for dx in [0,-2,2,-4,4]]
    label(ref,candidates,meta={'connector_reference':ref})
# Reserve the module identity near an edge, then fit complete referenced pin maps.
title={'midi-in':'ARDOR MIDI T1','expression':'ARDOR EXPR T1','mixer':'ARDOR MIX T1','line-out':'ARDOR LINE T1','headphones':'ARDOR HP T1'}[folder.name]
label(title,[(50+w/2,50+h-1.7),(50+w/2,51.8)]+[(50+w/2,52+y) for y in range(1,int(h)-4)],1)
def grid_near(x,y):
    pts=[(50+xx/2,50+yy/2) for xx in range(3,int(2*w)-2) for yy in range(3,int(2*h)-2)]
    return sorted(pts,key=lambda a:(a[0]-x)**2+(a[1]-y)**2)
for ref in sorted(guide['ports'],key=lambda r:-len(guide['ports'][r]['pins'])):
    c=guide['ports'][ref];f=fps[ref];pads=list(f.Pads());x=sum(p.ToMM(a.GetPosition().x) for a in pads)/len(pads);y=sum(p.ToMM(a.GetPosition().y) for a in pads)/len(pads)
    lines=[ref+' '+c['silk']]
    if ref=='JP301':
        lines += ['1 TIP   2 RING','3 PEDAL IN  4 EXC OUT','5 RING  6 TIP']
    elif ref=='JP302':lines += ['1 GND 2 SELECT 3 3V3','48:1-2  49:2-3']
    else:
        for i in range(0,len(c['pins']),2):lines.append('  '.join(str(a['number'])+' '+a['short'] for a in c['pins'][i:i+2]))
    targetx=x+(8 if x<50+w/2 else -8)
    metadata={'pin_map_for':ref,'described_pins':[ref+'.'+str(a['number']) for a in c['pins']]}
    choices=[lines,[ref,c['silk'],*[str(a['number'])+' '+a['short'] for a in c['pins']]]]
    if ref=='JP302':choices.append(['JP302','ADDR','1 GND','2 SEL','3 3V3','48:1-2','49:2-3'])
    for option in choices:
        found=label('\n'.join(option),grid_near(targetx,y),meta={**metadata,'optional':True})
        if found:maps[ref]=found;break
    else:raise AssertionError((folder.name,'Cannot label connector',ref))
for pin,note in guide['unused_pins'].items():
    ref,num=pin.split('.');a=next(a for a in fps[ref].Pads() if a.GetNumber()==num);x,y=p.ToMM(a.GetPosition().x),p.ToMM(a.GetPosition().y)
    label(pin+' NC\nNO WIRE',grid_near(x,y+3),meta={'unused_pin':pin})
for note in (['IN=INTO','OUT=FROM','I/O=BOTH','JP=SHUNTS'] if folder.name=='expression' else ['IN=INTO','OUT=FROM']):
    label(note,grid_near(50+w/2,50+h/2))
p.SaveBoard(str(file),b);shutil.copyfile(folder/'verification/project-config.json',folder/(spec['name']+'.kicad_pro'))
report={'side':'Bottom; text is mirrored for normal reading from the back','legends':placed,'numbered_connector_pins':numbers,'pin_maps':maps,'unused_pins':guide['unused_pins'],'signal_viewpoint':'IN enters this board; OUT leaves this board; I/O is both directions; GND is 0 V'}
reportfile.write_text(json.dumps(report,indent=2)+'\n')
print(folder.name,len(numbers),'numbered pins;',len(maps),'complete maps;',len(placed),'silk legends')
