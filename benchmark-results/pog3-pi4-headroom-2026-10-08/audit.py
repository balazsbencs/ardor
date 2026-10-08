#!/usr/bin/env python3
"""Validate retained headroom evidence, including the expected failed chains."""
import csv
import pathlib
import sys

from analyze import open_text

root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ".")
receipts = dict((r["label"], int(r["return_code"]))
                for r in csv.DictReader((root / "receipt.csv").open()))
assert len(receipts) == 11, receipts
summaries = {}
for label, code in receipts.items():
    summary = list(csv.DictReader((root / f"{label}.summary.csv").open()))
    assert len(summary) == 1
    row = summary[0]
    summaries[label] = row
    raw = list(csv.DictReader(open_text(root / f"{label}.callbacks.csv")))
    assert len(raw) == int(row["callbacks"])
    times = [float(r["wall_us"]) for r in raw]
    assert abs(sum(times) / len(times) - float(row["mean_us"])) < .01
    assert abs(max(times) - float(row["max_us"])) < .01
    assert sum(t > float(row["budget_us"]) for t in times) == int(row["over_period"])
    assert all(int(r["start_ns"]) < int(s["start_ns"]) for r, s in zip(raw, raw[1:]))
    if label in ("combined-1", "combined-2"):
        assert code == 3 and row["complete"] == "0" and row["warmup_included"] == "1"
        assert len(raw) == 11 and row["playback_xruns"] == "1"
    else:
        assert code == 0 and row["complete"] == "1" and row["warmup_included"] == "0"
        assert row["capture_xruns"] == row["playback_xruns"] == "0"
    print(f"{label}: exit={code}, callbacks={len(raw)}, trace/summary agree")
for a, b in [("core-1", "core-2"), ("chain-1", "chain-2"),
             ("combined-offline-1", "combined-offline-2"), ("core-sample", "core-count")]:
    assert summaries[a]["checksum"] == summaries[b]["checksum"]
    print(f"equal-duration checksum match: {a} / {b}")
counters = list(csv.DictReader((root / "core-count.counters.csv").open()))
assert len(counters) == 5
assert all(int(r["value"]) > 0 and r["enabled_ns"] == r["running_ns"] for r in counters)
assert "sample_lost=0" in (root / "core-sample.log").read_text()
assert (root / "service-readback.txt").read_text().strip().isdigit()
print("Five positive, unmultiplexed counters; no sample loss; restored service PID recorded.")
