"""Check purchased-part identity, critical pin nets, capacitor lands, and net preservation.
Run with KiCad 9 Python after exporting review/fresh-netlist.xml and fresh ERC/DRC.
"""
from pathlib import Path
import csv,json,math,xml.etree.ElementTree as ET
import pcbnew as p
R=Path(__file__).resolve().parents[1]
b=p.LoadBoard(str(R/'Ardor_IO.kicad_pcb'));fps={f.GetReference():f for f in b.GetFootprints()}
xml=ET.parse(R/'review/fresh-netlist.xml');comps={c.attrib['ref']:c for c in xml.findall('.//components/comp')}
with (R/'BOM.csv').open() as f:bom={r['Reference']:r for r in csv.DictReader(f)}
parts={
 'Q501':('AO3400A','Alpha & Omega AO3400A','Package_TO_SOT_SMD:SOT-23','https://www.aosmd.com/res/data_sheets/AO3400A.pdf'),
 'C501':('47u / 16V','Panasonic EEEFK1C470P','Ardor_Capacitor:CP_Elec_Panasonic_FK_D6.3_H5.8','https://industrial.panasonic.com/cdbs/www-data/pdf/RDE0000/ABA0000C1181.pdf'),
}
for ref,(value,mpn,fp,url) in parts.items():
 f=fps[ref];c=comps[ref];r=bom[ref]
 assert f.GetValue()==c.findtext('value')==r['Value']==value
 assert str(f.GetFPID().GetLibNickname())+':'+str(f.GetFPID().GetLibItemName())==c.findtext('footprint')==r['Footprint']==fp
 assert f.GetFieldByName('MPN').GetText()==c.findtext("fields/field[@name='MPN']")==r['MPN']==mpn
 assert f.GetFieldByName('Datasheet').GetText()==c.findtext('datasheet')==r['Datasheet']==url
# Manufacturer land drawing: gap 1.8, length 3.2, width 1.6 mm, center spacing 5.0 mm.
f=fps['C501'];pads={a.GetNumber():a for a in f.Pads()};assert set(pads)=={'1','2'}
for pn,a in pads.items():
 assert a.GetShape()==p.PAD_SHAPE_RECT and a.GetAttribute()==p.PAD_ATTRIB_SMD
 assert abs(p.ToMM(a.GetSize().x)-3.2)<1e-6 and abs(p.ToMM(a.GetSize().y)-1.6)<1e-6
 assert abs(p.ToMM(a.GetFPRelativePosition().x)-(-2.5 if pn=='1' else 2.5))<1e-6
 assert a.GetFPRelativePosition().y==0
# Original feedback and diode orientations are retained; do not mistake the transistor's pin order.
for ref,m in {'Q501':{'1':'RELAY_GATE','2':'GND','3':'RELAY_LOW'},'C501':{'1':'LINE_BUF','2':'LINE_AC'},'R101':{'1':'GND','2':'CHASSIS'},'J302':{'3':'GND'},'J503':{'2':'GND'},'J602':{'3':'GND'}}.items():
 for a in fps[ref].Pads():
  if a.GetNumber() in m:assert a.GetNetname().rsplit('/',1)[-1]==m[a.GetNumber()]
def netmap(path):
 return {(n.attrib['ref'],n.attrib['pin']):net.attrib['name'].replace('ALERT/RDY','ALERT{slash}RDY') for net in ET.parse(path).findall('.//nets/net') for n in net.findall('node')}
assert netmap(R/'review/baseline/fresh-netlist.xml')==netmap(R/'review/fresh-netlist.xml'),'Circuit connectivity changed'
# Match all actual numbered PCB pads against current schematic pins (not just the intended map).
want={key:net for key,net in netmap(R/'review/fresh-netlist.xml').items() if comps[key[0]].findtext('footprint')}
actual={(f.GetReference(),a.GetNumber()):a.GetNetname() for f in b.GetFootprints() for a in f.Pads() if a.GetNumber()}
assert actual==want
for key in ['violations','unconnected_items','schematic_parity']:assert not json.loads((R/'review/fresh-drc.json').read_text())[key]
assert not any(s['violations'] for s in json.loads((R/'review/fresh-erc.json').read_text())['sheets'])
report={'parts':{ref:dict(zip(['value','mpn','footprint','datasheet'],vals)) for ref,vals in parts.items()},'bom_schematic_board_agree':True,'c501_lands_mm':{'pad':[3.2,1.6],'pitch':5.0,'gap':1.8},'critical_pin_nets_match':True,'all_pad_assignments_checked':len(actual),'all_original_pin_nets_preserved':True,'erc_violations':0,'drc_violations':0,'unconnected_items':0,'schematic_parity_issues':0,'gate_voltage_nominal_V':3.3*100/101,'relay_drop_at_30mA_48mOhm_V':.030*.048,'hardware_tested':False}
(R/'review/fix-verification.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS: BOM/schematic/board part identity, manufacturer capacitor lands, critical pin mapping, all original nets, ERC/DRC/parity')
