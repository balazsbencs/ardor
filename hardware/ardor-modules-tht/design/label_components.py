"""Put every builder-fitted reference on clear front silkscreen space."""
import wx
app = wx.App(False)
import pcbnew as p
import json
import sys
import shutil
from pathlib import Path
from shapely.geometry import box
from shapely.strtree import STRtree

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
        # Courtyards include empty lead space beyond the body. It is valid
        # to print a reference there if it clears pads and lead graphics;
        # reserve the actual Fab body and Silk shapes instead.
        if item.GetLayer()==p.F_SilkS or (item.GetLayer()==p.F_Fab and not isinstance(item,p.PCB_TEXT)):
            blocks.append(rect(item.GetBoundingBox()).buffer(.15))
for item in board.GetTracks():
    if isinstance(item,p.PCB_VIA):
        blocks.append(rect(item.GetBoundingBox()).buffer(.2))
area = box(50.6,50.6,50+spec['size_mm'][0]-.6,50+spec['size_mm'][1]-.6)
# The centre of a narrow body-to-body corridor need not fall on a 0.5mm
# grid. Include its exact coordinate so readable vertical references fit
# without requiring empty board strips solely for text.
corridor_x=set();corridor_y=set()
for first in blocks:
    a=first.bounds
    for second in blocks:
        c=second.bounds
        if 1.7<c[0]-a[2]<4: corridor_x.add((a[2]+c[0])/2)
        if 1.7<c[1]-a[3]<4: corridor_y.add((a[3]+c[1])/2)
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
    xs=sorted((x for x in corridor_x if abs(x-tx)<=10),key=lambda x:abs(x-tx))
    ys=sorted((y for y in corridor_y if abs(y-ty)<=10),key=lambda y:abs(y-ty))
    candidates += [(x,ty) for x in xs]+[(tx,y) for y in ys]+[(x,y) for x in xs[:32] for y in ys[:32]]
    candidates.sort(key=lambda v:(v[0]-tx)**2+(v[1]-ty)**2)
    obstacles = STRtree(blocks)
    placed = False
    for x,y in candidates:
        text.SetPosition(p.VECTOR2I(p.FromMM(x),p.FromMM(y)))
        # Vertical text uses narrow corridors beside upright axial packages
        # without shrinking the readable 0.9mm lettering or hiding it inside
        # component bodies. Try both orientations at each distance first.
        for angle in [0,90]:
            text.SetTextAngle(p.EDA_ANGLE(angle,p.DEGREES_T))
            shape = rect(text.GetBoundingBox()).buffer(.15)
            if area.contains(shape) and not len(obstacles.query(shape,predicate='intersects')):
                board.Add(text)
                fp.Reference().SetPosition(text.GetPosition())
                fp.Reference().SetTextAngle(text.GetTextAngle())
                blocks.append(shape)
                records.append({'reference':ref,'position_mm':[round(x,4),round(y,4)],'angle_degrees':angle})
                placed = True
                break
        if placed:break
    if not placed:
        raise AssertionError((folder.name,'No readable reference position',ref))
p.SaveBoard(str(file),board)
shutil.copyfile(folder/'verification/project-config.json',folder/(spec['name']+'.kicad_pro'))
(folder/'verification/component-silkscreen.json').write_text(json.dumps(records,indent=2)+'\n')
print(folder.name,len(records),'front component references')
