"""Export router geometry in a fresh process.

KiCad 9.0.2's DSN converter asserts when polygonizing a DIP silkscreen notch
arc. Strip non-copper footprint arcs from this temporary router copy only.
Manufacturing CAD retains the exact original footprint graphics and pads.
"""
import wx
app=wx.App(False)
import pcbnew as p
from pathlib import Path
import sys
import sexpdata as sx
ROOT=Path(__file__).resolve().parents[1];folder=ROOT/sys.argv[1]
key=lambda v:str(v[0]) if isinstance(v,list) and v else ''
get=lambda v,k:next((x for x in v if key(x)==k),None)
data=sx.loads((folder/'routing/placement.kicad_pcb').read_text())
for f in data:
    if key(f)=='footprint':
        f[:]=[v for v in f if not (key(v)=='fp_arc' and get(v,'layer')[1] not in ['F.Cu','B.Cu','Edge.Cuts'])]
file=folder/'routing/router-copy.kicad_pcb';file.write_text(sx.dumps(data))
b=p.LoadBoard(str(file));assert p.ExportSpecctraDSN(b,str(folder/'routing/board.dsn'))
file.unlink()
