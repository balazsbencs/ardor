"""Render vector review sheets, diagram and five one-page schematics."""
from pathlib import Path
import json,html,textwrap,xml.etree.ElementTree as ET
from interfaces import contract
import cairosvg,pymupdf as fitz
ROOT=Path(__file__).resolve().parents[1];ORDER=['midi-in','expression','mixer','line-out','headphones']
NS='http://www.w3.org/2000/svg';ET.register_namespace('',NS)
def text(x,y,value,size=18,color='#142633',weight='normal'):
    return f'<text x="{x}" y="{y}" font-family="DejaVu Sans,sans-serif" font-size="{size}" fill="{color}" font-weight="{weight}">{html.escape(value)}</text>'
def svg(body):return '<svg xmlns="http://www.w3.org/2000/svg" width="420mm" height="297mm" viewBox="0 0 1400 990"><rect width="1400" height="990" fill="white"/>'+body+'</svg>'
def nested(file,x,y,w,h):
    element=ET.parse(file).getroot();element.set('x',str(x));element.set('y',str(y));element.set('width',str(w));element.set('height',str(h));element.set('preserveAspectRatio','xMidYMid meet');return ET.tostring(element,encoding='unicode')
def render(file,width=2400):
    cairosvg.svg2pdf(url=str(file),write_to=str(file.with_suffix('.pdf')))
    cairosvg.svg2png(url=str(file),write_to=str(file.with_suffix('.png')),output_width=width,background_color='white')
review=ROOT/'review';review.mkdir(exist_ok=True)
book=fitz.open()
for slug in ORDER:
    f=ROOT/slug;s=json.loads((f/'verification/design.json').read_text());a=json.loads((f/'assembly/assembly-validation.json').read_text())
    source=f/'review'/(s['name']+'.svg');cairosvg.svg2png(url=str(source),write_to=str(f/'review/schematic.png'),output_width=2800,background_color='white')
    doc=fitz.open(f/'review/schematic.pdf');assert len(doc)==1;book.insert_pdf(doc);doc.close()
    for side in ['front','back','assembly']:
        cairosvg.svg2png(url=str(f/'review'/('pcb-'+side+'.svg')),write_to=str(f/'review'/('pcb-'+side+'.png')),output_width=1000,background_color='white')
    body=text(40,58,s['title']+' / M1',32,weight='bold')+text(40,94,f"{s['size_mm'][0]} x {s['size_mm'][1]} mm   |   Top SMT only   |   Square header pad = pin 1   |   Separate independent project",19)
    for i,(label,side) in enumerate([('FRONT / COPPER','front'),('BACK / VIEWED FROM BACK','back'),('TOP / COMPONENT REFERENCES','assembly')]):
        x=40+450*i;body+=text(x,147,label,17,weight='bold')+nested(f/'review'/('pcb-'+side+'.svg'),x,170,410,385)
    body+=text(40,603,'CONNECTOR PIN MAP — USE NUMBERED PADS, NOT SCREEN LEFT/RIGHT',19,weight='bold')
    guide=contract(slug,s)
    ports={ref:[(pin['number'],pin['short']) for pin in c['pins']] for ref,c in guide['ports'].items()}
    for i,(ref,pins) in enumerate(ports.items()):
        col=i%3;row=i//3;x=40+450*col;y=642+100*row
        body+=text(x,y,ref+' / '+guide['ports'][ref]['silk'],16,weight='bold')
        lines=['  '.join(str(n)+'='+net for n,net in sorted(pins)[j:j+2]) for j in range(0,len(pins),2)]
        for j,line in enumerate(lines):body+=text(x,y+24*(j+1),line,15)
    footer=942
    body+=text(40,footer,f"SMT: {a['smt_placements_per_board']} placements; manual fit: {', '.join(a['manual_references'])}",16)
    body+=text(40,footer+25,'Check IC pin 1 / diode / electrolytic polarity in JLCPCB preview. See module README for jumpers, power and commissioning.',15)
    file=f/'review/assembly-reference.svg';file.write_text(svg(body));render(file)
    card=text(40,55,s['title']+' / PIN GUIDE',27,weight='bold')
    card+=text(40,94,'IN = into this board     OUT = from this board     I/O = both directions     GND = 0 V     3V3 = 3.3 V',18)
    card+=text(40,145,'BACK / READ FROM THIS SIDE',19,weight='bold')
    card+=nested(f/'review/pcb-back.svg',40,170,460,400)
    card+=text(40,610,'The square connector pad is pin 1.',20,weight='bold')
    card+=text(40,644,'Match the connector name and pin number.',17)
    card+=text(40,675,'Connector assignments differ between modules.',17)
    if guide['unused_pins']:
        card+=text(40,727,'UNUSED COMPONENT PINS',18,weight='bold')
        for j,(pin,note) in enumerate(guide['unused_pins'].items()):card+=text(40,756+j*24,pin+' / '+note,17)
    card+=text(40,826,'H1 / H2: mounting holes; no electrical connection.',16)
    card+=text(40,853,'Small via holes are not wire connectors.',16)
    card+=text(40,891,'JP connectors accept shunts, not host cables.' if slug=='expression' else 'Use the connector pin table before you connect power.',16)
    columns=[135,135]
    for ref,c in guide['ports'].items():
        lines=[]
        for pin in c['pins']:
            lines.append((str(pin['number'])+': '+pin['short']+' / '+pin['direction'],True))
            lines.extend((line,False) for line in textwrap.wrap(pin['connect'],width=50))
        if c['setup']:lines.extend((line,False) for line in textwrap.wrap(c['setup'],width=50))
        titlelines=textwrap.wrap(ref+' / '+c['title'],width=40)
        height=len(titlelines)*19+len(lines)*17+20
        col=min(range(2),key=lambda i:columns[i]);x=550+col*420;y=columns[col]
        assert y+height<935,(slug,ref,'Wiring card overflow')
        card+=f'<rect x="{x-10}" y="{y-20}" width="410" height="{height}" rx="6" fill="#f5f7f8" stroke="#dce3e6"/>'
        for titleline in titlelines:card+=text(x,y,titleline,16,weight='bold');y+=19
        y+=4
        for line,bold in lines:card+=text(x,y,line,14,weight='bold' if bold else 'normal');y+=17
        columns[col]+=height+12
    card+=text(40,963,'Use ASSEMBLY.md for the short procedure. Disconnect the power supply before solder work or jumper changes.',16)
    file=f/'review/wiring-guide.svg';file.write_text(svg(card));render(file)
book.save(review/'schematics.pdf');book.close()
DATA=[
('midi-in','MIDI INPUT','DIN contacts 4 / 5','H11L1M receiver','3.3 V UART RX','5 V + 3.3 V; chassis terminal local','No MIDI OUT / no USB'),
('expression','EXPRESSION','Passive TRS pot','ADS1115 + protection','3.3 V I2C host','3.3 V; polarity/address/pull-up jumpers local','10k–100k linear; no powered CV'),
('mixer','STEREO BUFFER / MONO MIX','Stereo line source','Buffers + (L+R)/2','L / R / MONO line','5 V; bias and all output DC blocking local','Inputs <=1 Vrms; loads >=10k'),
('line-out','MONO LINE OUTPUT','Mono line source','Buffer + relay mute','TS line / amp feed','5 V + host enable; bias and coupling local','No true bypass / no speaker power'),
('headphones','OPTIONAL HEADPHONES','Stereo line source','TPA6132 + charge pump','Stereo TRS headphones','5 V + host enable; all charge-pump parts local','Inputs <=1 Vrms; phones >=32 ohm')]
body=text(40,55,'ARDOR / BUILD ONLY WHAT YOU NEED',33,weight='bold')+text(40,94,'M1 — Five complete boards. Each needs only an external source/host and its listed regulated supply.',20)
for i,(slug,title,left,middle,right,power,limit) in enumerate(DATA):
    y=134+i*155;color=['#256d85','#5c6b32','#86572b','#5554a2','#a43b5b'][i]
    body+=f'<rect x="40" y="{y}" width="1320" height="139" rx="9" fill="#f5f7f8" stroke="#dae2e6"/>'
    body+=text(60,y+29,title,20,color,'bold')+text(520,y+29,power,17)
    for x,label,w in [(60,left,330),(520,middle,340),(995,right,335)]:
        body+=f'<rect x="{x}" y="{y+46}" width="{w}" height="47" rx="5" fill="white" stroke="{color}" stroke-width="2"/>'+text(x+14,y+77,label,19)
    for x in [413,891]:body+=f'<path d="M{x},{y+70}h66m-12,-8l12,8l-12,8" fill="none" stroke="{color}" stroke-width="2.5"/>'
    s=json.loads((ROOT/slug/'verification/design.json').read_text())
    body+=text(60,y+120,limit,16)+text(995,y+120,f"{s['size_mm'][0]} x {s['size_mm'][1]} mm / independent",16)
body+=text(40,951,'No carrier or other Ardor module required. Separate PCBA setups repeat costs; build a subset for best economy.',17)
file=review/'module-overview.svg';file.write_text(svg(body));render(file)
print('Five assembly references, five schematic PNGs, five-page booklet and one-page overview exported')
