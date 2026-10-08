#!/usr/bin/env python3
"""Compare immutable frame work and separately audit removed birth scans."""
from pathlib import Path
import csv
import sys
root=Path(sys.argv[1])
a=list(csv.DictReader((root/'profile-frame-counts.csv').open()))
b=list(csv.DictReader((root/'candidate-run/profile-candidate-frame-counts.csv').open()))
assert len(a)==len(b)==5247
fields=['sample','frame_stamp','frame_size','channel','bins','peaks','phase','logarithmic','phase_reuse','predicted','matched','births','selected']
for x,y in zip(a,b):
    assert all(x[k]==y[k] for k in fields)
    assert int(y['snapshots'])==bool(int(y['births']))
    assert int(y['snapshot_reads'])==512*int(y['snapshots'])
    assert int(y['first_steps'])==int(y['fallback_steps'])==0
print('All 5247 immutable per-frame DSP/count records match across original and candidate.')
for size in ('1024','2048','4096'):
    original=[x for x in a if x['frame_size']==size]
    current=[x for x in b if x['frame_size']==size]
    old=sum(int(x['first_steps'])+int(x['fallback_steps']) for x in original)
    reads=sum(int(x['snapshot_reads']) for x in current)
    builds=sum(int(x['snapshots']) for x in current)
    print(f'N={size} original_birth_scan_iterations={old} candidate_snapshots={builds} snapshot_track_reads={reads}')
