"""Rebuild compact placement from the embedded footprints (DESTROYS current routing).
Run with KiCad 9's pcbnew Python module. Route routing/compact.dsn afterwards.
"""
from pathlib import Path
import ast, json, csv
import pcbnew as p
import wx
app=wx.App(False)
ROOT=Path(__file__).resolve().parents[1]
b=p.LoadBoard(str(ROOT/'Ardor_IO.kicad_pcb'))
mm=p.FromMM
pt=lambda x,y:p.VECTOR2I(mm(x),mm(y))
poses=next(ast.literal_eval(n.value) for n in ast.parse((ROOT/'design/place_pcb.py').read_text()).body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='poses' for t in n.targets))
for ref,(x,y,a) in list(poses.items()):
 num=int(''.join(filter(str.isdigit,ref)))
 if 300<=num<400:y-=12
 if 400<=num<500:x-=4
 if 500<=num<600:x-=8
 if 600<=num<700:x-=8;y-=8
 poses[ref]=(x,y,a)
poses.update({'C607':(76,48.5,270),'C606':(72,52,180),'C604':(72,44.5,180),'R401':(47,24,90),'R202':(34.5,28,90),'TP102':(39,17,0),'R601':(66,44,90),'R505':(79,40.5,0),'U501':(69,31,0),'TP402':(62,35,0),'TP101':(59,18,0),'C401':(38,23,0),'C402':(38,35,0),'R203':(33.5,33,0),'TP201':(33,37,0),'U401':(49,31,0),'R402':(54.5,36.5,90),'R404':(54.5,34,0),'C502':(48,48,90),'C403':(66,41,0),'J502':(87,40,0),'R101':(10,37,90)})
poses.update({'C401':(40.5,23,0),'C402':(40.5,35,0),'C502':(48,45.5,90),'C601':(59.5,44,0),'C602':(59.5,55,0)})
for i,xy in enumerate([(4,4),(86,4),(4,60),(86,60)],1):poses['H'+str(i)]=(*xy,0)
fps={f.GetReference():f for f in b.GetFootprints()}
removed=list(b.GetTracks())+list(b.Zones())+list(b.GetDrawings())
for item in removed:b.Remove(item)
for ref,f in fps.items():
 x,y,a=poses[ref];f.SetPosition(pt(50+x,50+y));f.SetOrientationDegrees(a)
 f.Reference().SetLayer(p.F_Fab);f.Reference().SetVisible(True);f.Reference().SetPosition(f.GetPosition())
 f.Value().SetVisible(False)
 # Library ${REFERENCE} text belongs on fabrication only.
 for item in f.GraphicalItems():
  if isinstance(item,p.PCB_TEXT) and item.GetText()=='${REFERENCE}':item.SetVisible(True)
for a,c in [((50,50),(140,50)),((140,50),(140,114)),((140,114),(50,114)),((50,114),(50,50))]:
 s=p.PCB_SHAPE();s.SetShape(p.SHAPE_T_SEGMENT);s.SetStart(pt(*a));s.SetEnd(pt(*c));s.SetLayer(p.Edge_Cuts);s.SetWidth(mm(.05));b.Add(s)
def txt(text,x,y,size=.8,layer=p.F_SilkS):
 t=p.PCB_TEXT(b);t.SetText(text);t.SetPosition(pt(50+x,50+y));t.SetTextSize(pt(size,size));t.SetTextThickness(mm(.12));t.SetLayer(layer);b.Add(t);return t
# Connector labels read in the same order as the adjacent physical pads.
for s,x,y in [('PI / HOST',48,3),('1',25,4.5),('40',73.3,12.3),('CODEC AUX',11,8.8),('L  G  R',10.54,10.8),('MIDI IN',6.5,20),('4',3.5,26),('5',3.5,28.54),('EXPRESSION',10,40),('T',3.5,45),('R',3.5,47.54),('S',3.5,50.08),('POLARITY',17,42),('LINE OUT',83,14),('T',89.3,27),('S',89.3,29.54),('AMP',86.5,38),('SIG',83.5,40),('G',89,42.54),('PHONES',84,56.5),('L',89.3,47),('R',89.3,49.54),('G',89.3,52.08),('ARDOR IO',49,61.8)]:txt(s,x,y)
# MIDI input-side exclusion from logic copper; same corridor as original design.
z=p.ZONE(b);z.SetIsRuleArea(True);z.SetLayerSet(p.LSET.AllCuMask());z.SetDoNotAllowTracks(True);z.SetDoNotAllowVias(True);z.SetDoNotAllowCopperPour(True);z.SetDoNotAllowPads(False);z.SetDoNotAllowFootprints(False)
poly=z.Outline();poly.NewOutline()
for x,y in [(26.2,19),(29.4,19),(29.4,37),(26.2,37)]:poly.Append(mm(50+x),mm(50+y))
b.Add(z)
txt('JP301: NORMAL 1-3 / 2-4; REVERSE 3-5 / 4-6',35,58,.8,p.B_SilkS).SetMirrored(True)
b.GetTitleBlock().SetTitle('Ardor Codec Zero IO - compact');b.GetTitleBlock().SetRevision('A-compact');b.GetTitleBlock().SetComment(0,'90 x 64 mm; provisional enclosure mounting. Verify stack height before manufacture.')
# Signal labels for test pads, positioned in clear space after final placement.
labels={'TP101':('5V A',55.5,18),'TP102':('3V3 ADC',33.5,17),'TP103':('GND',46,16.5),'TP201':('MIDI RX',30,38.5),'TP301':('EXP ADC',27,60.5),'TP401':('VREF',50,39),'TP402':('MONO',61,36.8),'TP501':('LINE',87,33),'TP601':('HP L',79,60.5)}
for ref,(s,x,y) in labels.items():txt(s,x,y)
# Rules use ordinary 0.20 mm signal tracks, 0.15 mm clearance, 0.60/0.30 mm vias.
proj=ROOT/'Ardor_IO.kicad_pro';settings=json.loads(proj.read_text());rules=settings['board']['design_settings']['rules'];rules['min_clearance']=.15;rules['min_track_width']=.15;rules['min_via_diameter']=.6;rules['min_through_hole_diameter']=.2
for nc in settings['net_settings']['classes']:
 nc.update(clearance=.15,track_width=.2,via_diameter=.6,via_drill=.3)
proj.write_text(json.dumps(settings,indent=2)+'\n')
p.SaveBoard(str(ROOT/'Ardor_IO.kicad_pcb'),b)
with (ROOT/'routing/placement.csv').open('w') as f:
 w=csv.writer(f);w.writerow(['Reference','X_mm_from_left','Y_mm_from_top','Rotation_deg'])
 for ref,pose in sorted(poses.items()):w.writerow([ref,*pose])
print('Saved compact placement')
