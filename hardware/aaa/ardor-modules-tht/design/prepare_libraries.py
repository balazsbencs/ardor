"""Freeze every footprint before ERC; projects open without global libraries."""
from pathlib import Path
import json
import shutil
import sexpdata as sx
from cad import ROOT, key, get

for folder in ROOT.iterdir():
    if not (folder/'verification/design.json').exists():
        continue
    spec = json.loads((folder/'verification/design.json').read_text())
    footprints = {c['fp'] for c in spec['parts'].values()}
    footprints.add('MountingHole:MountingHole_2.2mm_M2')
    aliases = set()
    for footprint in footprints:
        alias, name = footprint.split(':')
        aliases.add(alias)
        dest = folder/'footprints'/(alias+'.pretty')/(name+'.kicad_mod')
        dest.parent.mkdir(parents=True,exist_ok=True)
        source = ROOT/'design/footprints'/(alias+'.pretty')/(name+'.kicad_mod')
        if not source.exists():
            source = Path('/usr/share/kicad/footprints')/(alias+'.pretty')/(name+'.kicad_mod')
        tree = sx.loads(source.read_text())
        tree[:] = [v for v in tree if not(key(v)=='fp_text' and str(v[1])=='user' and v[2]=='${REFERENCE}')]
        dest.write_text(sx.dumps(tree)+'\n')
    (folder/'fp-lib-table').write_text('(fp_lib_table (version 7)'+''.join(
        f'(lib (name "{a}") (type "KiCad") (uri "${{KIPRJMOD}}/footprints/{a}.pretty") (options "") (descr "Frozen THT footprint"))'
        for a in sorted(aliases))+')\n')
