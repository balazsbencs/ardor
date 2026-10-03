"""Repair the power route affected by C401's new SMD lands, then refill.
Run after convert_film_smd.py on the compact board. Other routing is preserved.
"""
from pathlib import Path
import pcbnew as p,wx
app=wx.App(False)
ROOT=Path(__file__).resolve().parents[1];b=p.LoadBoard(str(ROOT/'Ardor_IO.kicad_pcb'))
pt=lambda x,y:p.VECTOR2I(p.FromMM(x),p.FromMM(y))
a=pt(89.225,74.775);c=pt(100,64);v1=pt(90.9,74.775);v2=pt(94,70)
removed=[]
for t in b.GetTracks():
 if t.GetNetname()!='+5V_PI':continue
 if isinstance(t,p.PCB_VIA):
  if t.GetPosition() in [a,c]:removed.append(t)
 elif (t.GetStart()==a and t.GetEnd()==c) or (t.GetStart()==c and t.GetEnd()==a):removed.append(t)
for t in removed:b.Remove(t)
net=b.FindNet('+5V_PI')
# Only the span under the new lands uses B.Cu; cross back before the Pi control routes.
segments=[(a,v1,p.F_Cu),(v1,pt(94,71.675),p.B_Cu),(pt(94,71.675),v2,p.B_Cu),(v2,c,p.F_Cu)]
for start,end,layer in segments:
 if any(not isinstance(t,p.PCB_VIA) and t.GetNetCode()==net.GetNetCode() and t.GetLayer()==layer and t.GetStart()==start and t.GetEnd()==end for t in b.GetTracks()):continue
 t=p.PCB_TRACK(b);t.SetStart(start);t.SetEnd(end);t.SetWidth(p.FromMM(.4));t.SetLayer(layer);t.SetNet(net);b.Add(t)
for pos in [v1,v2]:
 if any(isinstance(t,p.PCB_VIA) and t.GetPosition()==pos and t.GetNetCode()==net.GetNetCode() for t in b.GetTracks()):continue
 v=p.PCB_VIA(b);v.SetPosition(pos);v.SetWidth(p.F_Cu,p.FromMM(.6));v.SetDrill(p.FromMM(.3));v.SetViaType(p.VIATYPE_THROUGH);v.SetLayerPair(p.F_Cu,p.B_Cu);v.SetNet(net);b.Add(v)
b.BuildConnectivity();p.ZONE_FILLER(b).Fill(b.Zones());p.SaveBoard(str(ROOT/'Ardor_IO.kicad_pcb'),b)
print('Repaired C401 power crossover and refilled ground pours')
