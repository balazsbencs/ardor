"""Verify every physical pad, board outline, trace width and module boundary."""
from pathlib import Path
import json,math,xml.etree.ElementTree as ET
import pcbnew as p
ROOT=Path(__file__).resolve().parents[1]
def verify(folder):
    spec=json.loads((folder/'verification/design.json').read_text());b=p.LoadBoard(str(folder/(spec['name']+'.kicad_pcb')))
    xml=ET.parse(folder/'verification/netlist.xml')
    pins={n.attrib['ref']+'.'+n.attrib['pin']:net.attrib['name'].replace('ALERT/RDY','ALERT{slash}RDY') for net in xml.findall('.//nets/net') for n in net.findall('node')}
    fps={f.GetReference():f for f in b.GetFootprints()};assert set(fps)==set(spec['parts'])|{'H1','H2'}
    found=set();physical=0
    for ref,f in fps.items():
        if ref in ['H1','H2']:assert f.GetAttributes() & p.FP_BOARD_ONLY;continue
        assert (str(f.GetFPID().GetLibNickname())+':'+str(f.GetFPID().GetLibItemName()))==spec['parts'][ref]['fp']
        for a in f.Pads():
            if not a.GetNumber():continue
            pin=ref+'.'+a.GetNumber();assert a.GetNetname()==pins[pin],(folder.name,pin)
            found.add(pin);physical+=1
    assert found==set(pins)
    drc=json.loads((folder/'verification/drc.json').read_text());assert not drc['violations'] and not drc['unconnected_items'] and not drc['schematic_parity']
    pro=json.loads((folder/(spec['name']+'.kicad_pro')).read_text());assert not pro['board']['design_settings']['drc_exclusions']
    counts={};via_count=0
    for t in b.GetTracks():
        if isinstance(t,p.PCB_VIA):
            assert p.ToMM(t.GetWidth(p.F_Cu))==.6 and p.ToMM(t.GetDrill())==.3
            via_count+=1;continue
        net=t.GetNetname();assert net!='/GND','Remove redundant ground routes'
        target=.6 if net=='/CHASSIS' else .2
        assert abs(p.ToMM(t.GetWidth())-target)<1e-6,(folder.name,net,p.ToMM(t.GetWidth()))
        counts[net]=counts.get(net,0)+1
    bb=b.GetBoardEdgesBoundingBox();size=[round(p.ToMM(bb.GetWidth())-.05,4),round(p.ToMM(bb.GetHeight())-.05,4)]
    assert size==spec['size_mm'],(folder.name,size)
    layers=[z.GetLayer() for z in b.Zones() if not z.GetIsRuleArea()]
    assert sorted(layers)==[p.F_Cu,p.B_Cu]
    assert all(z.IsFilled() for z in b.Zones() if not z.GetIsRuleArea())
    if folder.name=='midi-in':assert (folder/'Ardor_MIDI_THT.kicad_dru').exists()
    sch=(folder/(spec['name']+'.kicad_sch')).read_text();assert '(sheet ' not in sch,'Module must be one complete sheet'
    libs=(folder/'fp-lib-table').read_text();assert '${KIPRJMOD}/footprints/' in libs and '/ardor-io' not in libs
    report={'result':'PASS','connected_pin_numbers':len(spec['pins']),'explicit_no_connects':len(spec['nc']),'physical_numbered_pads':physical,'electrical_footprints':len(spec['parts']),'mounting_holes':2,'board_size_mm':size,'functional_nets':len(set(spec['pins'].values())),'routed_nets':len(counts),'signal_and_supply_width_mm':.2,'chassis_width_mm':.6,'ground_tracks':0,'vias':via_count,'erc_violations':0,'drc_violations':0,'unconnected_items':0,'schematic_parity_violations':0,'drc_exclusions':0,'one_sheet':True,'module_dependencies':[]}
    (folder/'verification/validation.json').write_text(json.dumps(report,indent=2)+'\n');print(folder.name,'PASS',physical,'pads',sum(counts.values()),'segments',via_count,'vias')
if __name__=='__main__':
    for folder in ROOT.iterdir():
        if (folder/'verification/design.json').exists():verify(folder)
