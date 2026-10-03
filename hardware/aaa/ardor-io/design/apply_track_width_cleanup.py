"""Apply the reviewed width-only plan to an exact source board, then refill zones.

Requires KiCad 9 Python and wx; use xvfb-run on a headless machine. This writes a
separate candidate board. Run KiCad DRC with the matching schematic and project
before promoting the candidate. The plan is a migration, not an autorouter.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path

import pcbnew as pcb
import wx


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument(
        "--plan", type=Path,
        default=Path(__file__).with_name("track-width-cleanup.json"),
    )
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve():
        parser.error("Use a separate output board; validate it before replacement.")
    plan = json.loads(args.plan.read_text())
    assert hashlib.sha256(args.source.read_bytes()).hexdigest() == plan["source_board_sha256"], \
        "Source board differs from the reviewed input; do not apply this migration."

    app = wx.App(False)
    board = pcb.LoadBoard(str(args.source))
    tracks = {str(t.m_Uuid.AsString()): t for t in board.GetTracks()}
    added = 0
    for change in plan["changes"]:
        track = tracks[change["uuid"]]
        old = change["original"]
        assert not isinstance(track, pcb.PCB_VIA)
        assert track.GetNetname() == old["net"]
        assert board.GetLayerName(track.GetLayer()) == old["layer"]
        assert track.GetWidth() == round(old["width"] * 1_000_000)
        assert track.GetStart() == pcb.VECTOR2I(*(round(x * 1_000_000) for x in old["start"]))
        assert track.GetEnd() == pcb.VECTOR2I(*(round(x * 1_000_000) for x in old["end"]))
        pieces = change["replacement"]
        assert pieces[0]["start"] == old["start"]
        assert pieces[-1]["end"] == old["end"]
        length = math.dist(old["start"], old["end"])
        previous = old["start"]
        total = 0.0
        # These checks forbid rerouting, gaps, reversals and width reductions.
        for piece in pieces:
            assert piece["start"] == previous
            assert piece["width"] >= old["width"]
            distance = math.dist(piece["start"], piece["end"])
            assert distance > 0
            for point in [piece["start"], piece["end"]]:
                det = ((point[0] - old["start"][0]) * (old["end"][1] - old["start"][1])
                       - (point[1] - old["start"][1]) * (old["end"][0] - old["start"][0]))
                assert abs(det) / length < 0.000002
            previous = piece["end"]
            total += distance
        assert abs(total - length) < 0.000002
        for index, piece in enumerate(pieces):
            item = track if index == 0 else pcb.PCB_TRACK(board)
            if index:
                item.SetNet(track.GetNet())
                item.SetLayer(track.GetLayer())
                item.SetLocked(track.IsLocked())
                board.Add(item)
                added += 1
            item.SetStart(pcb.VECTOR2I(*(round(x * 1_000_000) for x in piece["start"])))
            item.SetEnd(pcb.VECTOR2I(*(round(x * 1_000_000) for x in piece["end"])))
            item.SetWidth(round(piece["width"] * 1_000_000))

    board.BuildConnectivity()
    pcb.ZONE_FILLER(board).Fill(board.Zones())
    pcb.SaveBoard(str(args.output), board)
    print(f"Applied {len(plan['changes'])} reviewed segment changes; added {added} split segments.")


if __name__ == "__main__":
    main()
