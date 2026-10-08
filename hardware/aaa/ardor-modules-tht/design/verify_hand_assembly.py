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
    for ref,part in spec['parts'].items():
        fp = footprints[ref]
        assert fp.GetAttributes() & p.FP_THROUGH_HOLE, (folder.name,ref,'not THT')
        assert not fp.GetAttributes() & p.FP_SMD
        for pad in fp.Pads():
            if pad.GetNumber():
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
        for ref in ['U601','U602']:
            assert spec['parts'][ref]['mpn']=='Texas Instruments LM386N-1/NOPB'
            assert {ref+'.1',ref+'.8'}<=set(spec['nc'])
        assert spec['parts']['K601']['mpn']=='Omron G5V-2-H1 DC5'
    silk = json.loads((folder/'verification/component-silkscreen.json').read_text())
    assert {v['reference'] for v in silk}==set(spec['parts'])
    labels = json.loads((folder/'verification/silkscreen.json').read_text())
    assert set(labels['numbered_connector_pins'])==set(connectors)
    assert set(labels['pin_maps'])=={pin.split('.')[0] for pin in connectors}
    report = {'result':'PASS','board_smd_pads':0,'board_smd_footprints':0,'tht_numbered_pads':pad_count,
              'minimum_component_drill_mm':.6,'manual_parts':len(bom),'all_manual_parts_have_front_references':True,
              'host_and_panel_pinout_matches_m1':True,'preassembled_smd_module':
              'Adafruit 1085 STEMMA QT ADS1115' if folder.name=='expression' else None,
              'breakout_contains_smd':folder.name=='expression','hardware_tested':False}
    (folder/'verification/hand-assembly.json').write_text(json.dumps(report,indent=2)+'\n')
    print(folder.name,'THT and interface checks PASS',pad_count,'pads')
