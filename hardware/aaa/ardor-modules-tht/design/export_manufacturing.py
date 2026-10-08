"""Export separate two-layer fabrication packages and native board views."""
from pathlib import Path
import json,subprocess,zipfile
from concurrent.futures import ThreadPoolExecutor
ROOT=Path(__file__).resolve().parents[1]
LAYERS='F.Cu,B.Cu,F.Paste,B.Paste,F.Silkscreen,B.Silkscreen,F.Mask,B.Mask,Edge.Cuts'
def cli(args):subprocess.run(['kicad-cli',*map(str,args)],check=True,stdout=subprocess.DEVNULL)
def export(folder):
    spec=json.loads((folder/'verification/design.json').read_text());pcb=folder/(spec['name']+'.kicad_pcb');assembly=folder/'assembly';gerbers=assembly/'gerbers';gerbers.mkdir(exist_ok=True)
    drc=json.loads((folder/'verification/drc.json').read_text());assert not drc['violations'] and not drc['unconnected_items'] and not drc['schematic_parity']
    for old in gerbers.iterdir():old.unlink()
    cli(['pcb','export','gerbers','--no-x2','--no-netlist','--subtract-soldermask','--layers',LAYERS,'-o',str(gerbers)+'/',pcb])
    cli(['pcb','export','drill','--format','excellon','--drill-origin','absolute','--excellon-units','mm','--excellon-separate-th','-o',str(gerbers)+'/',pcb])
    cli(['pcb','export','pos','--format','csv','--units','mm','--smd-only','-o',assembly/'kicad-smt-positions.csv',pcb])
    for side,layers in [('front','F.Cu,F.Silkscreen,Edge.Cuts'),('back','B.Cu,B.Silkscreen,Edge.Cuts'),('assembly','F.Fab,Edge.Cuts')]:
        args=['pcb','export','svg','--layers',layers,'--page-size-mode','2','--mode-single','--exclude-drawing-sheet','-o',folder/'review'/('pcb-'+side+'.svg')]
        if side=='back':args+=['--mirror']
        if side=='assembly':args+=['--sketch-pads-on-fab-layers','--black-and-white']
        cli([*args,pcb])
    files=sorted(gerbers.iterdir());assert len(files)==12,[(p.name) for p in files]
    archive=assembly/(spec['name']+'-Gerbers.zip')
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
        for file in files:
            info=zipfile.ZipInfo(file.name,date_time=(2026,10,8,0,0,0));info.compress_type=zipfile.ZIP_DEFLATED;info.external_attr=0o644<<16;z.writestr(info,file.read_bytes())
    with zipfile.ZipFile(archive) as z:assert z.testzip() is None
    print(folder.name,'12 fabrication files; ZIP CRC PASS')
if __name__=='__main__':
    with ThreadPoolExecutor(max_workers=2) as pool:list(pool.map(export,[p for p in ROOT.iterdir() if (p/'verification/design.json').exists()]))
