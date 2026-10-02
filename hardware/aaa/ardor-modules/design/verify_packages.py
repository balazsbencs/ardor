"""Check BOM/CPL/fabrication coherence and write scoped SHA256 manifests."""
from pathlib import Path
import csv,json,re,hashlib,zipfile
ROOT=Path(__file__).resolve().parents[1]
def sha(file):return hashlib.sha256(file.read_bytes()).hexdigest()
def verify(folder):
    s=json.loads((folder/'verification/design.json').read_text());a=json.loads((folder/'assembly/assembly-validation.json').read_text());assembly=folder/'assembly'
    assert a['hashes']['pcb_sha256']==sha(folder/(s['name']+'.kicad_pcb'))
    assert a['hashes']['schematic_sha256']==sha(folder/(s['name']+'.kicad_sch'))
    cpl={r['Designator']:r for r in csv.DictReader((assembly/'jlcpcb-cpl.csv').open())}
    native={r['Ref']:r for r in csv.DictReader((assembly/'kicad-smt-positions.csv').open())}
    assert set(cpl)==set(native)
    for ref,row in cpl.items():
        n=native[ref];assert row['Layer']=='Top' and n['Side']=='top'
        assert abs(float(row['Mid X'].removesuffix('mm'))-float(n['PosX']))<.00011
        assert abs(float(row['Mid Y'].removesuffix('mm'))-float(n['PosY']))<.00011
        assert abs((float(row['Rotation'])-float(n['Rot'])+180)%360-180)<.0011
        assert n['Val']==s['parts'][ref]['value']
    bom=list(csv.DictReader((assembly/'jlcpcb-bom.csv').open()));seen=[]
    parts=json.loads((assembly/'parts.json').read_text())['parts'];codes={v['jlcpcb_part']:v for v in parts}
    for row in bom:
        refs=row['Designator'].split(',');part=codes[row['LCSC Part #']];assert set(refs)==set(part['references']);seen+=refs
        assert row['Footprint']==part['footprint'].split(':')[1]
    assert len(seen)==len(set(seen)) and set(seen)==set(cpl)
    manual={r['Reference'] for r in csv.DictReader((assembly/'manual-bom.csv').open()) if r['Reference']!='Shunts'}
    assert manual|set(cpl)==set(s['parts']) and not manual&set(cpl)
    gerbers=assembly/'gerbers';files=sorted(gerbers.iterdir());assert len(files)==12
    outline=next(v for v in files if 'Edge_Cuts' in v.name).read_text();assert '%MOMM*%' in outline and '%FSLAX46Y46*%' in outline
    xy={(int(x)/1e6,int(y)/1e6) for x,y in re.findall(r'X(-?\d+)Y(-?\d+)D0[12]\*',outline)}
    w,h=s['size_mm'];assert xy=={(50,-50),(50+w,-50),(50,-50-h),(50+w,-50-h)},xy
    drillpoints=[]
    for file in files:
        if file.suffix=='.drl':
            raw=file.read_text();assert 'METRIC' in raw and 'absolute' in raw
            drillpoints += [(float(x),float(y)) for x,y in re.findall(r'^X(-?[\d.]+)Y(-?[\d.]+)$',raw,re.M)]
    assert all(50<=x<=50+w and -50-h<=y<=-50 for x,y in drillpoints)
    npth=next(v for v in files if 'NPTH' in v.name).read_text();assert npth.count('\nX')==2
    archive=assembly/(s['name']+'-JLCPCB-Gerbers.zip')
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None and set(z.namelist())=={f.name for f in files}
        for file in files:assert z.read(file.name)==file.read_bytes()
    record={'result':'PASS','smt_bom_and_cpl_references':len(cpl),'manual_references':len(manual),'native_placement_coordinates_match':True,'absolute_mm_origin_matches_outline_and_drill':True,'outline_mm':[w,h],'fabrication_files':len(files),'zip_crc_and_contents_match':True,'stock_reserved':False,'supplier_placement_preview_verified':False}
    (folder/'verification/package-validation.json').write_text(json.dumps(record,indent=2)+'\n');print(folder.name,'package PASS')
def manifest(folder):
    files=sorted(p for p in folder.rglob('*') if p.is_file() and p.name!='SHA256SUMS' and p.suffix not in ['.kicad_prl','.pyc'] and '__pycache__' not in p.parts and not p.name.startswith('core'))
    (folder/'SHA256SUMS').write_text(''.join(sha(p)+'  '+str(p.relative_to(folder))+'\n' for p in files))
if __name__=='__main__':
    # Native SVG/SES exports add trailing spaces; trim lexical whitespace only.
    for file in ROOT.rglob('*'):
        if file.suffix in ['.svg','.ses']:
            file.write_text('\n'.join(line.rstrip() for line in file.read_text().splitlines())+'\n')
    for folder in ROOT.iterdir():
        if (folder/'verification/design.json').exists():verify(folder);manifest(folder)
    modules={folder.name:{'electrical':json.loads((folder/'verification/validation.json').read_text()),'assembly':json.loads((folder/'assembly/assembly-validation.json').read_text()),'labels':json.loads((folder/'verification/label-validation.json').read_text()),'short_guide':json.loads((folder/'verification/guide-language-review.json').read_text())} for folder in ROOT.iterdir() if (folder/'verification/validation.json').exists()}
    summary={'result':'PASS','kicad_version':'9.0.2','module_dependencies':[],'modules':modules,'total_smt_placements':sum(v['assembly']['smt_placements_per_board'] for v in modules.values()),'total_physical_numbered_pads':sum(v['electrical']['physical_numbered_pads'] for v in modules.values()),'total_numbered_connector_pins':sum(v['labels']['numbered_external_pins'] for v in modules.values()),'hardware_tested':False,'supplier_placement_preview_verified':False}
    (ROOT/'review/validation-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    manifest(ROOT)
