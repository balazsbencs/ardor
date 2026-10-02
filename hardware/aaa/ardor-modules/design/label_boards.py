"""Add readable back-side module IDs and connector pin legends.

Back-side legends keep the small component side uncluttered. Text candidates
avoid through-hole pads, vias, other text and the board edge. Native DRC is the
final authority for silkscreen clearance. Pin numbers always refer to square pad 1.
"""
import wx
app=wx.App(False)
import pcbnew as p
from pathlib import Path
import json,sys,shutil
from shapely.geometry import box
ROOT=Path(__file__).resolve().parents[1];folder=ROOT/sys.argv[1]
spec=json.loads((folder/'verification/design.json').read_text());file=folder/(spec['name']+'.kicad_pcb')
b=p.LoadBoard(str(file));w,h=spec['size_mm'];blocks=[];placed=[]
def rect(bb):return box(p.ToMM(bb.GetLeft()),p.ToMM(bb.GetTop()),p.ToMM(bb.GetRight()),p.ToMM(bb.GetBottom()))
for f in b.GetFootprints():
    for a in f.Pads():
        if a.GetDrillSize().x:blocks.append(rect(a.GetBoundingBox()).buffer(.15))
for a in b.GetTracks():
    if isinstance(a,p.PCB_VIA):blocks.append(rect(a.GetBoundingBox()).buffer(.15))
area=box(50.5,50.5,50+w-.5,50+h-.5)
def label(text,candidates,size=.8):
    t=p.PCB_TEXT(b);t.SetText(text);t.SetLayer(p.B_SilkS);t.SetMirrored(True);t.SetTextSize(p.VECTOR2I(p.FromMM(size),p.FromMM(size)));t.SetTextThickness(p.FromMM(.15))
    for x,y in candidates:
        t.SetPosition(p.VECTOR2I(p.FromMM(x),p.FromMM(y)));shape=rect(t.GetBoundingBox()).buffer(.05)
        if area.contains(shape) and not any(shape.intersects(v) for v in blocks):
            b.Add(t);blocks.append(shape);placed.append({'text':text,'position_mm':[x,y]});return
    raise AssertionError((folder.name,'No readable silk position',text))
title={'midi-in':'ARDOR MIDI M1','expression':'ARDOR EXPR M1','mixer':'ARDOR MIX M1','line-out':'ARDOR LINE M1','headphones':'ARDOR HP M1'}[folder.name]
label(title,[(50+w/2,50+h-2),(50+w/2,52),(50+w/2,50+h/2)]+[(50+w/2,52+y) for y in range(1,int(h)-4)],1)
short={'+5V':'5V','+3V3':'3V3','+3V3_A':'3V3','GND':'GND','CHASSIS':'CHS','MIDI_RX':'RX','MIDI_4':'4','MIDI_5':'5','EXP_TIP':'TIP','EXP_RING':'RNG','SDA':'SDA','SCL':'SCL','AUDIO_L':'L','AUDIO_R':'R','AUDIO_IN':'IN','OUT_L':'L','OUT_R':'R','OUT_MONO':'MONO','LINE_ENABLE':'EN','LINE_JACK':'OUT','AMP_FEED':'AMP','HP_ENABLE':'EN','HP_L':'L','HP_R':'R'}
for f in sorted(b.GetFootprints(),key=lambda f:f.GetReference()):
    ref=f.GetReference()
    if not ref.startswith('J'):continue
    pads=sorted([a for a in f.Pads() if a.GetNumber()],key=lambda a:int(a.GetNumber()))
    positions=[(p.ToMM(a.GetPosition().x),p.ToMM(a.GetPosition().y)) for a in pads];xs=[x for x,y in positions];ys=[y for x,y in positions];xmid=(min(xs)+max(xs))/2
    candidates=[(xmid,min(ys)-2.5),(xmid,max(ys)+2.5)]
    candidates += [(xmid+dx,min(ys)-2.5) for dx in [-2,2,-4,4,-6,6]]
    candidates += [(xmid+dx,max(ys)+2.5) for dx in [-2,2,-4,4,-6,6]]
    label(ref,candidates)
    if ref=='J302' and folder.name=='expression':
        label('J302: 1=T 2=R 3=G',[(61,70),(61,71),(62,72),(62,69),(63,73)])
        continue
    if ref.startswith('JP'):continue # Jumper maps are in the guide; both rows are densely packed.
    direction=1 if xmid<50+w/2 else -1
    for pad,(x,y) in zip(pads,positions):
        net=spec['pins'][ref+'.'+pad.GetNumber()];text=short[net]
        candidates=[(x+direction*d,y) for d in [4.5,5,5.5,6,6.5,7,7.5,8,8.5,9]]
        label(text,candidates)
p.SaveBoard(str(file),b);shutil.copyfile(folder/'verification/project-config.json',folder/(spec['name']+'.kicad_pro'))
(folder/'verification/silkscreen.json').write_text(json.dumps({'side':'Bottom; text is mirrored for normal reading from the back','legends':placed},indent=2)+'\n')
print(folder.name,len(placed),'silk legends')
