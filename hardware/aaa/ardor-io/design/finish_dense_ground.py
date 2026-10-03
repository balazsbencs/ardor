"""Tie the Pi header's small back-layer ground pockets to the front ground pour."""
from pathlib import Path
import pcbnew as p
import wx
app=wx.App(False)
ROOT=Path(__file__).resolve().parents[1]
b=p.LoadBoard(str(ROOT/'Ardor_IO.kicad_pcb'));f=next(f for f in b.GetFootprints() if f.GetReference()=='J101')
for number,dx,dy in [('9',-.9,-.4),('14',.4,.9),('20',.4,.9)]:
 pad=next(a for a in f.Pads() if a.GetNumber()==number);origin=pad.GetPosition();pos=p.VECTOR2I(origin.x+p.FromMM(dx),origin.y+p.FromMM(dy))
 if any(isinstance(t,p.PCB_VIA) and t.GetPosition()==pos for t in b.GetTracks()):continue
 via=p.PCB_VIA(b);via.SetPosition(pos);via.SetWidth(p.F_Cu,p.FromMM(.6));via.SetDrill(p.FromMM(.3));via.SetViaType(p.VIATYPE_THROUGH);via.SetLayerPair(p.F_Cu,p.B_Cu);via.SetNet(pad.GetNet());b.Add(via)
 track=p.PCB_TRACK(b);track.SetStart(origin);track.SetEnd(pos);track.SetWidth(p.FromMM(.3));track.SetLayer(p.F_Cu);track.SetNet(pad.GetNet());b.Add(track)
b.BuildConnectivity();p.ZONE_FILLER(b).Fill(b.Zones());p.SaveBoard(str(ROOT/'Ardor_IO.kicad_pcb'),b)
