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
poses=json.loads((ROOT/'design/compact-placement.json').read_text())
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
for a,c in [((50,50),(118,50)),((118,50),(118,96)),((118,96),(50,96)),((50,96),(50,50))]:
 s=p.PCB_SHAPE();s.SetShape(p.SHAPE_T_SEGMENT);s.SetStart(pt(*a));s.SetEnd(pt(*c));s.SetLayer(p.Edge_Cuts);s.SetWidth(mm(.05));b.Add(s)
def txt(text,x,y,size=.8,layer=p.F_SilkS):
 t=p.PCB_TEXT(b);t.SetText(text);t.SetPosition(pt(50+x,50+y));t.SetTextSize(pt(size,size));t.SetTextThickness(mm(.12));t.SetLayer(layer);b.Add(t);return t
# Connector labels read in the same order as the adjacent physical pads.
for s,x,y in [('PI / HOST',34,1.5),('1',9.5,1.5),('40',58,9),('AUX',3,9.5),('L',3,14.4),('G',5.54,14.4),('R',8.08,14.4),('MIDI',2.6,17),('4',1,20),('5',1,22.54),('EXPR',3.5,28.5),('T',1,31.5),('R',1,34.04),('S',1,36.58),('POLARITY',14.7,40.8),('T',67,17),('S',67,19.54),('S',67,24.8),('G',67,27.34),('L',67,31.5),('R',67,34.04),('G',67,36.58),('ARDOR IO',38,44.5)]:txt(s,x,y)
for s,x,y in [('LINE OUT',62.5,18.5),('AMP',62.8,26),('PHONES',62.5,34.5)]:txt(s,x,y).SetTextAngle(p.EDA_ANGLE(90,p.DEGREES_T))
# MIDI input-side exclusion from logic copper; same corridor as original design.
z=p.ZONE(b);z.SetIsRuleArea(True);z.SetLayerSet(p.LSET.AllCuMask());z.SetDoNotAllowTracks(True);z.SetDoNotAllowVias(True);z.SetDoNotAllowCopperPour(True);z.SetDoNotAllowPads(False);z.SetDoNotAllowFootprints(False)
poly=z.Outline();poly.NewOutline()
for x,y in [(17.7,16),(20.9,16),(20.9,29),(17.7,29)]:poly.Append(mm(50+x),mm(50+y))
b.Add(z)
txt('JP301: NORMAL 1-3 / 2-4; REVERSE 3-5 / 4-6',33,43,.8,p.B_SilkS).SetMirrored(True)
b.GetTitleBlock().SetTitle('Ardor Codec Zero IO - compact');b.GetTitleBlock().SetRevision('A-compact');b.GetTitleBlock().SetComment(0,'68 x 46 mm; provisional enclosure mounting. Verify stack height before manufacture.')
# Signal labels for test pads, positioned in clear space after final placement.
labels={'TP101':('5V A',44,18),'TP102':('3V3',29,12),'TP103':('GND',37.25,14.2),'TP201':('MIDI RX',31,28.75),'TP301':('EXP ADC',20.5,42.5),'TP401':('VREF',33,25.5),'TP402':('MONO',46.25,28.9),'TP501':('LINE',65.5,11.75),'TP601':('HP L',58,44.5)}
for ref,(s,x,y) in labels.items():txt(s,x,y)
# Rules use ordinary 0.20 mm signal tracks, 0.15 mm clearance, 0.60/0.30 mm vias.
proj=ROOT/'Ardor_IO.kicad_pro';settings=json.loads(proj.read_text());rules=settings['board']['design_settings']['rules'];rules['min_clearance']=.15;rules['min_track_width']=.15;rules['min_via_diameter']=.6;rules['min_through_hole_diameter']=.2
for nc in settings['net_settings']['classes']:
 nc.update(clearance=.15,track_width=.2,via_diameter=.6,via_drill=.3)
proj.write_text(json.dumps(settings,indent=2)+'\n')
p.SaveBoard(str(ROOT/'Ardor_IO.kicad_pcb'),b)
with (ROOT/'routing/placement.csv').open('w') as f:
 w=csv.writer(f,lineterminator="\n");w.writerow(['Reference','X_mm_from_left','Y_mm_from_top','Rotation_deg'])
 for ref,pose in sorted(poses.items()):w.writerow([ref,*pose])
print('Saved compact placement')
