"""Import local Freerouting result, add ground pours and refill (KiCad 9)."""
from pathlib import Path
import pcbnew as p
import wx
app=wx.App(False)
ROOT=Path(__file__).resolve().parents[1]
b=p.LoadBoard(str(ROOT/'Ardor_IO.kicad_pcb'))
assert p.ImportSpecctraSES(b,str(ROOT/'routing/compact.ses'))
mm=p.FromMM
for layer in [p.F_Cu,p.B_Cu]:
 z=p.ZONE(b);z.SetLayer(layer);z.SetNet(b.FindNet('GND'));z.SetLocalClearance(mm(.2));z.SetPadConnection(p.ZONE_CONNECTION_THERMAL);z.SetThermalReliefGap(mm(.25));z.SetThermalReliefSpokeWidth(mm(.3));z.SetMinThickness(mm(.2))
 poly=z.Outline();poly.NewOutline()
 for x,y in [(50.5,50.5),(117.5,50.5),(117.5,95.5),(50.5,95.5)]:poly.Append(mm(x),mm(y))
 b.Add(z)
b.BuildConnectivity();p.ZONE_FILLER(b).Fill(b.Zones());p.SaveBoard(str(ROOT/'Ardor_IO.kicad_pcb'),b)
print('Imported routing and filled front/back ground pours')
