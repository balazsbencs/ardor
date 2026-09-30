"""Check every physical PCB pin and intended circuit connection for Rev C."""
from pathlib import Path
import json,xml.etree.ElementTree as ET
import pcbnew as pcb
R=Path(__file__).resolve().parents[1]
x=ET.parse(R/'verification/netlist.xml')
pins={(n.attrib['ref'],n.attrib['pin']):net.attrib['name'].replace('ALERT/RDY','ALERT{slash}RDY') for net in x.findall('.//nets/net') for n in net.findall('node')}
expected={k:v for k,v in json.loads((R/'design/expected_nets.json').read_text()).items() if not k.startswith('#')}
for key,net in expected.items():
 ref,num=key.rsplit('.',1);actual=pins[(ref,num)].split('/')[-1]
 assert actual==net,(key,actual,net)
comps={c.attrib['ref']:c for c in x.findall('.//components/comp') if c.findtext('footprint')}
b=pcb.LoadBoard(str(R/'Ardor_IO.kicad_pcb'));fps={f.GetReference():f for f in b.GetFootprints()}
assert set(comps)=={ref for ref in fps if not ref.startswith('H')}
count=0
for ref,c in comps.items():
 assert c.findtext('value')==fps[ref].GetValue(),ref
 for pad in fps[ref].Pads():
  assert pad.GetNetname()==pins[(ref,pad.GetNumber())],(ref,pad.GetNumber())
  count+=1
critical={'J101.10':'MIDI_RX','J101.15':'LINE_ENABLE','J101.3':'PI_SDA','J101.5':'PI_SCL','U201.6':'+5V_PI','R202.1':'+3V3_PI','U301.4':'EXP_ADC','U301.5':'EXP_REF_ADC','U301.8':'+3V3_ADC','C501.1':'MONO_BUF','C502.1':'MONO_BUF','K501.1':'GND','Q501.2':'GND','D502.1':'+5V_PI'}
for key,net in critical.items():assert expected[key]==net,key
assert not any(ref in fps for ref in ['U501','U601','J602','C503'])
assert pins[('J101','11')].startswith('unconnected-')
# Precision-resistor compromise: worst-case averaging coefficients .495 and .505.
line_load=10000*10000/(10000+10000)+100
amp_load=100000*100000/(100000+100000)+1000
report={'intended_non_nc_connections_checked':len(expected),'all_physical_pad_net_assignments_checked':count,'electrical_footprints':len(comps),'critical_invariants_checked':len(critical),'midi_expression_gpio_and_i2c_mapping_preserved':True,'headphones_and_redundant_output_buffers_absent':True,'opamp_peak_load_current_at_1_vrms_ma':round(2**.5*(1/line_load+1/amp_load)*1000,4),'required_slew_at_20khz_1_vrms_v_per_us':round(2*3.141592653589793*20000*2**.5/1e6,4),'tlv9002_typical_slew_v_per_us':2.0,'mono_1pct_resistor_worst_case_weights':[.495,.505],'hardware_measured':False}
(R/'verification/connectivity-audit.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
