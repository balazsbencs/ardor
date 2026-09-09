"""Audit every schematic pad/net, DRC result, and compact-board geometry."""
from pathlib import Path
import collections,json,xml.etree.ElementTree as ET
import pcbnew as p
ROOT=Path(__file__).resolve().parents[1];b=p.LoadBoard(str(ROOT/'Ardor_IO.kicad_pcb'))
xml=ET.parse(ROOT/'verification/pcb-source-netlist.xml')
refs={c.attrib['ref'] for c in xml.findall('.//components/comp') if c.findtext('footprint')}
expected={}
for net in xml.findall('.//nets/net'):
 name=net.attrib['name'].replace('ALERT/RDY','ALERT{slash}RDY')
 for node in net.findall('node'):
  if node.attrib['ref'] in refs:expected[(node.attrib['ref'],node.attrib['pin'])]=name
actual={};fps={f.GetReference():f for f in b.GetFootprints()}
for ref,f in fps.items():
 if ref not in refs:continue
 for pad in f.Pads():
  if pad.GetNumber():
   key=(ref,pad.GetNumber());value=pad.GetNetname()
   if key in actual:assert actual[key]==value
   actual[key]=value
assert actual==expected,{'missing':set(expected.items())-set(actual.items()),'extra':set(actual.items())-set(expected.items())}
assert set(fps)==refs|{'H1','H2','H3','H4'}
r=json.loads((ROOT/'routing/drc.json').read_text())
for key in ['violations','unconnected_items','schematic_parity']:assert not r[key],key
edges=[d for d in b.GetDrawings() if d.GetLayer()==p.Edge_Cuts]
points=[v for e in edges for v in [e.GetStart(),e.GetEnd()]]
size=[round(p.ToMM(max(v.x for v in points)-min(v.x for v in points)),2),round(p.ToMM(max(v.y for v in points)-min(v.y for v in points)),2)]
assert size==[68,46],size
# All component courtyards must fit the outline and remain on the assembly side.
mask=p.LSET();mask.AddLayer(p.F_CrtYd);bounds={}
for ref,f in fps.items():
 assert not f.IsFlipped(),ref
 f.BuildCourtyardCaches();box=f.GetLayerBoundingBox(mask)
 edges_mm=[round(p.ToMM(v)-50,3) for v in [box.GetLeft(),box.GetTop(),box.GetRight(),box.GetBottom()]]
 assert 0<=edges_mm[0]<edges_mm[2]<=68 and 0<=edges_mm[1]<edges_mm[3]<=46,(ref,edges_mm)
 bounds[ref]=edges_mm
(ROOT/'routing/placement-bounds.json').write_text(json.dumps(dict(sorted(bounds.items())),indent=2)+'\n')
tracks=[t for t in b.GetTracks() if not isinstance(t,p.PCB_VIA)];vias=[t for t in b.GetTracks() if isinstance(t,p.PCB_VIA)]
report={'board_mm':size,'original_board_mm':[100,80],'area_reduction_percent':60.9,'previous_board_mm':[90,64],'area_reduction_from_previous_percent':round(100*(1-68*46/(90*64)),2),'schematic_components':len(refs),'mounting_holes':4,'all_courtyards_inside_outline':True,'all_components_on_front':True,'pad_net_assignments_checked':len(expected),'netlist_match':True,'drc_violations':0,'unconnected_items':0,'schematic_parity_issues':0,'track_segments':len(tracks),'vias':len(vias),'track_widths_mm':dict(sorted(collections.Counter(round(p.ToMM(t.GetWidth()),3) for t in tracks).items())),'kicad_version':p.Version()}
(ROOT/'routing/connectivity-audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
