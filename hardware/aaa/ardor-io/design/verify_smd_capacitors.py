"""Verify the selected film parts, SMD lands, and BOM/schematic/board agreement."""
from pathlib import Path
import csv,json,xml.etree.ElementTree as ET
import pcbnew as p
ROOT=Path(__file__).resolve().parents[1]
b=p.LoadBoard(str(ROOT/'Ardor_IO.kicad_pcb'));fps={f.GetReference():f for f in b.GetFootprints()}
xml=ET.parse(ROOT/'routing/schematic-netlist.xml');comps={c.attrib['ref']:c for c in xml.findall('.//components/comp')}
with (ROOT/'BOM.csv').open() as f:bom={r['Reference']:r for r in csv.DictReader(f)}
fpname='Ardor_Capacitor:C_Rubycon_MU_3225';mpn='Rubycon 16MU225KB23225';value='2.2u / 16V film';url='https://www.rubycon.co.jp/wp-content/uploads/catalog-pmlcap/MU.pdf'
refs=['C401','C402','C502','C601','C602']
for ref in refs:
 f=fps[ref];c=comps[ref];row=bom[ref]
 assert str(f.GetFPID().GetLibNickname())+':'+str(f.GetFPID().GetLibItemName())==fpname==c.findtext('footprint')==row['Footprint'],ref
 assert f.GetValue()==value==c.findtext('value')==row['Value'],ref
 assert mpn==c.findtext("fields/field[@name='MPN']")==row['MPN'],ref
 assert url==c.findtext('datasheet')==row['Datasheet'],ref
 assert f.GetAttributes()&p.FP_SMD,ref
 pads={a.GetNumber():a for a in f.Pads()};assert set(pads)=={'1','2'}
 for num,a in pads.items():
  assert a.GetAttribute()==p.PAD_ATTRIB_SMD and a.GetDrillSize().x==0 and a.GetDrillSize().y==0
  assert abs(p.ToMM(a.GetSize().x)-.9)<1e-6 and abs(p.ToMM(a.GetSize().y)-2.3)<1e-6
 delta=pads['2'].GetPosition()-pads['1'].GetPosition()
 assert abs((delta.x**2+delta.y**2)**.5/p.FromMM(1)-2.7)<1e-6
 # Independent expected-net map includes the circuit's local net names.
 expected=json.loads((ROOT/'design/expected_nets.json').read_text())
 for num,a in pads.items():assert a.GetNetname().rsplit('/',1)[-1]==expected[ref+'.'+num]
report={'references':refs,'manufacturer_part':mpn,'value':value,'tolerance_percent':10,'nonpolar':True,'body_nominal_mm':[3.2,2.5,1.8],'body_max_height_mm':2.0,'footprint':fpname,'land_pad_mm':[.9,2.3],'pad_center_spacing_mm':2.7,'inner_gap_mm':1.8,'outer_span_mm':3.6,'body_area_reduction_percent':round(100*(1-3.2*2.5/(7.2*7.2)),1),'smd_pads_checked':10,'bom_schematic_board_match':True,'unchanged_capacitor_nets':True}
(ROOT/'routing/smd-capacitor-audit.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS: five SMD film capacitors, ten manufacturer-sized SMD lands, BOM/schematic/board agreement and unchanged nets')
