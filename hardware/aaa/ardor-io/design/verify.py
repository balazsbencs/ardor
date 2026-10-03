from pathlib import Path
import json,xml.etree.ElementTree as ET,hashlib,csv
import pymupdf as fitz
P=Path(__file__).resolve().parents[1]
expected=json.loads((P/'design/expected_nets.json').read_text())
r=ET.parse(P/'verification/netlist.xml')
actual={n.attrib['ref']+'.'+n.attrib['pin']:net.attrib['name'].rsplit('/',1)[-1] for net in r.findall('.//nets/net') for n in net.findall('node')}
errors=[f'{pin}: expected {net}, got {actual.get(pin)}' for pin,net in expected.items() if not pin.startswith('#') and actual.get(pin)!=net]
# Deliberate circuit invariants, independent of the generated expected-net map.
checks={
 'Pi UART RX uses GPIO15, pin 10':actual['J101.10']=='MIDI_RX',
 'Pi headphone enable uses GPIO17, pin 11':actual['J101.11']=='HP_ENABLE',
 'Pi line enable uses GPIO22, pin 15':actual['J101.15']=='LINE_ENABLE',
 'Codec button GPIO27 is not borrowed':actual.get('J101.13','').startswith('unconnected-'),
 'MIDI output is not pulled up to 5V':actual['R202.1']=='+3V3_PI',
 'MIDI LED reverse diode correct':actual['D201.1']==actual['U201.1'] and actual['D201.2']==actual['U201.2'],
 'MIDI differential TVS across DIN4/5':{actual['D202.1'],actual['D202.2']}=={'MIDI_4','MIDI_5'},
 'ADC address grounded, 0x48':actual['U301.1']=='GND',
 'Both ADC channels have upper and lower clamps':all(actual[d+'.1']=='+3V3_ADC' for d in ['D303','D305']) and all(actual[d+'.2']=='GND' for d in ['D304','D306']),
 'Line relay NC grounds jack; NO carries signal':actual['K501.1']=='GND' and actual['K501.10']=='LINE_DRIVE' and actual['K501.5']==actual['K501.6']=='LINE_JACK',
 'Line capacitor polarity faces biased output':actual['C501.1']=='LINE_BUF',
 'Separate line/amp buffers':actual['U501.1']=='LINE_BUF' and actual['U501.7']=='AMP_BUF',
 'Headphone charge-pump reservoir nets distinct from 5V':actual['U601.12']=='HPVDD' and actual['U601.8']=='HPVSS' and actual['U601.14']=='+5V_A',
 'Headphone flying capacitor across CPP/CPN':{actual['C607.1'],actual['C607.2']}=={'CPP','CPN'},
 'Headphone gain -6dB and inputs single-ended':all(actual['U601.'+n]=='GND' for n in ['2','3','6','7']),
 'Headphone ground and thermal pad connected':all(actual['U601.'+n]=='GND' for n in ['10','15','17']),
 'No short between AUX L and R':actual['J102.1']!=actual['J102.3'],
 'ADC and audio rails distinct':actual['U301.8']!=actual['U601.14'],
}
errors += [k for k,v in checks.items() if not v]
assert not errors,'\n'.join(errors)
report=['Ardor Rev A connectivity verification','',f'PASS: {sum(not k.startswith("#") for k in expected)} assigned physical pin connections match the exported KiCad netlist.',f'PASS: {len(checks)} independent circuit invariants.','']+[('PASS: '+k) for k in checks]
report+=['','ERC: see erc.rpt. No suppressed ERC rules or exclusions.','No hardware, SPICE, audio performance or assembled-product ESD qualification has been performed.']
(P/'verification/connectivity-audit.txt').write_text('\n'.join(report)+'\n')
doc=fitz.open(P/'Ardor_IO.pdf');assert len(doc)==6
for i,p in enumerate(doc):p.get_pixmap(matrix=fitz.Matrix(1.35,1.35)).save(P/f'verification/page-{i+1}.png')
print('\n'.join(report[:5]))
