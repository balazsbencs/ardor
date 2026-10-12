"""Independently verify compact board outlines and assembly body clearance.

Use the actual embedded courtyards, including the complete ADC module body
and maximum Nichicon can size. Native DRC additionally checks copper, holes,
silkscreen and the MIDI functional-isolation rule.
"""
from pathlib import Path
import json
import itertools
import pcbnew as p
from shapely.geometry import Polygon, box

ROOT = Path(__file__).resolve().parents[1]
ORIGINAL = {'midi-in': [85,70], 'expression': [110,85], 'mixer': [100,75],
            'line-out': [115,85], 'headphones': [96,84]}
report = {}
for slug,old in ORIGINAL.items():
    folder=ROOT/slug
    spec=json.loads((folder/'verification/design.json').read_text())
    width,height=spec['size_mm']
    board=p.LoadBoard(str(folder/(spec['name']+'.kicad_pcb')))
    permitted=box(50.25,50.25,50+width-.25,50+height-.25)
    shapes={}
    for fp in board.GetFootprints():
        fp.BuildCourtyardCaches()
        courtyard=fp.GetCourtyard(p.F_CrtYd)
        assert courtyard.OutlineCount()==1,(slug,fp.GetReference(),'missing/ambiguous assembly courtyard')
        chain=courtyard.COutline(0)
        shape=Polygon([(p.ToMM(chain.CPoint(i).x),p.ToMM(chain.CPoint(i).y))
                       for i in range(chain.PointCount())])
        assert shape.is_valid and permitted.covers(shape),(slug,fp.GetReference(),'body reservation crosses edge')
        shapes[fp.GetReference()]=shape
    pairs=list(itertools.combinations(shapes.items(),2))
    for (a,first),(b,second) in pairs:
        assert first.intersection(second).area<1e-5,(slug,a,b,'assembly courtyard overlap')
    new_area=width*height
    reduction=1-new_area/(old[0]*old[1])
    assert reduction>.40,(slug,'size reduction insufficient')
    result={'result':'PASS','previous_size_mm':old,'size_mm':[width,height],
            'previous_area_mm2':old[0]*old[1],'area_mm2':new_area,
            'area_reduction_percent':round(100*reduction,2),
            'embedded_courtyards_checked':len(shapes),'courtyard_overlaps':0,
            'courtyards_inside_outline_with_0_25mm_margin':True,
            'minimum_courtyard_gap_mm':round(min(a.distance(b) for (_,a),(_,b) in pairs),4),
            'assembly_courtyard_area_fraction':round(sum(a.area for a in shapes.values())/new_area,4),
            'hardware_tested':False}
    (folder/'verification/compact-layout.json').write_text(json.dumps(result,indent=2)+'\n')
    report[slug]=result
    print(slug,'compact body/outline PASS:',width,'x',height,'mm;',result['area_reduction_percent'],'% less area')
summary={'result':'PASS','reviewed_revision':'T1.1','modules':report,
         'total_previous_area_mm2':sum(v['previous_area_mm2'] for v in report.values()),
         'total_area_mm2':sum(v['area_mm2'] for v in report.values()),'hardware_tested':False}
(ROOT/'review/compact-layouts.json').write_text(json.dumps(summary,indent=2)+'\n')
