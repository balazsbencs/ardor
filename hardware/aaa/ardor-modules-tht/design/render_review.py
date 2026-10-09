"""Render native CAD previews and a printable assembly overview."""
from pathlib import Path
import json
import html
import xml.etree.ElementTree as ET
import cairosvg
import pymupdf as fitz
ROOT = Path(__file__).resolve().parents[1]
ORDER = ['midi-in','expression','mixer','line-out','headphones']
NS = 'http://www.w3.org/2000/svg'
ET.register_namespace('',NS)
def text(x,y,value,size=18):
    return f'<text x="{x}" y="{y}" font-family="DejaVu Sans,sans-serif" font-size="{size}" fill="#172b38">{html.escape(value)}</text>'
def nested(file,x,y,w,h):
    element = ET.parse(file).getroot()
    for k,v in [('x',x),('y',y),('width',w),('height',h)]:
        element.set(k,str(v))
    element.set('preserveAspectRatio','xMidYMid meet')
    return ET.tostring(element,encoding='unicode')
review = ROOT/'review'
review.mkdir(exist_ok=True)
book = fitz.open()
body = text(35,50,'ARDOR T1 / HAND-ASSEMBLED MODULES',30)+text(35,83,'Three all-THT circuits; expression uses ADS1115 breakout; headphones use one SOIC-8',18)
for i,slug in enumerate(ORDER):
    folder = ROOT/slug
    s = json.loads((folder/'verification/design.json').read_text())
    doc = fitz.open(folder/'review/schematic.pdf')
    assert len(doc)==1
    doc[0].get_pixmap(matrix=fitz.Matrix(2,2)).save(folder/'review/schematic.png')
    book.insert_pdf(doc)
    doc.close()
    for side in ['front','back','assembly']:
        file = folder/'review'/('pcb-'+side+'.svg')
        cairosvg.svg2png(url=str(file),write_to=str(file.with_suffix('.png')),output_width=1200,background_color='white')
    x = 35+(i%3)*460
    y = 125+(i//3)*430
    w,h = s['size_mm']
    body += text(x,y,slug.upper().replace('-',' '),22)
    body += text(x,y+28,f'{w} x {h} mm / {len(s["parts"])} manual parts',17)
    body += nested(folder/'review/pcb-assembly.svg',x,y+45,410,325)
body += text(955,615,'Headphones: OPA1656 SOIC-8',20)
body += text(955,650,'Expression: Adafruit 1085 QT',20)
body += text(955,685,'Headphones: 5 V / 150 mA',20)
body += text(955,720,'Enable LOW at startup >=5 s',20)
body += text(35,975,'Unbuilt prototypes. Read each BOM, pin guide and commissioning procedure before assembly.',18)
file = review/'module-overview.svg'
file.write_text('<svg xmlns="http://www.w3.org/2000/svg" width="420mm" height="297mm" viewBox="0 0 1440 1018"><rect width="1440" height="1018" fill="white"/>'+body+'</svg>\n')
cairosvg.svg2pdf(url=str(file),write_to=str(file.with_suffix('.pdf')))
cairosvg.svg2png(url=str(file),write_to=str(file.with_suffix('.png')),output_width=2200,background_color='white')
book.save(review/'schematics.pdf')
book.close()
print('CAD review PNGs, assembly overview and schematic booklet exported')
