"""Compare this release to a separately regenerated copy of the family.

Run the complete documented workflow in the copy, reusing frozen board.ses,
then pass the copy's family directory as the only argument. Board UUIDs and
polygon vertex ordering may differ. Non-fill geometry must match exactly;
filled copper may differ by at most two native nanometre coordinate units.
"""
from pathlib import Path
import hashlib
import json
import sys
import sexpdata as sx
from shapely.geometry import Polygon
from shapely.ops import unary_union

ROOT = Path(__file__).resolve().parents[1]
CANDIDATE = Path(sys.argv[1]).resolve()
assert CANDIDATE != ROOT

def key(x):
    return str(x[0]) if isinstance(x, list) and x else ''

def normalize(x):
    if not isinstance(x, list):
        return x
    return [normalize(v) for v in x if key(v) not in ['uuid', 'tstamp', 'filled_polygon']]

def geometry(file):
    result = {}
    for value in normalize(sx.loads(file.read_text())):
        result.setdefault(key(value), []).append(sx.dumps(value))
    return {k: sorted(v) for k, v in result.items()}

def copper(file):
    result = {}
    for zone in sx.loads(file.read_text()):
        if key(zone) != 'zone':
            continue
        for filled in zone:
            if key(filled) != 'filled_polygon':
                continue
            layer = next(v[1] for v in filled if key(v) == 'layer')
            points = next(v for v in filled if key(v) == 'pts')
            shape = Polygon([v[1:] for v in points[1:]])
            if not shape.is_valid:
                shape = shape.buffer(0)
            result.setdefault(layer, []).append(shape)
    return {k: unary_union(v) for k, v in result.items()}

reports = {}
for folder in sorted(ROOT.iterdir()):
    pcb = next(folder.glob('*.kicad_pcb'), None) if folder.is_dir() else None
    if pcb is None:
        continue
    other = CANDIDATE / folder.name
    spec = json.loads((folder / 'verification/design.json').read_text())
    files = [spec['name'] + '.kicad_sch', 'BOM.csv', 'README.md',
             'assembly/accessories.csv', 'verification/design.json']
    for name in files:
        assert (folder / name).read_bytes() == (other / name).read_bytes(), (folder.name, name)
    assert geometry(pcb) == geometry(other / pcb.name), folder.name
    a, b = copper(pcb), copper(other / pcb.name)
    assert a.keys() == b.keys()
    differences = {layer: a[layer].symmetric_difference(b[layer]).area for layer in a}
    # KiCad fill/simplification can shift a rounded intersection by 1-2 nm
    # even with identical pads/tracks/zones. Bound both boundary displacement
    # and total changed area; this is not a general layout tolerance.
    tolerance_mm=.000002
    boundary={layer:a[layer].boundary.hausdorff_distance(b[layer].boundary) for layer in a}
    assert all(v<=tolerance_mm for v in boundary.values()),(folder.name,boundary)
    assert all(v<=.000001 for v in differences.values()),(folder.name,differences)
    assert all(a[layer].difference(b[layer].buffer(tolerance_mm)).area==0
               and b[layer].difference(a[layer].buffer(tolerance_mm)).area==0 for layer in a)
    for name in ['validation', 'hand-assembly', 'package-validation']:
        assert json.loads((other / 'verification' / (name + '.json')).read_text())['result'] == 'PASS'
    reports[folder.name] = {
        'result': 'PASS', 'byte_identical_files': files,
        'pcb_geometry_and_nets_identical_excluding_uuids': True,
        'filled_copper_symmetric_difference_mm2': differences,
        'filled_copper_max_boundary_drift_mm':boundary,
        'filled_copper_coordinate_tolerance_mm':tolerance_mm,
        'release_pcb_sha256': hashlib.sha256(pcb.read_bytes()).hexdigest(),
        'regenerated_native_checks': 'PASS',
    }
    print(folder.name, 'regeneration comparison PASS')
assert len(reports) == 5
(ROOT / 'review/regeneration-check.json').write_text(json.dumps({
    'result': 'PASS', 'kicad_version': '9.0.2',
    'routing': 'Frozen board.ses imported into freshly generated placements',
    'modules': reports,
}, indent=2) + '\n')
