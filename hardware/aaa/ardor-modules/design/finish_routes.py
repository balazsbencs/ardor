"""Import independent candidates in fresh processes, then check final boards."""
from pathlib import Path
import json, subprocess, sys
ROOT=Path(__file__).resolve().parents[1]
folders=[p for p in ROOT.iterdir() if (p/'verification/design.json').exists()]
for folder in folders:
    subprocess.run(['xvfb-run','-a',sys.executable,str(ROOT/'design/import_routes.py'),folder.name],check=True,stdout=subprocess.DEVNULL)
subprocess.run([sys.executable,str(ROOT/'design/isolation_rules.py')],check=True)
for folder in folders:
    subprocess.run(['xvfb-run','-a',sys.executable,str(ROOT/'design/stitch_ground.py'),folder.name],check=True,stdout=subprocess.DEVNULL)
for folder in folders:
    subprocess.run([sys.executable,str(ROOT/'design/trim_spurs.py'),folder.name],check=True)
    subprocess.run(['xvfb-run','-a',sys.executable,str(ROOT/'design/normalize_widths.py'),folder.name],check=True,stdout=subprocess.DEVNULL)
    subprocess.run(['xvfb-run','-a',sys.executable,str(ROOT/'design/label_boards.py'),folder.name],check=True,stdout=subprocess.DEVNULL)
subprocess.run([sys.executable,str(ROOT/'design/drc.py')],check=True)
