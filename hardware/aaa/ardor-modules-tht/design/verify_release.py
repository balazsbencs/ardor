"""Require clean native CAD, then check exported bare-board packages."""
from pathlib import Path
import csv
import hashlib
import json
import re
import zipfile
ROOT = Path(__file__).resolve().parents[1]
def sha(file):
    return hashlib.sha256(file.read_bytes()).hexdigest()
def manifest(folder):
    files = sorted(p for p in folder.rglob('*') if p.is_file() and p.name!='SHA256SUMS'
                   and p.suffix not in ['.kicad_prl','.pyc'] and '__pycache__' not in p.parts)
    (folder/'SHA256SUMS').write_text(''.join(sha(p)+'  '+str(p.relative_to(folder))+'\n' for p in files))
# Native vector/session exporters add trailing spaces. Normalize lexical
# whitespace before hashing; this does not change any geometry or rendering.
for file in ROOT.rglob('*'):
    if file.suffix in ['.svg','.ses']:
        file.write_text('\n'.join(line.rstrip() for line in file.read_text().splitlines())+'\n')
modules = {}
for folder in ROOT.iterdir():
    if not (folder/'verification/design.json').exists():
        continue
    s = json.loads((folder/'verification/design.json').read_text())
    validation = json.loads((folder/'verification/validation.json').read_text())
    hand = json.loads((folder/'verification/hand-assembly.json').read_text())
    assert validation['result']==hand['result']=='PASS'
    if folder.name=='headphones':
        assert json.loads((folder/'verification/headphone-circuit.json').read_text())['result']=='PASS'
    d = json.loads((folder/'verification/drc.json').read_text())
    assert not d['violations'] and not d['unconnected_items'] and not d['schematic_parity']
    placements = list(csv.DictReader((folder/'assembly/kicad-smt-positions.csv').open()))
    assert len(placements)==hand['board_smd_footprints']
    if placements:
        assert folder.name=='headphones' and placements[0]['Ref']=='U601'
    files = sorted((folder/'assembly/gerbers').iterdir())
    assert len(files)==12
    outline = next(p for p in files if 'Edge_Cuts' in p.name).read_text()
    assert '%MOMM*%' in outline and '%FSLAX46Y46*%' in outline
    xy = {(int(x)/1e6,int(y)/1e6) for x,y in re.findall(r'X(-?\d+)Y(-?\d+)D0[12]\*',outline)}
    w,h = s['size_mm']
    assert xy=={(50,-50),(50+w,-50),(50,-50-h),(50+w,-50-h)}
    points = []
    for file in files:
        if file.suffix=='.drl':
            raw = file.read_text()
            assert 'METRIC' in raw and 'absolute' in raw
            points += [(float(x),float(y)) for x,y in re.findall(r'^X(-?[\d.]+)Y(-?[\d.]+)$',raw,re.M)]
    assert all(50<=x<=50+w and -50-h<=y<=-50 for x,y in points)
    npth = next(p for p in files if 'NPTH' in p.name).read_text()
    assert npth.count('\nX')==2
    with zipfile.ZipFile(folder/'assembly'/(s['name']+'-Gerbers.zip')) as z:
        assert z.testzip() is None and set(z.namelist())=={p.name for p in files}
        for file in files:
            assert z.read(file.name)==file.read_bytes()
    report = {'result':'PASS','fabrication_files':12,'zip_crc_and_contents_match':True,
              'smt_placements':len(placements),'plated_drill_and_outline_origin_match':True,'nonplated_mounting_holes':2,
              'pcb_sha256':sha(folder/(s['name']+'.kicad_pcb')),
              'schematic_sha256':sha(folder/(s['name']+'.kicad_sch')),
              'bom_sha256':sha(folder/'BOM.csv')}
    (folder/'verification/package-validation.json').write_text(json.dumps(report,indent=2)+'\n')
    modules[folder.name] = {'electrical':validation,'hand_assembly':hand,'package':report}
    manifest(folder)
    print(folder.name,'bare-board package PASS')
summary = {'result':'PASS','kicad_version':'9.0.2','modules':modules,
           'total_numbered_component_pads':sum(m['hand_assembly']['tht_numbered_pads']+m['hand_assembly']['board_smd_pads'] for m in modules.values()),
           'total_manual_components':sum(m['hand_assembly']['manual_parts'] for m in modules.values()),
           'board_smd_pads':sum(m['hand_assembly']['board_smd_pads'] for m in modules.values()),
           'board_smd_footprints':sum(m['hand_assembly']['board_smd_footprints'] for m in modules.values()),'preassembled_smd_breakouts':1,'hardware_tested':False}
(ROOT/'review/validation-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
manifest(ROOT)
