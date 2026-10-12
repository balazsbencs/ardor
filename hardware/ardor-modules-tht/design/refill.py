"""Refill the final board in a fresh KiCad process, retaining project rules."""
import wx
app=wx.App(False)
import pcbnew as p
from pathlib import Path
import json,sys,shutil
folder=Path(__file__).resolve().parents[1]/sys.argv[1]
spec=json.loads((folder/'verification/design.json').read_text());file=folder/(spec['name']+'.kicad_pcb')
b=p.LoadBoard(str(file))
for z in b.Zones():
    if not z.GetIsRuleArea():z.UnFill()
b.BuildConnectivity();assert p.ZONE_FILLER(b).Fill(b.Zones());p.SaveBoard(str(file),b)
shutil.copyfile(folder/'verification/project-config.json',folder/(spec['name']+'.kicad_pro'))
