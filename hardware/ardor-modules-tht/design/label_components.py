"""Place values/types on expression and MIDI; references on other modules."""
import wx
app = wx.App(False)
import pcbnew as p
import json
import sys
import shutil
from pathlib import Path
from shapely.geometry import box, Point
from shapely.strtree import STRtree
from shapely.ops import unary_union
from expression_labels import component_label as expression_label, SHARED_LABELS, SHARED_POSITIONS
from midi_labels import component_label as midi_label
import sexpdata as sx

ROOT = Path(__file__).resolve().parents[1]
folder = ROOT/sys.argv[1]
spec = json.loads((folder/'verification/design.json').read_text())
file = folder/(spec['name']+'.kicad_pcb')
value_labels = folder.name in ['expression','midi-in']
component_label = midi_label if folder.name=='midi-in' else expression_label
shared_labels = SHARED_LABELS if folder.name=='expression' else {}
shared_positions = SHARED_POSITIONS if folder.name=='expression' else {}
# Replace only labels owned by the preceding generated-label report.
if value_labels:
    report=folder/'verification/component-silkscreen.json'
    previous=json.loads(report.read_text()) if report.exists() else []
    data=sx.loads(file.read_text())
    def owned(item):
        if not isinstance(item,list) or str(item[0])!='gr_text':return False
        layer=next(v[1] for v in item if isinstance(v,list) and str(v[0])=='layer')
        if layer not in ['F.SilkS','F.Fab']:return False
        xy=next(v[1:3] for v in item if isinstance(v,list) and str(v[0])=='at')
        return any(item[1]==r.get('label',r['reference']) and max(abs(a-b) for a,b in zip(xy,r['position_mm']))<.0001 for r in previous)
    file.write_text(sx.dumps([item for item in data if not owned(item)])+'\n')
board = p.LoadBoard(str(file))
def rect(bb):
    return box(p.ToMM(bb.GetLeft()),p.ToMM(bb.GetTop()),p.ToMM(bb.GetRight()),p.ToMM(bb.GetBottom()))
blocks = []
body_blocks = {}
for fp in board.GetFootprints():
    body_blocks[fp.GetReference()] = []
    for pad in fp.Pads():
        blocks.append(rect(pad.GetBoundingBox()).buffer(.25))
    for item in fp.GraphicalItems():
        # Courtyards include empty lead space beyond the body. It is valid
        # to print a reference there if it clears pads and lead graphics;
        # reserve the actual Fab body and Silk shapes instead.
        if item.GetLayer()==p.F_SilkS or (item.GetLayer()==p.F_Fab and not isinstance(item,p.PCB_TEXT)):
            shape = rect(item.GetBoundingBox()).buffer(.15)
            # An unfilled silk outline occupies its strokes, not its interior.
            if value_labels and item.GetLayer()==p.F_SilkS and isinstance(item,p.PCB_SHAPE):
                margin = p.ToMM(item.GetWidth())/2+.15
                if item.GetShape()==p.SHAPE_T_RECT:
                    a,c=item.GetStart(),item.GetEnd()
                    shape=box(min(a.x,c.x)/1e6,min(a.y,c.y)/1e6,
                              max(a.x,c.x)/1e6,max(a.y,c.y)/1e6).boundary.buffer(margin)
                elif item.GetShape()==p.SHAPE_T_CIRCLE:
                    centre=item.GetCenter()
                    shape=Point(p.ToMM(centre.x),p.ToMM(centre.y)).buffer(p.ToMM(item.GetRadius())).boundary.buffer(margin)
            blocks.append(shape)
            if item.GetLayer()==p.F_Fab:
                body_blocks[fp.GetReference()].append(shape)
    if folder.name=='midi-in' and body_blocks[fp.GetReference()]:
        # A DIP body is drawn as separate edges. Reserve its complete envelope
        # so neighboring values cannot land inside the optocoupler outline.
        shape=unary_union(body_blocks[fp.GetReference()]).envelope
        blocks.append(shape);body_blocks[fp.GetReference()].append(shape)
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
for fp in sorted(board.GetFootprints(),key=lambda f:((0 if f.GetReference().startswith('J') else 1 if f.GetReference() in shared_labels else 2) if value_labels else 0,f.GetReference())):
    if fp.GetReference() not in spec['parts']:
        continue
    ref = fp.GetReference()
    if value_labels and ref in {r for refs in shared_labels.values() for r in refs}:continue
    label = component_label(ref,spec['parts'][ref]) if value_labels else ref
    target = fp.Reference().GetPosition()
    tx,ty = p.ToMM(target.x),p.ToMM(target.y)
    text = p.PCB_TEXT(board)
    text.SetText(label)
    text.SetLayer(p.F_SilkS)
    small_label = (folder.name=='expression' and ref in ['D303','D304','D305','D306']) or (folder.name=='midi-in' and ref in ['D201','D203','D204','C201','C204'])
    size = .8 if small_label else .9
    text.SetTextSize(p.VECTOR2I(p.FromMM(size),p.FromMM(size)))
    text.SetTextThickness(p.FromMM(.12 if small_label else .15))
    candidates = [(tx+dx/2,ty+dy/2) for dx in range(-20,21) for dy in range(-20,21)]
    xs=sorted((x for x in corridor_x if abs(x-tx)<=10),key=lambda x:abs(x-tx))
    ys=sorted((y for y in corridor_y if abs(y-ty)<=10),key=lambda y:abs(y-ty))
    candidates += [(x,ty) for x in xs]+[(tx,y) for y in ys]+[(x,y) for x in xs[:32] for y in ys[:32]]
    candidates.sort(key=lambda v:(v[0]-tx)**2+(v[1]-ty)**2)
    if value_labels and ref in shared_positions:
        candidates=[(50+shared_positions[ref][0],50+shared_positions[ref][1])]
    if folder.name=='midi-in' and ref in ['C201','C204']:
        candidates=[(85.1,72 if ref=='C201' else 77)]
    obstacles = STRtree(blocks)
    placed = False
    if value_labels and not ref.startswith('J') and ref not in shared_labels:
        # Dense boards may print a value/type inside its own body
        # outline (visible before fitting the part). Keep pads, silk, other
        # bodies and previous labels protected; assembly drawings retain it.
        own = {id(shape) for shape in body_blocks[ref]}
        under_body = STRtree([shape for shape in blocks if id(shape) not in own])
        body = max(body_blocks[ref],key=lambda shape:shape.area).centroid
        centre = (body.x,body.y)
        text.SetPosition(p.VECTOR2I(p.FromMM(centre[0]),p.FromMM(centre[1])))
        for angle in [0,90]:
            text.SetTextAngle(p.EDA_ANGLE(angle,p.DEGREES_T))
            shape = rect(text.GetBoundingBox()).buffer(.15)
            if area.contains(shape) and not len(under_body.query(shape,predicate='intersects')):
                candidates.insert(0,centre)
                obstacles = under_body
                break
    for x,y in candidates:
        text.SetPosition(p.VECTOR2I(p.FromMM(x),p.FromMM(y)))
        # Try both orientations in narrow corridors beside axial packages.
        for angle in [0,90]:
            text.SetTextAngle(p.EDA_ANGLE(angle,p.DEGREES_T))
            shape = rect(text.GetBoundingBox()).buffer(.15)
            if area.contains(shape) and not len(obstacles.query(shape,predicate='intersects')):
                board.Add(text)
                fp.Reference().SetPosition(text.GetPosition())
                fp.Reference().SetTextAngle(text.GetTextAngle())
                if value_labels and not ref.startswith('J'):
                    fp.Reference().SetVisible(False)
                    fab=p.PCB_TEXT(board);fab.SetText(label);fab.SetLayer(p.F_Fab)
                    fab.SetPosition(text.GetPosition());fab.SetTextAngle(text.GetTextAngle())
                    fab.SetTextSize(text.GetTextSize());fab.SetTextThickness(text.GetTextThickness());board.Add(fab)
                blocks.append(shape)
                record={'reference':ref,'label':label,'position_mm':[round(x,4),round(y,4)],'angle_degrees':angle,
                                'text_height_mm':size,
                                'covered_after_assembly':any(shape.intersects(body) for body in body_blocks[ref])}
                records.append(record)
                if value_labels and ref in shared_labels:
                    record['labels_components']=[ref,*shared_labels[ref]]
                    for shared in shared_labels[ref]:
                        board.FindFootprintByReference(shared).Reference().SetVisible(False)
                        records.append({**record,'reference':shared})
                placed = True
                break
        if placed:break
    if not placed:
        raise AssertionError((folder.name,'No readable component label position',ref))
p.SaveBoard(str(file),board)
shutil.copyfile(folder/'verification/project-config.json',folder/(spec['name']+'.kicad_pro'))
(folder/'verification/component-silkscreen.json').write_text(json.dumps(records,indent=2)+'\n')
print(folder.name,len(records),'components covered by front labels')
