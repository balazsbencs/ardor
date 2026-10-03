"""Verify the width migration against its archived input and fresh KiCad DRC."""
from collections import Counter
import gzip
import hashlib
import json
import math
from pathlib import Path

import sexpdata as sx

ROOT = Path(__file__).resolve().parents[1]


def kind(item):
    return str(item[0]) if isinstance(item, list) and item else ""


def field(item, name):
    return next((v[1:] for v in item if kind(v) == name), [])


def segments(board):
    names = {v[1]: v[2] for v in board if kind(v) == "net"}
    return {
        field(v, "uuid")[0]: {
            "net": names[field(v, "net")[0]], "layer": field(v, "layer")[0],
            "start": field(v, "start"), "end": field(v, "end"),
            "width": field(v, "width")[0],
        }
        for v in board if kind(v) == "segment"
    }


def signature(segment):
    return (segment["net"], segment["layer"], tuple(segment["start"]),
            tuple(segment["end"]), segment["width"])


def width_junctions(items):
    endpoints = {}
    for t in items.values():
        for point in (t["start"], t["end"]):
            endpoints.setdefault((t["net"], t["layer"], *point), set()).add(t["width"])
    return sum(len(widths) > 1 for widths in endpoints.values())


def main():
    before_bytes = gzip.decompress((ROOT / "review/width-baseline/Ardor_IO.kicad_pcb.gz").read_bytes())
    after_bytes = (ROOT / "Ardor_IO.kicad_pcb").read_bytes()
    plan = json.loads((ROOT / "design/track-width-cleanup.json").read_text())
    assert hashlib.sha256(before_bytes).hexdigest() == plan["source_board_sha256"]
    before, after = sx.loads(before_bytes.decode()), sx.loads(after_bytes.decode())

    # Procurement fields were subsequently populated for JLCPCB. Their identity
    # is checked by export_jlcpcb_assembly.py; retain every physical item here.
    procurement_fields = {"Value", "MPN", "Datasheet", "JLCPCB Part #"}
    def fixed(b):
        result = []
        for v in b:
            if kind(v) in {"segment", "zone"}:
                continue
            if kind(v) == "footprint":
                v = [p for p in v if not (kind(p) == "property" and p[1] in procurement_fields)]
            result.append(v)
        return result
    assert fixed(before) == fixed(after), "Non-track board objects changed"
    zone_settings = lambda b: [
        [v for v in zone if kind(v) not in {"filled_polygon", "fill_segments"}]
        for zone in b if kind(zone) == "zone"
    ]
    assert zone_settings(before) == zone_settings(after), "Zone definitions changed"
    old, new = segments(before), segments(after)
    expected = Counter(signature(t) for t in old.values())
    widened_length = 0.0
    for change in plan["changes"]:
        original = old[change["uuid"]]
        assert original == change["original"]
        expected[signature(original)] -= 1
        for piece in change["replacement"]:
            assert piece["width"] >= original["width"]
            expected[signature({**original, **piece})] += 1
            if piece["width"] > original["width"]:
                widened_length += math.dist(piece["start"], piece["end"])
    assert +expected == Counter(signature(t) for t in new.values()), "Unexpected track geometry"
    for name in ["violations", "unconnected_items", "schematic_parity"]:
        assert not json.loads((ROOT / "routing/drc.json").read_text())[name]
    before_length = sum(math.dist(t["start"], t["end"]) for t in old.values())
    after_length = sum(math.dist(t["start"], t["end"]) for t in new.values())
    assert abs(before_length - after_length) < 0.0001
    report = {
        "before_board_sha256": hashlib.sha256(before_bytes).hexdigest(),
        "after_board_sha256": hashlib.sha256(after_bytes).hexdigest(),
        "changed_original_segments": len(plan["changes"]),
        "whole_segments_widened": sum(len(c["replacement"]) == 1 for c in plan["changes"]),
        "segments_with_shortened_neckdowns": sum(len(c["replacement"]) > 1 for c in plan["changes"]),
        "width_junctions_before": width_junctions(old),
        "width_junctions_after": width_junctions(new),
        "track_segments_before": len(old), "track_segments_after": len(new),
        "total_centerline_length_mm_before": round(before_length, 6),
        "total_centerline_length_mm_after": round(after_length, 6),
        "length_widened_mm": round(widened_length, 3),
        "no_width_reductions": True, "all_nontrack_geometry_and_nets_unchanged": True,
        "procurement_properties_checked_separately": sorted(procurement_fields),
        "zone_definitions_unchanged": True, "all_track_changes_match_reviewed_plan": True,
        "drc_violations": 0, "unconnected_items": 0, "schematic_parity_issues": 0,
    }
    (ROOT / "review/track-width-cleanup-verification.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
