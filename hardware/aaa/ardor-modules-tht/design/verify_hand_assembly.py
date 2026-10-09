"""Check the actual assembly/package contract, independently of the generator."""
import csv
import json
from pathlib import Path
import pcbnew as p
import sexpdata as sx
ROOT = Path(__file__).resolve().parents[1]
def key(x):
    return str(x[0]) if isinstance(x,list) and x else ''
def get(x,name):
    return next((v for v in x if key(v)==name),None)
for folder in ROOT.iterdir():
    if not (folder/'verification/design.json').exists():
        continue
    spec = json.loads((folder/'verification/design.json').read_text())
    board = p.LoadBoard(str(folder/(spec['name']+'.kicad_pcb')))
    original = json.loads((ROOT/'design/m1-interface-baseline.json').read_text())['pins_by_module'][folder.name]
    connectors = {pin:net for pin,net in spec['pins'].items() if pin.startswith('J')}
    assert connectors == original
    bom = {row['Reference']:row for row in csv.DictReader((folder/'BOM.csv').open())}
    assert set(bom)==set(spec['parts'])
    footprints = {f.GetReference():f for f in board.GetFootprints()}
    pad_count = 0
    smd_count = 0
    smd_refs = []
    low_voltage_tvs = {'midi-in':['D202'], 'expression':['D301','D302'],
                       'line-out':['D501'], 'headphones':['D601','D602'], 'mixer':[]}[folder.name]
    for ref,part in spec['parts'].items():
        fp = footprints[ref]
        is_smd = folder.name=='headphones' and ref=='U601'
        if is_smd:
            assert fp.GetAttributes() & p.FP_SMD
            assert not fp.GetAttributes() & p.FP_THROUGH_HOLE
            assert part['fp']=='Package_SO:SOIC-8_3.9x4.9mm_P1.27mm'
            assert part['mpn']=='Texas Instruments OPA1656ID'
            assert bom[ref]['Assembly']=='SMD manual / SOIC-8 1.27mm pitch'
            smd_refs.append(ref)
        else:
            assert fp.GetAttributes() & p.FP_THROUGH_HOLE, (folder.name,ref,'not THT')
            assert not fp.GetAttributes() & p.FP_SMD
        for pad in fp.Pads():
            if pad.GetNumber():
                if is_smd:
                    assert pad.GetAttribute()==p.PAD_ATTRIB_SMD
                    assert pad.GetDrillSize().x==0 and pad.IsOnLayer(p.F_Cu)
                    assert not pad.IsOnLayer(p.B_Cu)
                    smd_count += 1
                else:
                    assert pad.GetAttribute()==p.PAD_ATTRIB_PTH
                    assert p.ToMM(pad.GetDrillSize().x)>=.6
                    assert pad.IsOnLayer(p.F_Cu) and pad.IsOnLayer(p.B_Cu)
                    pad_count += 1
        assert bom[ref]['Footprint']==part['fp']
        assert bom[ref]['Value']==part['value']
        assert part['mpn']==bom[ref]['Purchasing specification']
        assert not part['part'] or not part['part']['jlcpcb_part']
        # Check that each embedded package has all pads from its local library.
        alias,name = part['fp'].split(':')
        local = p.FootprintLoad(str(folder/'footprints'/(alias+'.pretty')),name)
        assert local and local.GetPadCount()==fp.GetPadCount()
        if ref in low_voltage_tvs:
            # Independent purchasing/package limits from the DC Components
            # drawing: 9.5 x 5.6 mm maximum body; 1.07 mm maximum leads.
            assert part['value']=='1.5KE6.8CA'
            assert part['mpn']=='DC Components 1.5KE6.8CA (HESTORE 100.430.71)'
            assert part['fp']=='Ardor_THT:D_TVS_DC_1.5KE_P15.24mm'
            pads = list(fp.Pads())
            assert len(pads)==2 and {a.GetNumber() for a in pads}=={'1','2'}
            assert abs(p.ToMM((pads[0].GetPosition()-pads[1].GetPosition()).EuclideanNorm())-15.24)<1e-6
            assert all(p.ToMM(a.GetDrillSize().x)>=1.3 for a in pads)
            tree = sx.loads((folder/'footprints'/(alias+'.pretty')/(name+'.kicad_mod')).read_text())
            rect = next(v for v in tree if key(v)=='fp_rect' and get(v,'layer')[1]=='F.Fab')
            start,end = get(rect,'start')[1:],get(rect,'end')[1:]
            assert abs(end[0]-start[0])>=9.5 and abs(end[1]-start[1])>=5.6
            assert not any(key(v)=='fp_text' and v[2]=='K' for v in tree)
        if folder.name=='midi-in' and ref in ['D203','D204']:
            assert part['value']=='SA24CA-E3/54'
            assert part['fp']=='Diode_THT:D_DO-15_P10.16mm_Horizontal'
    erc = json.loads((folder/'verification/erc.json').read_text())
    assert not [v for sheet in erc['sheets'] for v in sheet['violations']]
    if folder.name=='expression':
        fp = footprints['U301']
        expected = {'8':(-6.35,6.35),'3':(-3.81,6.35),'10':(-1.27,6.35),'9':(1.27,6.35),
                    '1':(3.81,6.35),'2':(6.35,6.35),'11':(-6.35,-6.35),'7':(-3.81,-6.35),
                    '6':(-1.27,-6.35),'5':(1.27,-6.35),'4':(3.81,-6.35),'12':(6.35,-6.35)}
        origin = fp.GetPosition()
        for pad in fp.Pads():
            delta = pad.GetPosition()-origin
            assert (round(p.ToMM(delta.x),2),round(p.ToMM(delta.y),2))==expected[pad.GetNumber()]
        assert set(spec['nc'])=={'U301.2','U301.11','U301.12'}
        assert spec['parts']['U301']['mpn']=='Adafruit 1085 (STEMMA QT revision)'
    if folder.name in ['line-out','headphones']:
        ref = 'Q501' if folder.name=='line-out' else 'Q601'
        assert spec['parts'][ref]['mpn']=='onsemi 2N3904BU'
        assert spec['pins'][ref+'.1']=='GND'
        assert spec['pins'][ref+'.3']=='RELAY_LOW'
    if folder.name=='headphones':
        assert spec['pins']['K601.4']=='HP_L' and spec['pins']['K601.13']=='HP_R'
        assert spec['pins']['K601.6']=='GND' and spec['pins']['K601.11']=='GND'
        assert spec['pins']['K601.8']=='DRIVE_L' and spec['pins']['K601.9']=='DRIVE_R'
        assert not spec['nc'] and 'U602' not in footprints
        assert smd_count==8 and smd_refs==['U601']
        pads = {a.GetNumber():a for a in footprints['U601'].Pads()}
        assert set(pads)==set('12345678')
        for a,b in [('1','2'),('2','3'),('3','4'),('5','6'),('6','7'),('7','8')]:
            assert abs(p.ToMM((pads[a].GetPosition()-pads[b].GetPosition()).y))==1.27
        assert spec['pins']['U601.8']=='+5V_A' and spec['pins']['U601.4']=='GND'
        assert spec['pins']['U601.3']==spec['pins']['U601.5']=='VREF'
        assert spec['pins']['U601.2']=='SUM_L' and spec['pins']['U601.6']=='SUM_R'
        assert spec['pins']['U601.1']=='RAW_L' and spec['pins']['U601.7']=='RAW_R'
        assert spec['parts']['K601']['mpn']=='Omron G5V-2-H1 DC5'
    silk = json.loads((folder/'verification/component-silkscreen.json').read_text())
    assert {v['reference'] for v in silk}==set(spec['parts'])
    labels = json.loads((folder/'verification/silkscreen.json').read_text())
    assert set(labels['numbered_connector_pins'])==set(connectors)
    assert set(labels['pin_maps'])=={pin.split('.')[0] for pin in connectors}
    report = {'result':'PASS','board_smd_pads':smd_count,'board_smd_footprints':len(smd_refs),'manual_smd_references':smd_refs,'tht_numbered_pads':pad_count,
              'minimum_component_drill_mm':.6,'manual_parts':len(bom),'all_manual_parts_have_front_references':True,
              'hestore_1_5ke6_8ca_references':low_voltage_tvs,
              'retained_sa24ca_references':['D203','D204'] if folder.name=='midi-in' else [],
              'host_and_panel_pinout_matches_m1':True,'preassembled_smd_module':
              'Adafruit 1085 STEMMA QT ADS1115' if folder.name=='expression' else None,
              'breakout_contains_smd':folder.name=='expression','hardware_tested':False}
    (folder/'verification/hand-assembly.json').write_text(json.dumps(report,indent=2)+'\n')
    print(folder.name,'hand assembly and interface checks PASS',pad_count,'THT pads',smd_count,'SOIC pads')
