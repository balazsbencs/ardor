"""Add short critical routes and export a Specctra design for local Freerouting."""
from pathlib import Path
import pcbnew as p
ROOT=Path(__file__).resolve().parents[1]
b=p.LoadBoard(str(ROOT/'Ardor_IO.kicad_pcb'));fps={f.GetReference():f for f in b.GetFootprints()}
mm=p.FromMM;pt=lambda x,y:p.VECTOR2I(mm(x+50),mm(y+50))
def pad(ref,num):return next(a for a in fps[ref].Pads() if a.GetNumber()==str(num))
def route(ref,num,other,onum,waypoints=(),width=.25):
 a=pad(ref,num);c=pad(other,onum);assert a.GetNetCode()==c.GetNetCode()
 points=[a.GetPosition(),*[pt(*v) for v in waypoints],c.GetPosition()]
 for start,end in zip(points,points[1:]):
  t=p.PCB_TRACK(b);t.SetStart(start);t.SetEnd(end);t.SetWidth(mm(width));t.SetLayer(p.F_Cu);t.SetNetCode(a.GetNetCode());b.Add(t)
for ref in ['U401','U402','U501']:
 route(ref,1,ref,2);route(ref,6,ref,7)
route('U601',11,'C607',1,[(74.0,47.75),(74.2,47.55)],.2)
route('U601',9,'C607',2,[(74.0,48.75),(74.7,49.45)],.2)
route('U601',12,'C605',1,[(73.85,47.25),(75.05,46.05)],.25)
route('U601',8,'C606',1,[(72.75,50.0),(72.95,50.2)],.25)
route('U601',14,'C604',1,[(72.25,45.7),(72.775,45.175)],.25)
# Continuous plane pour is kept outside the entire MIDI input component island.
z=p.ZONE(b);z.SetIsRuleArea(True);z.SetLayerSet(p.LSET.AllCuMask());z.SetDoNotAllowTracks(False);z.SetDoNotAllowVias(False);z.SetDoNotAllowCopperPour(True);z.SetDoNotAllowPads(False);z.SetDoNotAllowFootprints(False)
poly=z.Outline();poly.NewOutline()
for x,y in [(1,19),(26.2,19),(26.2,35),(1,35)]:poly.Append(mm(x+50),mm(y+50))
b.Add(z)
p.SaveBoard(str(ROOT/'Ardor_IO.kicad_pcb'),b)
# KiCad 9 exports pour-only keepouts as routing keepouts; omit this one from DSN.
b.Remove(z)
assert p.ExportSpecctraDSN(b,str(ROOT/'routing/compact.dsn'))
print('Exported DSN with critical local routes')
