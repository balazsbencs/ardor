"""Fill zones in a fresh KiCad process after net reassignment and route pruning."""
from pathlib import Path
import wx
app=wx.App(False)
import pcbnew as p
r=Path(__file__).resolve().parents[1]
b=p.LoadBoard(str(r/'Ardor_IO.kicad_pcb'))
for zone in b.Zones():
 if not zone.GetIsRuleArea():zone.UnFill()
b.BuildConnectivity()
assert p.ZONE_FILLER(b).Fill(b.Zones())
p.SaveBoard(str(r/'Ardor_IO.kicad_pcb'),b)
