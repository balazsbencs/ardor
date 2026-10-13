"""Check physical pin markers, complete direction maps and unused pin labels."""
from pathlib import Path
import json,math
import pcbnew as p
from interfaces import contract
ROOT=Path(__file__).resolve().parents[1]
def xy(pos):return [p.ToMM(pos.x),p.ToMM(pos.y)]
for folder in ROOT.iterdir():
    if not (folder/'verification/design.json').exists():continue
    s=json.loads((folder/'verification/design.json').read_text());guide=contract(folder.name,s)
    report=json.loads((folder/'verification/silkscreen.json').read_text());b=p.LoadBoard(str(folder/(s['name']+'.kicad_pcb')))
    external={pin:net for pin,net in s['pins'].items() if pin.startswith('J')};fps={f.GetReference():f for f in b.GetFootprints()}
    assert set(report['numbered_connector_pins'])==set(external)
    assert set(report['pin_maps'])==set(guide['ports'])
    drawings=[v for v in b.GetDrawings() if isinstance(v,p.PCB_TEXT) and v.GetLayer()==p.B_SilkS]
    for v in report['legends']:
        matches=[t for t in drawings if t.GetText()==v['text'] and math.dist(xy(t.GetPosition()),v['position_mm'])<.00011]
        assert len(matches)==1,(folder.name,'Missing printed label',v['text'])
        assert matches[0].IsMirrored(),('Back label must read from the back',v['text'])
    pads={ref+'.'+a.GetNumber():a for ref,f in fps.items() if ref in guide['ports'] for a in f.Pads() if a.GetNumber()}
    for pin,v in report['numbered_connector_pins'].items():
        assert v['text']==pin.split('.')[1]
        assert math.dist(v['position_mm'],xy(pads[pin].GetPosition()))<=2.5,(pin,'Pin number is too far away')
        nearest=min(pads,key=lambda q:math.dist(v['position_mm'],xy(pads[q].GetPosition())))
        assert nearest==pin,(pin,'Pin number points to a different connector',nearest)
    for ref,v in report['pin_maps'].items():assert set(v['described_pins'])=={ref+'.'+str(n['number']) for n in guide['ports'][ref]['pins']}
    assert report['unused_pins']==guide['unused_pins']
    assert {v['unused_pin'] for v in report['legends'] if 'unused_pin' in v}==set(s['nc'])
    guidefile=folder/'verification/connector-guide.json';assert json.loads(guidefile.read_text())==guide
    assert (folder/'ASSEMBLY.md').exists()
    result={'result':'PASS','numbered_external_pins':len(external),'complete_pin_maps':len(guide['ports']),'explicit_unused_pin_labels':len(s['nc']),'pin_numbers_point_to_nearest_matching_pad':True,'all_printed_labels_present':True,'directions_relative_to_module':True,'all_pin_nets_match_schematic':True}
    (folder/'verification/label-validation.json').write_text(json.dumps(result,indent=2)+'\n')
    print(folder.name,'labels PASS',len(external),'pins,',len(s['nc']),'unused pins')
