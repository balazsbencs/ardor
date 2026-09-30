"""Derive Rev C from the frozen Rev 2 layout using KiCad's s-expression format.
Preserves every retained pad/footprint and track centerline. Reassigns nets from
the fresh schematic, applies reviewed widths, removes stubs, and clears old fills.
Run refill_board.py afterwards in a separate KiCad process.
"""
from pathlib import Path
import gzip,xml.etree.ElementTree as ET,json,uuid
import sexpdata as sx
R=Path(__file__).resolve().parents[1];S=sx.Symbol
key=lambda v:str(v[0]) if isinstance(v,list) and v else ''
def get(v,name):return next((u for u in v if key(u)==name),None)
source=sx.loads(gzip.decompress((R/'design/rev2-layout-baseline.kicad_pcb.gz').read_bytes()).decode())
x=ET.parse(R/'verification/netlist.xml')
comps={c.attrib['ref']:c for c in x.findall('.//components/comp') if c.findtext('footprint')}
pins={(n.attrib['ref'],n.attrib['pin']):net.attrib['name'].replace('ALERT/RDY','ALERT{slash}RDY') for net in x.findall('.//nets/net') for n in net.findall('node')}
fps={get(f,'property')[2]:f for f in source if key(f)=='footprint'}
oldnames={v[1]:v[2] for v in source if key(v)=='net'}
merge={}
for ref,f in fps.items():
 if ref not in comps:continue
 for pad in f:
  if key(pad)=='pad' and get(pad,'net'):
   old=get(pad,'net')[2];new=pins[(ref,pad[1])]
   if old!=new:
    assert old not in merge or merge[old]==new,(old,new)
    merge[old]=new
# Reuse old codes where possible; newly localised labels get new canonical codes.
canonical={name:code for code,name in oldnames.items() if name not in merge}
for name in merge.values():
 if name not in canonical:canonical[name]=max(canonical.values(),default=0)+1
netcode={oldcode:canonical[merge.get(oldname,oldname)] for oldcode,oldname in oldnames.items()}
removed=[];kept=[]
used=set(pins.values())
pruned=set(json.loads((R/'design/pruned-track-uuids.json').read_text()))
ground_plan=json.loads((R/'design/ground-plane-routing.json').read_text())
ground_pruned=set(ground_plan['removed_segment_uuids'])
ground_candidates={get(t,'uuid')[1]:t for t in source if key(t)=='segment'}
assert all(u in ground_candidates and oldnames[get(ground_candidates[u],'net')[1]]=='GND' for u in ground_pruned)
width_plan=json.loads((R/'design/uniform-3v3-routing.json').read_text())
width_changes={change['uuid']:change for change in width_plan['changes']}
assert len(width_changes)==len(width_plan['changes'])
for uid,change in width_changes.items():
 t=ground_candidates[uid]
 assert oldnames[get(t,'net')[1]]==change['net'] and change['net'] in width_plan['nets']
 assert get(t,'width')[1]==change['before_width_mm']
 assert all(get(t,name)[1:]==change[name] for name in ['start','end'])
 assert get(t,'layer')[1]==change['layer']
for item in source:
 kind=key(item)
 if kind=='net':continue
 if kind=='footprint':
  ref=get(item,'property')[2]
  if ref not in comps and not ref.startswith('H'):
   removed.append(ref);continue
  if ref in comps:
   c=comps[ref];props={p[1]:p for p in item if key(p)=='property'}
   props['Value'][2]=c.findtext('value')
   for name in ['MPN','Datasheet','JLCPCB Part #']:
    if name in props:props[name][2]=c.findtext('datasheet','') if name=='Datasheet' else c.findtext("fields/field[@name='"+name+"']",'')
   get(item,'path')[1]=c.find('sheetpath').attrib['tstamps']+c.findtext('tstamps').split()[0]
   for name,xmlname in [('sheetname','Sheetname'),('sheetfile','Sheetfile')]:
    value=next((p.attrib['value'] for p in c.findall('property') if p.attrib['name']==xmlname),'')
    if get(item,name):get(item,name)[1]=value
    elif value:item.append([S(name),value])
   for pad in item:
    if key(pad)=='pad' and get(pad,'net'):
     name=pins[(ref,pad[1])];get(pad,'net')[:]=[S('net'),canonical[name],name]
 if kind in ['segment','via']:
  net=get(item,'net');name=merge.get(oldnames[net[1]],oldnames[net[1]])
  if name not in used or get(item,'uuid')[1] in pruned|ground_pruned:continue
  net[1]=canonical[name]
  if kind=='segment' and get(item,'uuid')[1] in width_changes:
   get(item,'width')[1]=width_plan['target_width_mm']
 if kind=='zone':
  n=get(item,'net');n[1]=netcode.get(n[1],0)
  item[:]=[v for v in item if key(v) not in ['filled_polygon','fill_segments']]
 if kind=='gr_text':
  text=item[1];at=get(item,'at')
  if any(t in text for t in ['HEADPHONE','HP EN','HP L','PHONES']) or (text in ['L','R','G'] and at[1]==117 and at[2]>80):continue
  if 'ARDOR' in text:item[1]='ARDOR IO / REV C'
 if kind=='title_block':
  get(item,'rev')[1]='C';get(item,'title')[1]='Ardor IO Rev C - budget SMT, MIDI and expression'
 kept.append(item)
# Empty old U501 courtyard: bridge its feedback nets to the shared mono input.
for start,end in [((97.025,72.365),(97.025,73.635)),((101.975,73.635),(101.975,74.905))]:
 kept.append([S('segment'),[S('start'),*start],[S('end'),*end],[S('width'),.25],[S('layer'),'F.Cu'],[S('net'),canonical['MONO_BUF']],[S('uuid'),str(uuid.uuid4())]])
index=next(i for i,v in enumerate(kept) if key(v)=='footprint')
kept[index:index]=[[S('net'),code,name] for name,code in sorted(canonical.items(),key=lambda n:n[1])]
(R/'Ardor_IO.kicad_pcb').write_text(sx.dumps(kept)+'\n')
(R/'verification/layout-changes.json').write_text(json.dumps({'removed_footprints':sorted(removed),'net_merges':merge,'buffer_bypass_connections':[['U501 old pad 2','U501 old pad 3'],['U501 old pad 6','U501 old pad 5']],'retained_outline_mm':[68,46],'reviewed_branch_stubs_removed':len(pruned),'redundant_ground_segments_removed':len(ground_pruned),'uniform_3v3_width_mm':width_plan['target_width_mm'],'reviewed_3v3_segments_narrowed':len(width_changes)},indent=2)+'\n')
print('Rev C board:',len(comps),'electrical footprints; removed',len(removed))
