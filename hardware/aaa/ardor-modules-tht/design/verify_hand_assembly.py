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
        is_smd = (folder.name=='headphones' and ref=='U601') or (folder.name=='midi-in' and ref in ['D203','D204'])
        if is_smd:
            assert fp.GetAttributes() & p.FP_SMD
            assert not fp.GetAttributes() & p.FP_THROUGH_HOLE
            if folder.name=='headphones':
                assert part['fp']=='Package_SO:SOIC-8_3.9x4.9mm_P1.27mm'
                assert part['mpn']=='Texas Instruments OPA1656ID'
                assert bom[ref]['Assembly']=='SMD manual / SOIC-8 1.27mm pitch'
            else:
                assert part['fp']=='Ardor_THT:D_SMC_Bidirectional_Handsolder'
                assert part['mpn']=='Littelfuse SMDJ24CA (HESTORE 100.461.56)'
                assert bom[ref]['Assembly']=='SMD manual / SMC large two-terminal package'
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
            assert part['value']=='SMDJ24CA'
            pads=list(fp.Pads())
            assert {a.GetNumber() for a in pads}=={'1','2'}
            assert abs(p.ToMM((pads[0].GetPosition()-pads[1].GetPosition()).EuclideanNorm())-6.8)<1e-6
            assert all((p.ToMM(a.GetSize().x),p.ToMM(a.GetSize().y))==(3.3,2.5) for a in pads)
            tree=sx.loads((folder/'footprints'/(alias+'.pretty')/(name+'.kicad_mod')).read_text())
            rect=next(v for v in tree if key(v)=='fp_rect' and get(v,'layer')[1]=='F.Fab')
            start,end=get(rect,'start')[1:],get(rect,'end')[1:]
            assert end[0]-start[0]>=7.11 and end[1]-start[1]>=6.22
            assert not any(key(v)=='fp_text' and v[2]=='K' for v in tree)
        if 'BIPOLAR' in part['value']:
            assert part['value']=='10u / 50V BIPOLAR'
            assert part['mpn']=='Nichicon UES1H100MPM (HESTORE 100.524.69)'
            assert part['fp']=='Ardor_THT:CP_Bipolar_Nichicon_D8.0mm_P3.50mm'
            pads=list(fp.Pads())
            assert abs(p.ToMM((pads[0].GetPosition()-pads[1].GetPosition()).EuclideanNorm())-3.5)<1e-6
            assert all(p.ToMM(a.GetDrillSize().x)>=.8 for a in pads)
            tree=sx.loads((folder/'footprints'/(alias+'.pretty')/(name+'.kicad_mod')).read_text())
            circle=next(v for v in tree if key(v)=='fp_circle' and get(v,'layer')[1]=='F.Fab')
            assert abs(get(circle,'end')[1]-get(circle,'center')[1])*2>=8.5
            assert not any(key(v)=='fp_text' and v[2]=='+' for v in tree)
    erc = json.loads((folder/'verification/erc.json').read_text())
    assert not [v for sheet in erc['sheets'] for v in sheet['violations']]
    if folder.name=='expression':
        fp = footprints['U301']
        # Independent top-view header order from Hestore drawing 16090:
        # VDD GND SCL SDA ADDR ALRT A0 A1 A2 A3.
        expected = dict(zip(['8','3','10','9','1','2','4','5','6','7'],
                            [(round(-11.43+i*2.54,2),0) for i in range(10)]))
        origin = fp.GetPosition()
        for pad in fp.Pads():
            delta = pad.GetPosition()-origin
            assert (round(p.ToMM(delta.x),2),round(p.ToMM(delta.y),2))==expected[pad.GetNumber()]
        assert set(spec['nc'])=={'U301.2'}
        assert spec['parts']['U301']['mpn']=='ADS1115-M (HESTORE 100.417.98)'
        assert spec['parts']['U301']['fp']=='Ardor_THT:ADS1115_M_HESTORE_10041798'
        assert fp.GetPadCount()==10
        for pin,net in {'8':'+3V3_A','3':'GND','10':'ADC_SCL','9':'ADC_SDA',
                        '1':'ADDR','4':'EXP_ADC','5':'EXP_REF_ADC','6':'GND','7':'GND'}.items():
            assert spec['pins']['U301.'+pin]==net
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
    actual={t.GetText():t for t in board.GetDrawings()
            if isinstance(t,p.PCB_TEXT) and t.GetLayer()==p.F_SilkS and t.GetText() in spec['parts']}
    assert set(actual)==set(spec['parts'])
    for record in silk:
        text=actual[record['reference']]
        xy=[p.ToMM(v) for v in [text.GetPosition().x,text.GetPosition().y]]
        assert max(abs(a-b) for a,b in zip(xy,record['position_mm']))<=.000051
        assert text.GetTextAngle().AsDegrees()==record['angle_degrees']
        assert p.ToMM(text.GetTextSize().x)>=.9 and p.ToMM(text.GetTextSize().y)>=.9
    labels = json.loads((folder/'verification/silkscreen.json').read_text())
    assert set(labels['numbered_connector_pins'])==set(connectors)
    assert set(labels['pin_maps'])=={pin.split('.')[0] for pin in connectors}
    report = {'result':'PASS','board_smd_pads':smd_count,'board_smd_footprints':len(smd_refs),'manual_smd_references':smd_refs,'tht_numbered_pads':pad_count,
              'minimum_component_drill_mm':.6,'manual_parts':len(bom),'all_manual_parts_have_front_references':True,
              'hestore_1_5ke6_8ca_references':low_voltage_tvs,
              'hestore_smdj24ca_references':['D203','D204'] if folder.name=='midi-in' else [],
              'hestore_ues1h100mpm_references':[ref for ref,part in spec['parts'].items() if 'BIPOLAR' in part['value']],
              'host_and_panel_pinout_matches_m1':True,'preassembled_smd_module':
              'ADS1115-M HESTORE 100.417.98; single ten-pin row' if folder.name=='expression' else None,
              'breakout_contains_smd':folder.name=='expression','hardware_tested':False}
    (folder/'verification/hand-assembly.json').write_text(json.dumps(report,indent=2)+'\n')
    print(folder.name,'hand assembly and interface checks PASS',pad_count,'THT pads',smd_count,'SMD pads')
