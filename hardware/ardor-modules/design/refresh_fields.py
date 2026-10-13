"""Refresh text/part metadata from the schematic without moving any copper."""
from pathlib import Path
import json,xml.etree.ElementTree as ET
import sexpdata as sx
ROOT=Path(__file__).resolve().parents[1]
key=lambda v:str(v[0]) if isinstance(v,list) and v else ''
for folder in ROOT.iterdir():
    if not (folder/'verification/design.json').exists():continue
    spec=json.loads((folder/'verification/design.json').read_text());file=folder/(spec['name']+'.kicad_pcb')
    board=sx.loads(file.read_text());x=ET.parse(folder/'verification/netlist.xml')
    comps={c.attrib['ref']:c for c in x.findall('.//components/comp')}
    for f in board:
        if key(f)!='footprint':continue
        properties={v[1]:v for v in f if key(v)=='property'};ref=properties['Reference'][2]
        if ref not in comps:continue
        f[:]=[v for v in f if not(key(v)=='fp_text' and str(v[1])=='user' and v[2]=='${REFERENCE}')]
        c=comps[ref];properties['Value'][2]=c.findtext('value');properties['Datasheet'][2]=c.findtext('datasheet','')
        for field in ['MPN','JLCPCB Part #']:properties[field][2]=c.findtext("fields/field[@name='"+field+"']",'')
    file.write_text(sx.dumps(board)+'\n')
