from pathlib import Path
import sexpdata as sx,json,gzip,hashlib
r=Path(__file__).resolve().parents[1];old=sx.loads(gzip.decompress((r/'design/rev2-layout-baseline.kicad_pcb.gz').read_bytes()).decode());new=sx.loads((r/'Ardor_IO.kicad_pcb').read_text());key=lambda v:str(v[0]) if isinstance(v,list) and v else ''
def get(v,name):return next((u for u in v if key(u)==name),None)
def fps(v):return{get(f,'property')[2]:f for f in v if key(f)=='footprint'}
a,b=fps(old),fps(new)
def geom(f):
 return [v for v in f if key(v) not in ['property','path','sheetname','sheetfile'] and key(v)!='pad']+[[z for z in v if key(z)!='net'] for v in f if key(v)=='pad']
assert all(geom(a[ref])==geom(f) for ref,f in b.items())
def tracks(v):return{get(t,'uuid')[1]:t for t in v if key(t) in ['segment','via']}
a,b=tracks(old),tracks(new)
assert all([z for z in a[u] if key(z)!='net']==[z for z in t if key(z)!='net'] for u,t in b.items() if u in a)
assert len(set(b)-set(a))==2
assert [v for v in old if key(v)=='gr_line']==[v for v in new if key(v)=='gr_line']
report={'all_retained_footprint_and_pad_geometry_unchanged':True,'all_retained_track_and_via_geometry_and_widths_unchanged':True,'outline_unchanged':True,'original_routing_items':len(a),'retained_routing_items':len(set(a)&set(b)),'new_buffer_bridge_segments':2,'removed_routing_items':len(set(a)-set(b)),'board_sha256':hashlib.sha256((r/'Ardor_IO.kicad_pcb').read_bytes()).hexdigest()}
(r/'verification/geometry-audit.json').write_text(json.dumps(report,indent=2)+'\n');print(report)
