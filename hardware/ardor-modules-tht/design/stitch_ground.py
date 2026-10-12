"""Connect ground-pour islands to the host ground through the other plane.

Derives actual copper polygons, not pad-centre guesses. Proposed vias must fit
inside filled copper on both layers, clear existing drill holes, and bridge to
the host-ground component. Final KiCad DRC independently validates the result.
Run in a fresh wx-first process after importing/refilling the signal candidate.
"""
import wx
app=wx.App(False)
import pcbnew as p
from pathlib import Path
import json, math, sys, shutil
import sexpdata as sx
from shapely.geometry import Polygon, Point
from shapely.ops import unary_union

ROOT=Path(__file__).resolve().parents[1];folder=ROOT/sys.argv[1]
spec=json.loads((folder/'verification/design.json').read_text());file=folder/(spec['name']+'.kicad_pcb')
key=lambda v:str(v[0]) if isinstance(v,list) and v else ''
get=lambda v,k:next((x for x in v if key(x)==k),None)
data=sx.loads(file.read_text());removed=[]
small=[];kept=[]
for t in data:
    if key(t)=='segment':
        a,c=get(t,'start')[1:],get(t,'end')[1:]
        if math.dist(a,c)<.001:small.append(t);continue
    kept.append(t)
data=kept
file.write_text(sx.dumps(data)+'\n')
b=p.LoadBoard(str(file));ground=b.FindNet('/GND')
assert ground
layers=[p.F_Cu,p.B_Cu];items=[];holes=[]
def polygons(poly):
    for i in range(poly.OutlineCount()):
        def points(chain):return [(p.ToMM(chain.CPoint(j).x),p.ToMM(chain.CPoint(j).y)) for j in range(chain.PointCount())]
        shape=Polygon(points(poly.COutline(i)),[points(poly.CHole(i,j)) for j in range(poly.HoleCount(i))])
        if not shape.is_valid:shape=shape.buffer(0)
        if not shape.is_empty:yield shape
def add(name,shape,layer,zone=False):items.append({'name':name,'shape':shape,'layer':layer,'zone':zone})
for z in b.Zones():
    if z.GetIsRuleArea():continue
    assert z.GetNetname()=='/GND'
    for i,shape in enumerate(polygons(z.GetFilledPolysList(z.GetLayer()))):add('zone:'+str(z.GetLayer())+':'+str(i),shape,z.GetLayer(),True)
for f in b.GetFootprints():
    for i,a in enumerate(f.Pads()):
        pos=a.GetPosition();drill=a.GetDrillSize()
        if drill.x:holes.append(Point(p.ToMM(pos.x),p.ToMM(pos.y)).buffer(p.ToMM(max(drill.x,drill.y))/2+.15+.25))
        if a.GetNetname()!='/GND':continue
        for layer in layers:
            if not a.IsOnLayer(layer):continue
            shape=p.SHAPE_POLY_SET();a.TransformShapeToPolygon(shape,layer,0,p.FromMM(.003),p.ERROR_INSIDE)
            for poly in polygons(shape):add(f.GetReference()+'.'+a.GetNumber()+':'+str(i),poly,layer)
for t in b.GetTracks():
    if not isinstance(t,p.PCB_VIA):
        assert t.GetNetname()!='/GND','Ground is provided by pours and stitching, not redundant tracks'
        continue
    pos=t.GetPosition();centre=Point(p.ToMM(pos.x),p.ToMM(pos.y));holes.append(centre.buffer(p.ToMM(t.GetDrill())/2+.15+.25))
    if t.GetNetname()=='/GND':
        for layer in layers:add('via:'+str(t.m_Uuid),centre.buffer(p.ToMM(t.GetWidth(layer))/2),layer)
parents=list(range(len(items)))
def root(n):
    if parents[n]!=n:parents[n]=root(parents[n])
    return parents[n]
def union(a,c):parents[root(a)]=root(c)
for i,a in enumerate(items):
    for j,c in enumerate(items[:i]):
        if a['name']==c['name'] or (a['layer']==c['layer'] and a['shape'].intersects(c['shape'])):union(i,j)
hostpin=next(pin for pin,net in spec['pins'].items() if pin.startswith('J101.') and net=='GND')
host=root(next(i for i,v in enumerate(items) if v['name'].startswith(hostpin+':')))
groups={}
for i,item in enumerate(items):groups.setdefault(root(i),[]).append(item)
assert host in groups
main={layer:unary_union([v['shape'] for v in groups[host] if v['zone'] and v['layer']==layer]) for layer in layers}
avoid=unary_union(holes);stitches=[]
for component,group in groups.items():
    if component==host:continue
    candidates=[]
    for layer in layers:
        current=unary_union([v['shape'] for v in group if v['zone'] and v['layer']==layer])
        opposite=main[layers[1] if layer==layers[0] else layers[0]]
        region=current.buffer(-.35).intersection(opposite.buffer(-.35)).difference(avoid)
        if not region.is_empty:candidates.append(region)
    assert candidates,(folder.name,'Ground island needs manual bridge',[v['name'] for v in group])
    region=max(candidates,key=lambda v:v.area);point=region.representative_point();xy=[round(point.x,4),round(point.y,4)]
    assert region.covers(Point(*xy))
    via=p.PCB_VIA(b);via.SetPosition(p.VECTOR2I(p.FromMM(xy[0]),p.FromMM(xy[1])));via.SetWidth(p.F_Cu,p.FromMM(.6));via.SetWidth(p.B_Cu,p.FromMM(.6));via.SetDrill(p.FromMM(.3));via.SetViaType(p.VIATYPE_THROUGH);via.SetLayerPair(p.F_Cu,p.B_Cu);via.SetNet(ground);b.Add(via)
    stitches.append({'position_mm':xy,'connects':[v['name'] for v in group]})
for z in b.Zones():
    if not z.GetIsRuleArea():z.UnFill()
b.BuildConnectivity();assert p.ZONE_FILLER(b).Fill(b.Zones());p.SaveBoard(str(file),b)
shutil.copyfile(folder/'verification/project-config.json',folder/(spec['name']+'.kicad_pro'))
(folder/'verification/ground-stitching.json').write_text(json.dumps({'ground_components_before':len(groups),'host_ground':hostpin,'added_vias':stitches,'removed_router_spurs':len(removed),'removed_degenerate_segments':len(small)},indent=2)+'\n')
print(folder.name,len(stitches),'ground stitches',len(removed),'spurs removed')
