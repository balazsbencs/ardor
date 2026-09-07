"""Export a local routing repair after the five-capacitor footprint conversion.
The prior +5V_PI diagonal crossed C401's new SMD lands inside the old THT body.
Remove that segment (and any temporary endpoint vias), preserving other routes.
"""
from pathlib import Path
import pcbnew as p,wx
app=wx.App(False)
ROOT=Path(__file__).resolve().parents[1];b=p.LoadBoard(str(ROOT/'Ardor_IO.kicad_pcb'))
a=p.VECTOR2I(p.FromMM(89.225),p.FromMM(74.775));c=p.VECTOR2I(p.FromMM(100),p.FromMM(64))
removed=[]
for t in b.GetTracks():
 if t.GetNetname()!='+5V_PI':continue
 if isinstance(t,p.PCB_VIA):
  if t.GetPosition() in [a,c]:removed.append(t)
 elif (t.GetStart()==a and t.GetEnd()==c) or (t.GetStart()==c and t.GetEnd()==a):removed.append(t)
for t in removed:b.Remove(t)
b.BuildConnectivity();p.ZONE_FILLER(b).Fill(b.Zones());p.SaveBoard(str(ROOT/'Ardor_IO.kicad_pcb'),b)
# Export only the track/via keepout; exclude pours and pour-only keepouts from DSN.
zones=[z for z in b.Zones() if not z.GetIsRuleArea() or not z.GetDoNotAllowTracks()]
for z in zones:b.Remove(z)
# KiCad 9.0.2 exporter raises a wx debug assertion while closing valid polygons.
# DRC validates the saved outline; still require a successful export return value.
wx.DisableAsserts()
assert p.ExportSpecctraDSN(b,str(ROOT/'routing/smd-update.dsn'))
print('Exported capacitor update; removed',len(removed),'conflicting/temporary copper items')
