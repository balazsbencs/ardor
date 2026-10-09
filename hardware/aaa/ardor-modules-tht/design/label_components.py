"""Put every builder-fitted reference on clear front silkscreen space."""
import wx
app = wx.App(False)
import pcbnew as p
import json
import sys
import shutil
from pathlib import Path
from shapely.geometry import box

ROOT = Path(__file__).resolve().parents[1]
folder = ROOT/sys.argv[1]
spec = json.loads((folder/'verification/design.json').read_text())
file = folder/(spec['name']+'.kicad_pcb')
board = p.LoadBoard(str(file))
def rect(bb):
    return box(p.ToMM(bb.GetLeft()),p.ToMM(bb.GetTop()),p.ToMM(bb.GetRight()),p.ToMM(bb.GetBottom()))
blocks = []
for fp in board.GetFootprints():
    for pad in fp.Pads():
        blocks.append(rect(pad.GetBoundingBox()).buffer(.25))
    for item in fp.GraphicalItems():
        if item.GetLayer() in [p.F_CrtYd,p.F_SilkS]:
            blocks.append(rect(item.GetBoundingBox()).buffer(.15))
for item in board.GetTracks():
    if isinstance(item,p.PCB_VIA):
        blocks.append(rect(item.GetBoundingBox()).buffer(.2))
area = box(50.6,50.6,50+spec['size_mm'][0]-.6,50+spec['size_mm'][1]-.6)
records = []
for fp in sorted(board.GetFootprints(),key=lambda f:f.GetReference()):
    if fp.GetReference() not in spec['parts']:
        continue
    ref = fp.GetReference()
    target = fp.Reference().GetPosition()
    tx,ty = p.ToMM(target.x),p.ToMM(target.y)
    text = p.PCB_TEXT(board)
    text.SetText(ref)
    text.SetLayer(p.F_SilkS)
    text.SetTextSize(p.VECTOR2I(p.FromMM(.9),p.FromMM(.9)))
    text.SetTextThickness(p.FromMM(.15))
    candidates = [(tx+dx/2,ty+dy/2) for dx in range(-20,21) for dy in range(-20,21)]
    candidates.sort(key=lambda v:(v[0]-tx)**2+(v[1]-ty)**2)
    for x,y in candidates:
        text.SetPosition(p.VECTOR2I(p.FromMM(x),p.FromMM(y)))
        shape = rect(text.GetBoundingBox()).buffer(.15)
        if area.contains(shape) and not any(shape.intersects(b) for b in blocks):
            board.Add(text)
            fp.Reference().SetPosition(text.GetPosition())
            blocks.append(shape)
            records.append({'reference':ref,'position_mm':[round(x,4),round(y,4)]})
            break
    else:
        raise AssertionError((folder.name,'No readable reference position',ref))
p.SaveBoard(str(file),board)
shutil.copyfile(folder/'verification/project-config.json',folder/(spec['name']+'.kicad_pro'))
(folder/'verification/component-silkscreen.json').write_text(json.dumps(records,indent=2)+'\n')
print(folder.name,len(records),'front component references')
