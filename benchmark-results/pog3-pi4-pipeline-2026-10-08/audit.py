#!/usr/bin/env python3
"""Validate archived counters/generations against their original timing traces."""
import csv
import gzip
import math
import pathlib
import sys


def rows(path):
    candidate = path if path.exists() else path.with_suffix(path.suffix + ".gz")
    opener = gzip.open if candidate.suffix == ".gz" else open
    with opener(candidate, "rt", newline="") as handle:
        return list(csv.DictReader(handle))


def check(folder, receipt_name):
    receipts = rows(folder / receipt_name)
    for receipt in receipts:
        label = receipt["label"]
        summary, = rows(folder / (label + ".summary.csv"))
        code = int(receipt["return_code"])
        failed = int(summary["complete"]) == 0
        assert code == (3 if failed else 0), (label, "exit/complete mismatch")
        capture, playback = int(summary["capture_xruns"]), int(summary["playback_xruns"])
        late = int(summary["worker_late"])
        assert not failed or capture + playback + late > 0, (label, "unexplained failure")
        assert int(summary["warmup_included"]) == int(failed), label
        callbacks = rows(folder / (label + ".callbacks.csv"))
        assert len(callbacks) == int(summary["callbacks"]), label
        wall = [float(row["wall_us"]) for row in callbacks]
        assert all(math.isfinite(value) and value >= 0 for value in wall), label
        assert abs(sum(wall) / len(wall) - float(summary["mean_us"])) < .01, label
        assert abs(max(wall) - float(summary["max_us"])) < .01, label
        assert sum(int(row["playback_xruns"]) for row in callbacks) == playback, label
        pipeline = summary["mode"] in ("pipeline", "pipeline2")
        if pipeline:
            delay = int(summary["additional_delay_frames"]) // int(summary["frames"])
            jobs = rows(folder / (label + ".worker.csv"))
            assert len(jobs) == int(summary["worker_completed"]) == int(summary["worker_submitted"]), label
            for generation, job in enumerate(jobs, 1):
                assert int(job["generation"]) == generation and int(job["cpu"]) == 3, label
                assert int(job["submitted_ns"]) <= int(job["start_ns"]) <= int(job["end_ns"]), label
            first = 1 if failed else 4 * 48000 // int(summary["frames"]) + 1
            for generation, callback in enumerate(callbacks, first):
                assert int(callback["submitted_generation"]) == generation, label
                assert int(callback["output_generation"]) == max(0, generation - delay), label
            assert int(summary["worker_consumed"]) == max(0, int(summary["worker_submitted"]) - delay), label
            assert int(summary["worker_submission_misses"]) == int(summary["worker_wrong_output"]) == 0, label
            selected = jobs if failed else jobs[first - 1:]
            work = [(int(job["end_ns"]) - int(job["start_ns"])) / 1000 for job in selected]
            wakeup = [(int(job["start_ns"]) - int(job["submitted_ns"])) / 1000 for job in selected]
            completion = [(int(job["end_ns"]) - int(job["submitted_ns"])) / 1000 for job in selected]
            for actual, key in ((sum(work) / len(work), "worker_mean_us"),
                                (max(work), "worker_max_us"),
                                (max(wakeup), "worker_wakeup_max_us"),
                                (max(completion), "worker_submit_to_finish_max_us")):
                assert abs(actual - float(summary[key])) < .01, (label, key)
            assert sum(value > float(summary["budget_us"]) for value in completion) == int(summary["worker_over_period"]), label
            assert failed or (late == capture == playback == 0), label
        print(f"{folder.name}/{label}: traces and generation accounting pass; complete={not failed}")


if __name__ == "__main__":
    root = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(__file__).parent
    check(root, "receipt.csv")
    if (root / "soak-receipt.csv").exists():
        check(root, "soak-receipt.csv")
    initial = root / "initial-one-block" / "raw"
    if initial.exists():
        check(initial, "receipt.csv")
