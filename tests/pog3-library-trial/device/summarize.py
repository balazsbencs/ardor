#!/usr/bin/env python3
"""Validate retained device receipts and summarize every timing row."""
import csv
from pathlib import Path
import sys


def read(path):
    with path.open(newline="") as file:
        return list(csv.DictReader(file))


def write(path, rows):
    with path.open("w", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=rows[0].keys(), lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: summarize.py RETRIEVED_DIRECTORY OUTPUT_DIRECTORY")
    source, output = map(Path, sys.argv[1:])
    output.mkdir(parents=True, exist_ok=True)
    backends = ["ardor", "erb-cadence-count-32", "erb-effects-ownership-32",
                "erb-effects-attack-32", "erb-effects-freeze-32", "erb-effects-gliss-32"]
    expected = {(str(p), b, str(c), w) for p in (1, 2) for b in backends
                for c in (64, 128) for w in ("static", "dynamic", "events")}
    receipts = read(source / "receipt.csv")
    actual = {(r["pass"], r["backend"], r["callback"], r["workload"]) for r in receipts}
    if actual != expected or len(receipts) != 72 or any(r["return_code"] != "0" for r in receipts):
        raise RuntimeError("incomplete or failed device receipts")
    rows = []
    for p, backend, callback, workload in sorted(expected):
        label = f"{p}-{backend}-{callback}-{workload}"
        case = read(source / f"{label}.csv")
        if len(case) != 1 or case[0]["backend"] != backend or case[0]["callback"] != callback:
            raise RuntimeError(f"unexpected case: {label}")
        row = case[0]
        if row["workload"] != workload or row["faults"] != "0":
            raise RuntimeError(f"workload or processing fault: {label}")
        if int(row["callbacks"]) != 192000 // int(callback):
            raise RuntimeError(f"incomplete timeline: {label}")
        if backend.startswith("erb-effects-"):
            log = (source / f"{label}.log").read_text()
            if "transforms=5247 " not in log:
                raise RuntimeError(f"cold ownership analysis: {label}")
            if backend in backends[-2:] and "captures=1 " not in log:
                raise RuntimeError(f"missing capture: {label}")
            if backend == backends[-1] and workload == "events":
                targets = next(int(item.split("=", 1)[1]) for item in log.split() if item.startswith("targets="))
                if targets == 0:
                    raise RuntimeError(f"missing gliss assignments: {label}")
        rows.append({"pass": p, **row})
    for backend in backends:
        for callback in (64, 128):
            for workload in ("static", "dynamic", "events"):
                pair = [r for r in rows if r["backend"] == backend and int(r["callback"]) == callback and r["workload"] == workload]
                if pair[0]["checksum"] != pair[1]["checksum"]:
                    raise RuntimeError("paired output checksums differ")
    full = [{"pass": str(p), **r} for p in (1, 2) for r in read(source / f"full-{p}.csv")]
    if len(full) != 28 or any(r["callback_allocations"] != "0" for r in full):
        raise RuntimeError("full suite incomplete or C++ callback allocations observed")
    write(output / "trial.csv", rows)
    write(output / "full.csv", full)
    write(output / "receipt.csv", receipts)

    def span(values):
        return f"{min(values):.2f}–{max(values):.2f}%"

    lines = ["| Backend | Frames | Mean period demand | Highest case p99 µs | Worst callback µs | Overruns / callbacks |",
             "| --- | ---: | ---: | ---: | ---: | ---: |"]
    for backend in backends:
        for callback in (64, 128):
            cases = [r for r in rows if r["backend"] == backend and int(r["callback"]) == callback]
            lines.append(f"| {backend} | {callback} | {span([float(r['cpu_percent']) for r in cases])} | "
                         f"{max(float(r['p99_us']) for r in cases):.3f} | {max(float(r['max_us']) for r in cases):.3f} | "
                         f"{sum(int(r['overruns']) for r in cases)} / {sum(int(r['callbacks']) for r in cases)} |")
    lines += ["", "| Full processor workload | Frames | Mean period demand | Highest case p99 µs | Worst callback µs |",
              "| --- | ---: | ---: | ---: | ---: |"]
    for workload in dict.fromkeys(r["workload"] for r in full):
        for callback in (64, 128):
            cases = [r for r in full if r["workload"] == workload and int(r["callback_frames"]) == callback]
            budget = callback / .048
            lines.append(f"| {workload} | {callback} | {span([float(r['mean_us']) / budget * 100 for r in cases])} | "
                         f"{max(float(r['p99_us']) for r in cases):.3f} | {max(float(r['max_us']) for r in cases):.3f} |")
    lines += ["", "| Held trial | Frames | Middle-two-second mean demand |", "| --- | ---: | ---: |"]
    for backend in backends[-2:]:
        for callback in (64, 128):
            cases = [r for r in rows if r["backend"] == backend and int(r["callback"]) == callback]
            lines.append(f"| {backend} | {callback} | {span([float(r['middle_two_seconds_mean_us']) / (callback / .048) * 100 for r in cases])} |")
    (output / "tables.md").write_text("\n".join(lines) + "\n")
    print("Validated 72 stage rows, matching checksums/counters, and 28 full-suite rows.")


if __name__ == "__main__":
    main()
