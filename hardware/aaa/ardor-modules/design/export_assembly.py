"""Validate selected JLC stock/parts and export per-module SMT BOM and CPL."""
from pathlib import Path
import csv,json,hashlib,math,xml.etree.ElementTree as ET
import pcbnew as p
ROOT=Path(__file__).resolve().parents[1]
def sha(file):return hashlib.sha256(file.read_bytes()).hexdigest()
def export(folder):
    spec=json.loads((folder/'verification/design.json').read_text());assembly=folder/'assembly'
    stock=json.loads((assembly/'inventory-snapshot.json').read_text())
    parts=json.loads((assembly/'parts.json').read_text())['parts'];lookup={r:v for v in parts for r in v['references']}
    boardfile=folder/(spec['name']+'.kicad_pcb');b=p.LoadBoard(str(boardfile));fps={f.GetReference():f for f in b.GetFootprints()}
    smt={r for r,f in fps.items() if f.GetAttributes() & p.FP_SMD and not f.GetAttributes() & p.FP_BOARD_ONLY}
    assert smt==set(lookup),(folder,'SMT coverage',smt^set(lookup))
    assert all(f.GetLayer()==p.F_Cu for r,f in fps.items() if r in smt)
    drc=json.loads((folder/'verification/drc.json').read_text());erc=json.loads((folder/'verification/erc.json').read_text())
    assert not drc['violations'] and not drc['unconnected_items'] and not drc['schematic_parity']
    assert not any(s['violations'] for s in erc['sheets'])
    bycode={v['componentCode']:v for v in stock['parts']};cost=0;extended=0;rows=[]
    for part in parts:
        item=bycode[part['jlcpcb_part']]
        assert item['componentModelEn']==part['mpn'],(part['jlcpcb_part'],'MPN changed')
        assert item['assemblyMode']=='smtWeld' and item['isBuyComponent'],item
        assert item['componentLibraryType'] in ['base','expand']
        needed=max(2*len(part['references'])+int(item.get('lossNumber') or 0),int(item.get('leastPatchNumber') or 0))
        available=min(int(item['overseasStockCount']),int(item['canPresaleNumber']))
        assert available>=needed,(part['jlcpcb_part'],'insufficient stock',needed,available)
        for ref in part['references']:
            assert spec['parts'][ref]['fp']==part['footprint']
            assert (str(fps[ref].GetFPID().GetLibNickname())+':'+str(fps[ref].GetFPID().GetLibItemName()))==part['footprint']
            assert fps[ref].GetFieldText('MPN')==part['bom_mpn']
            assert fps[ref].GetFieldText('JLCPCB Part #')==part['jlcpcb_part']
        cost+=float(item['initialPrice'])*needed;extended+=item['componentLibraryType']=='expand'
        rows.append({'Comment':spec['parts'][part['references'][0]]['value'],'Designator':','.join(part['references']),'Footprint':part['footprint'].split(':')[1],'LCSC Part #':part['jlcpcb_part']})
    with (assembly/'jlcpcb-bom.csv').open('w') as f:
        w=csv.DictWriter(f,fieldnames=['Comment','Designator','Footprint','LCSC Part #'],lineterminator='\n');w.writeheader();w.writerows(rows)
    with (assembly/'jlcpcb-cpl.csv').open('w') as f:
        w=csv.writer(f,lineterminator='\n');w.writerow(['Designator','Mid X','Mid Y','Layer','Rotation'])
        for ref in sorted(smt):
            fp=fps[ref];pos=fp.GetPosition()
            # Absolute Gerber/drill origin, mm, Y up; do not mix with aux-origin exports.
            w.writerow([ref,f'{p.ToMM(pos.x):.4f}mm',f'{-p.ToMM(pos.y):.4f}mm','Top',f'{fp.GetOrientationDegrees()%360:.3f}'])
    with (assembly/'manual-bom.csv').open('w') as f:
        w=csv.writer(f,lineterminator='\n');w.writerow(['Reference','Description','Footprint','Quantity per board'])
        for ref,part in spec['parts'].items():
            if ref not in smt:w.writerow([ref,part['mpn'],part['fp'],1])
        if folder.name=='expression':w.writerow(['Shunts','2.54 mm shunt: JP301 x2, JP302 x1; JP303/304 optional x2','',5])
    report={'board_quantity':2,'smt_placements_per_board':len(smt),'unique_smt_parts':len(parts),'basic_types':len(parts)-extended,'extended_types':extended,'manual_references':sorted(set(spec['parts'])-smt),'stock_check_utc':stock['checked_utc'],'stock_reserved':False,'component_cost_with_assembly_attrition_usd':round(cost,2),'economic_extended_feeders_usd':round(extended*3.07,2),'optional_qfn_xray_for_two_usd':3.28 if folder.name=='headphones' else 0,'estimate_excludes':['PCB fabrication','assembly setup','stencil','solder joints','shipping','tax','manual parts','discounts'],'placement_preview_verified':False,'rotation_convention':'Native KiCad CCW; inspect manufacturer pin 1 in JLC placement preview','origin':'Absolute Gerber origin; mm; CPL Y up','hashes':{'pcb_sha256':sha(boardfile),'schematic_sha256':sha(folder/(spec['name']+'.kicad_sch'))}}
    (assembly/'assembly-validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(folder.name,len(smt),'SMT placements,',extended,'Extended types; component estimate',round(cost,2))
if __name__=='__main__':
    for folder in ROOT.iterdir():
        if (folder/'verification/design.json').exists():export(folder)
