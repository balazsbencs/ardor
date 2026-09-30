"""Regenerate source CAD and validate Rev C in separate KiCad processes.
Requires KiCad 9 CLI/Python libraries, sexpdata, wx and a GUI display (Xvfb works).
Does not refresh live stock, alter the full revision or place an order.
"""
from pathlib import Path
import subprocess,sys,shutil
r=Path(__file__).resolve().parents[1]
def run(*args):subprocess.run(args,cwd=r,check=True)
run(sys.executable,'design/build.py')
run('kicad-cli','sch','export','netlist','--format','kicadxml','-o','verification/netlist.xml','Ardor_IO.kicad_sch')
run('kicad-cli','sch','erc','--severity-all','--exit-code-violations','--format','json','-o','verification/erc.json','Ardor_IO.kicad_sch')
run(sys.executable,'design/build_pcb.py')
# Loading/filling after the layout builder has exited avoids stale zone caches.
run(sys.executable,'design/refill_board.py')
run('kicad-cli','pcb','drc','--schematic-parity','--all-track-errors','--severity-all','--exit-code-violations','--format','json','-o','routing/drc.json','Ardor_IO.kicad_pcb')
shutil.copy2(r/'verification/netlist.xml',r/'review/fresh-netlist.xml')
shutil.copy2(r/'verification/erc.json',r/'review/fresh-erc.json')
run(sys.executable,'design/verify.py')
run(sys.executable,'design/verify_geometry.py')
run('kicad-cli','pcb','export','pos','--format','csv','--units','mm','--smd-only','-o','assembly/kicad-smt-positions.csv','Ardor_IO.kicad_pcb')
run(sys.executable,'design/export_jlcpcb_assembly.py')
run('kicad-cli','sch','export','pdf','-o','Ardor_IO.pdf','Ardor_IO.kicad_sch')
print('PASS: regenerated source CAD, ERC/DRC/parity, pad/net audit and assembly tables')
