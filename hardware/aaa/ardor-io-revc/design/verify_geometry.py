from pathlib import Path
import sexpdata as sx,json,gzip,hashlib,math
r=Path(__file__).resolve().parents[1];old=sx.loads(gzip.decompress((r/'design/rev2-layout-baseline.kicad_pcb.gz').read_bytes()).decode());new=sx.loads((r/'Ardor_IO.kicad_pcb').read_text());key=lambda v:str(v[0]) if isinstance(v,list) and v else ''
def get(v,name):return next((u for u in v if key(u)==name),None)
def fps(v):return{get(f,'property')[2]:f for f in v if key(f)=='footprint'}
a,b=fps(old),fps(new)
def geom(f):
 return [v for v in f if key(v) not in ['property','path','sheetname','sheetfile'] and key(v)!='pad']+[[z for z in v if key(z)!='net'] for v in f if key(v)=='pad']
assert all(geom(a[ref])==geom(f) for ref,f in b.items())
def tracks(v):return{get(t,'uuid')[1]:t for t in v if key(t) in ['segment','via']}
a,b=tracks(old),tracks(new)
width_plan=json.loads((r/'design/uniform-3v3-routing.json').read_text())
width_changes={change['uuid']:change for change in width_plan['changes']}
names={v[1]:v[2] for v in new if key(v)=='net'}
for uid,change in width_changes.items():
 assert uid in a and uid in b and key(b[uid])=='segment'
 assert names[get(b[uid],'net')[1]]==change['net'] and change['net'] in width_plan['nets']
 assert get(a[uid],'width')[1]==change['before_width_mm']
 assert get(b[uid],'width')[1]==width_plan['target_width_mm']
 assert all(get(a[uid],name)[1:]==change[name] for name in ['start','end'])
 assert get(a[uid],'layer')[1]==change['layer']
for uid,t in b.items():
 if uid not in a:continue
 ignored=['net','width'] if uid in width_changes else ['net']
 assert [z for z in a[uid] if key(z) not in ignored]==[z for z in t if key(z) not in ignored]
rails={name:[t for t in b.values() if key(t)=='segment' and names[get(t,'net')[1]]==name] for name in width_plan['nets']}
assert all(rails.values())
assert all(get(t,'width')[1]==width_plan['target_width_mm'] for ts in rails.values() for t in ts)
assert len(set(b)-set(a))==2
assert [v for v in old if key(v)=='gr_line']==[v for v in new if key(v)=='gr_line']
def zones(v):return {get(z,'uuid')[1]:[u for u in z if key(u) not in ['net','filled_polygon','fill_segments']] for z in v if key(z)=='zone'}
assert zones(old)==zones(new)
report={'all_retained_footprint_and_pad_geometry_unchanged':True,'all_retained_track_centerlines_and_via_geometry_unchanged':True,'track_widths_match_baseline_and_reviewed_3v3_plan':True,'reviewed_3v3_segments_narrowed':len(width_changes),'zone_outlines_settings_and_isolation_keepouts_unchanged':True,'outline_unchanged':True,'original_routing_items':len(a),'retained_routing_items':len(set(a)&set(b)),'new_buffer_bridge_segments':2,'removed_routing_items':len(set(a)-set(b)),'board_sha256':hashlib.sha256((r/'Ardor_IO.kicad_pcb').read_bytes()).hexdigest()}
(r/'verification/geometry-audit.json').write_text(json.dumps(report,indent=2)+'\n');print(report)
rail_report={'source_commit':width_plan['source_commit'],'target_width_mm':width_plan['target_width_mm'],'changed_segments':len(width_changes),'route_centerlines_and_vias_unchanged':True,'rails':{}}
for name,ts in rails.items():
 length=sum(math.dist(get(t,'start')[1:],get(t,'end')[1:]) for t in ts)
 # Sum every branch to give a conservative upper bound for track resistance.
 resistance=1.724e-8*length*1e-3/(width_plan['target_width_mm']*1e-3*35e-6)
 rail_report['rails'][name]={'segments':len(ts),'segments_narrowed':sum(change['net']==name for change in width_changes.values()),'distinct_track_widths_mm':sorted({get(t,'width')[1] for t in ts}),'total_track_length_mm':round(length,4),'assumed_copper_thickness_um':35,'sum_all_branch_track_resistances_ohm':round(resistance,5),'screening_current_ma':10,'track_voltage_drop_upper_bound_at_screening_current_mv':round(resistance*10,4),'vias_and_ferrite_excluded_from_resistance_estimate':True}
rail_report['board_sha256']=report['board_sha256']
(r/'verification/3v3-width-audit.json').write_text(json.dumps(rail_report,indent=2)+'\n');print(rail_report)
