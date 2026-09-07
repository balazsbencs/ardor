"""Convert the five legacy film footprints, preserving nets and existing routing.
Idempotent for an already converted board. Run with KiCad 9 pcbnew + wx.
"""
from pathlib import Path
import pcbnew as p,wx
app=wx.App(False)
ROOT=Path(__file__).resolve().parents[1]
REFS={'C401','C402','C502','C601','C602'}
LIB='Ardor_Capacitor';NAME='C_Rubycon_MU_3225';VALUE='2.2u / 16V film';MPN='Rubycon 16MU225KB23225'
b=p.LoadBoard(str(ROOT/'Ardor_IO.kicad_pcb'));fps={f.GetReference():f for f in b.GetFootprints()};removed=[];changed=[]
for ref in sorted(REFS):
 old=fps[ref]
 if str(old.GetFPID().GetLibNickname())==LIB:continue
 assert str(old.GetFPID().GetLibNickname())=='Capacitor_THT',ref
 pads={a.GetNumber():a for a in old.Pads()};assert set(pads)=={'1','2'}
 a,c=[pads[n].GetPosition() for n in ['1','2']]
 center=p.VECTOR2I((a.x+c.x)//2,(a.y+c.y)//2)
 fp=p.FootprintLoad(str(ROOT/'footprints'/(LIB+'.pretty')),NAME);assert fp
 fp.SetReference(ref);fp.SetValue(VALUE);fp.SetFPID(p.LIB_ID(LIB,NAME));fp.SetPath(old.GetPath());fp.SetSheetname(old.GetSheetname());fp.SetSheetfile(old.GetSheetfile())
 fp.SetPosition(center);fp.SetOrientationDegrees(old.GetOrientationDegrees());fp.SetAttributes(p.FP_SMD)
 fp.Reference().SetPosition(center);fp.Value().SetVisible(False)
 for pad in fp.Pads():
  previous=pads[pad.GetNumber()];pad.SetNet(previous.GetNet());origin=previous.GetPosition()
  # Old PTH pads were also layer transitions. Retain a via only where back copper uses it.
  back=any(t.GetLayer()==p.B_Cu and (t.GetStart()==origin or t.GetEnd()==origin) for t in b.GetTracks())
  if back:
   v=p.PCB_VIA(b);v.SetPosition(origin);v.SetWidth(p.F_Cu,p.FromMM(.6));v.SetDrill(p.FromMM(.3));v.SetViaType(p.VIATYPE_THROUGH);v.SetLayerPair(p.F_Cu,p.B_Cu);v.SetNet(previous.GetNet());b.Add(v)
  t=p.PCB_TRACK(b);t.SetStart(origin);t.SetEnd(pad.GetPosition());t.SetWidth(p.FromMM(.2));t.SetLayer(p.F_Cu);t.SetNet(previous.GetNet());b.Add(t)
 b.Remove(old);removed.append(old);b.Add(fp);changed.append(ref)
b.BuildConnectivity();p.ZONE_FILLER(b).Fill(b.Zones());p.SaveBoard(str(ROOT/'Ardor_IO.kicad_pcb'),b)
print('Converted:',', '.join(changed) or 'already SMD')
