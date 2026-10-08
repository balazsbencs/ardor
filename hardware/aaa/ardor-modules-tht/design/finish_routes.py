"""Finish freshly placed boards using their frozen candidate sessions."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
folders = [p for p in ROOT.iterdir() if (p/'verification/design.json').exists()]
def native(script,folder):
    subprocess.run(['xvfb-run','-a',sys.executable,str(ROOT/'design'/script),folder.name],check=True)
for folder in folders:
    native('import_routes.py',folder)
subprocess.run([sys.executable,str(ROOT/'design/isolation_rules.py')],check=True)
for folder in folders:
    native('stitch_ground.py',folder)
    subprocess.run([sys.executable,str(ROOT/'design/trim_spurs.py'),folder.name],check=True)
    native('normalize_widths.py',folder)
    native('label_boards.py',folder)
    native('label_components.py',folder)
subprocess.run([sys.executable,str(ROOT/'design/drc.py')],check=True)
