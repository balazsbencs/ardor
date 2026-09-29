"""Read-only audit of source CAD against a fresh KiCad netlist; writes here only."""
from pathlib import Path
import sexpdata as sx
import xml.etree.ElementTree as ET
import json, hashlib, math, collections
P=Path(__file__).resolve().parents[1]; O=Path(__file__).resolve().parent
k=lambda x:str(x[0]) if isinstance(x,list) and x else ''
g=lambda x,n:next((v[1:] for v in x if k(v)==n),[])
b=sx.loads((P/'Ardor_IO.kicad_pcb').read_text())
r=ET.parse(O/'fresh-netlist.xml')
components={c.attrib['ref']:c for c in r.findall('.//components/comp')}
expected={(n.attrib['ref'],n.attrib['pin']):net.attrib['name'].replace('ALERT/RDY','ALERT{slash}RDY') for net in r.findall('.//nets/net') for n in net.findall('node')}
boardrefs={ref for ref,c in components.items() if c.findtext('footprint')}
actual={}; pads={}; attrs={}
for f in [x for x in b if k(x)=='footprint']:
 ref=next(v[2] for v in f if k(v)=='property' and v[1]=='Reference')
 at=g(f,'at'); angle=math.radians(at[2] if len(at)>2 else 0)
 attrs[ref]=g(f,'attr')
 for pad in [x for x in f if k(x)=='pad' and x[1]]:
  net=g(pad,'net'); net=net[1] if net else ''
  a=g(pad,'at'); pos=[at[0]+a[0]*math.cos(angle)+a[1]*math.sin(angle),at[1]-a[0]*math.sin(angle)+a[1]*math.cos(angle)]
  actual[(ref,str(pad[1]))]=net; pads[(ref,str(pad[1]))]=pos
want={key:n for key,n in expected.items() if key[0] in boardrefs}
assert actual==want, {'missing':set(want.items())-set(actual.items()),'extra':set(actual.items())-set(want.items())}
old={n.attrib['ref']+'.'+n.attrib['pin']:net.attrib['name'] for net in ET.parse(P/'verification/netlist.xml').findall('.//nets/net') for n in net.findall('node')}
fresh={ref+'.'+pin:n for (ref,pin),n in expected.items()}
# Account for KiCad's representation of slash inside unconnected pin names.
old={a:n.replace('ALERT/RDY','ALERT{slash}RDY') for a,n in old.items()}
assert old==fresh
segments=[x for x in b if k(x)=='segment']; netnames={x[1]:x[2] for x in b if k(x)=='net'}
lengths=collections.defaultdict(float); widths=collections.defaultdict(list)
for s in segments:
 net=netnames[g(s,'net')[0]]; a=g(s,'start'); c=g(s,'end')
 lengths[net]+=math.dist(a,c); widths[net].append(g(s,'width')[0])
near={}
for ref,pin,c,cp in [('U601','14','C603','1'),('U601','14','C604','1'),('U601','12','C605','1'),('U601','8','C606','1'),('U601','11','C607','1'),('U601','9','C607','2')]:
 near[f'{ref}.{pin} to {c}.{cp}']=round(math.dist(pads[(ref,pin)],pads[(c,cp)]),3)
report={'physical_components':len(boardrefs),'panel_components':len(components)-len(boardrefs),'numbered_board_pads':len(actual),'fresh_netlist_matches_all_board_pads':True,'saved_schematic_netlist_matches_fresh':True,'chassis_pins':[f'{a}.{p}' for (a,p),n in actual.items() if n=='CHASSIS'],'chassis_total_track_length_mm':round(lengths['CHASSIS'],3),'chassis_width_range_mm':[min(widths['CHASSIS']),max(widths['CHASSIS'])],'headphone_cap_pad_straight_line_distances_mm':near,'mounting_hole_attributes':{a:[str(v) for v in n] for a,n in attrs.items() if a.startswith('H')},'net_lengths_mm':{a:round(n,3) for a,n in lengths.items()}}
(O/'source-audit.json').write_text(json.dumps(report,indent=2)+'\n')
files=sorted(list(P.glob('*.kicad_sch'))+list((P/'footprints').rglob('*.kicad_mod'))+[P/n for n in ['Ardor_IO.kicad_pcb','Ardor_IO.kicad_pro','Ardor.kicad_sym','sym-lib-table','fp-lib-table','BOM.csv','design/expected_nets.json']])
(O/'source-sha256.txt').write_text(''.join(hashlib.sha256(f.read_bytes()).hexdigest()+'  ../'+str(f.relative_to(P))+'\n' for f in files))
print(json.dumps({a:n for a,n in report.items() if a!='net_lengths_mm'},indent=2))
