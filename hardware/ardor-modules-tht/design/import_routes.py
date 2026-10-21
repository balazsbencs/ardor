"""Import a routing candidate, apply isolation pour keepouts and refill GND."""
import wx
app=wx.App(False)
import pcbnew as p
from pathlib import Path
import json, sys, shutil

ROOT=Path(__file__).resolve().parents[1];folder=ROOT/sys.argv[1]
spec=json.loads((folder/'verification/design.json').read_text());file=folder/(spec['name']+'.kicad_pcb')
b=p.LoadBoard(str(folder/'routing/placement.kicad_pcb'));b.SetFileName(str(file))
for f in b.GetFootprints():
    if f.GetReference()=='U601':
        for a in f.Pads():
            if a.GetNumber()=='17':a.SetLocalZoneConnection(p.ZONE_CONNECTION_FULL)
assert p.ImportSpecctraSES(b,str(folder/'routing/board.ses'))
for keepout in spec['keepouts']:
    z=p.ZONE(b);z.SetIsRuleArea(True);z.SetLayerSet(p.LSET.AllCuMask());z.SetDoNotAllowTracks(False);z.SetDoNotAllowVias(False);z.SetDoNotAllowCopperPour(True);z.SetDoNotAllowPads(False);z.SetDoNotAllowFootprints(False)
    poly=z.Outline();poly.NewOutline()
    for x,y in keepout['polygon']:poly.Append(p.FromMM(50+x),p.FromMM(50+y))
    b.Add(z)
for z in b.Zones():
    if not z.GetIsRuleArea():z.UnFill()
b.BuildConnectivity();assert p.ZONE_FILLER(b).Fill(b.Zones())
p.SaveBoard(str(file),b)
shutil.copyfile(folder/'verification/project-config.json',folder/(spec['name']+'.kicad_pro'))
print(folder.name,'imported and filled')
