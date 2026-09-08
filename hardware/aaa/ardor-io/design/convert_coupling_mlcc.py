"""Replace the five Rubycon SMD couplers with Samsung 1206 MLCCs (KiCad 9).

Preserves schematic paths, component positions, net assignments and routing;
extends each connected front-layer track to the new pad center. Idempotent.
"""
from pathlib import Path
import os
import pcbnew as p
import wx

app = wx.App(False)
ROOT = Path(__file__).resolve().parents[1]
REFS = {'C401', 'C402', 'C502', 'C601', 'C602'}
LIB, NAME = 'Capacitor_SMD', 'C_1206_3216Metric'
VALUE = '10u / 50V X7R'
MPN = 'Samsung CL31B106KBHNNNE'
URL = 'https://media.digikey.com/pdf/Data%20Sheets/Samsung%20PDFs/CL31B106KBHNNNE_Spec.pdf'
b = p.LoadBoard(str(ROOT / 'Ardor_IO.kicad_pcb'))
removed = []
for old in list(b.GetFootprints()):
    ref = old.GetReference()
    if ref not in REFS:
        continue
    if str(old.GetFPID().GetLibNickname()) == LIB and str(old.GetFPID().GetLibItemName()) == NAME:
        continue
    assert str(old.GetFPID().GetLibNickname()) == 'Ardor_Capacitor', ref
    library_root = Path(os.environ.get('KICAD9_FOOTPRINT_DIR', '/usr/share/kicad/footprints'))
    fp = p.FootprintLoad(str(library_root / (LIB + '.pretty')), NAME)
    assert fp, NAME
    fp.SetReference(ref)
    fp.SetValue(VALUE)
    fp.SetFPID(p.LIB_ID(LIB, NAME))
    fp.SetPath(old.GetPath())
    fp.SetSheetname(old.GetSheetname())
    fp.SetSheetfile(old.GetSheetfile())
    fp.SetPosition(old.GetPosition())
    fp.SetOrientationDegrees(old.GetOrientationDegrees())
    fp.Reference().SetLayer(p.F_Fab)
    fp.Reference().SetVisible(False)
    fp.Value().SetVisible(False)
    for name, value in [('MPN', MPN), ('JLCPCB Part #', 'C89632'), ('Datasheet', URL)]:
        field = fp.GetFieldByName(name)
        if field is None:
            field = p.PCB_FIELD(fp, fp.GetNextFieldId(), name)
            fp.AddField(field)
            field = fp.GetFieldByName(name)
        field.SetText(value)
        field.SetVisible(False)
    pads = {pad.GetNumber(): pad for pad in old.Pads()}
    for pad in fp.Pads():
        previous = pads[pad.GetNumber()]
        pad.SetNet(previous.GetNet())
        origin, dest = previous.GetPosition(), pad.GetPosition()
        attached = False
        for track in b.GetTracks():
            if isinstance(track, p.PCB_VIA) or track.GetLayer() != p.F_Cu:
                continue
            if track.GetNetCode() != pad.GetNetCode():
                continue
            if track.GetStart() == origin:
                track.SetStart(dest)
                attached = True
            if track.GetEnd() == origin:
                track.SetEnd(dest)
                attached = True
        assert attached, (ref, pad.GetNumber())
    b.Remove(old)
    removed.append(old)  # Keep SWIG objects alive until the board is saved.
    b.Add(fp)
    print('Converted', ref)
b.BuildConnectivity()
p.ZONE_FILLER(b).Fill(b.Zones())
p.SaveBoard(str(ROOT / 'Ardor_IO.kicad_pcb'), b)
