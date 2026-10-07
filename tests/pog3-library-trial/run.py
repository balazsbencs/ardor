#!/usr/bin/env python3
"""Run the optional DSP library screening sequentially; retain every row."""
import argparse
import csv
import io
import json
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--phase", choices=["timing", "allocation", "quality"], required=True)
    parser.add_argument("--probe", type=Path)
    parser.add_argument("--passes", type=int, choices=[1, 2], default=1)
    parser.add_argument("--backends", nargs="+", choices=["ardor", "ss-default", "ss-cheap", "ss-pog3", "ss-balanced", "rb-r2", "rb-r3", "rb-live", "terrarium-48", "terrarium-80", "erb-ps2"])
    args = parser.parse_args()
    if args.phase == "allocation" and not args.probe:
        parser.error("allocation phase requires --probe")
    env = os.environ.copy()
    env.pop("LD_PRELOAD", None)
    if args.phase == "allocation":
        env["LD_PRELOAD"] = str(args.probe.resolve())
    args.destination.mkdir(parents=True, exist_ok=True)
    backends = ["ardor", "ss-default", "ss-cheap", "ss-pog3", "ss-balanced", "rb-r2", "rb-r3", "rb-live"]
    cases = []
    for callback in ([128] if args.phase == "quality" else [64, 128]):
        for kind in (["tone", "resolved", "low", "alias"] if args.phase == "quality" else ["static", "dynamic"]):
            for count in ([8] if args.phase == "quality" else [5, 8]):
                for backend in backends:
                    if backend != "ardor" or count == 8:
                        cases.append((backend, count, callback, kind))
    for callback in ([128] if args.phase == "quality" else [64, 128]):
        for kind in (["tone", "resolved", "low", "alias"] if args.phase == "quality" else ["static"]):
            for backend in ["terrarium-48", "terrarium-80"]:
                cases.append((backend, 3, callback, kind))
    # Preserve the original 64-row library screening unless this new reference
    # is explicitly requested. Selected backends keep the same case contracts.
    if args.backends:
        if "erb-ps2" in args.backends:
            for callback in ([128] if args.phase == "quality" else [64, 128]):
                for kind in (["tone", "resolved", "low", "alias"] if args.phase == "quality" else ["static"]):
                    cases.append(("erb-ps2", 1, callback, kind))
        cases = [case for case in cases if case[0] in args.backends]
    receipts = []
    for pass_index in range(args.passes):
        order = cases if pass_index % 2 == 0 else list(reversed(cases))
        output = args.destination / f"{args.phase}-{pass_index + 1}.csv"
        with output.open("w", newline="") as file:
            writer = None
            for backend, count, callback, kind in order:
                command = [str(args.executable.resolve()), backend, str(count), str(callback), kind]
                if args.phase == "allocation":
                    command.append("--allocation")
                completed = subprocess.run(command, env=env, capture_output=True, text=True)
                label = f"{args.phase}-{pass_index + 1}-{backend}-{count}-{callback}-{kind}"
                (args.destination / f"{label}.log").write_text(completed.stderr)
                receipts.append({"command": command, "returncode": completed.returncode, "log": label})
                (args.destination / f"{args.phase}-receipts.json").write_text(json.dumps(receipts, indent=2) + "\n")
                if completed.returncode:
                    (args.destination / f"{label}.stdout").write_text(completed.stdout)
                    raise RuntimeError(f"trial failed: {label}; see retained log")
                rows = list(csv.DictReader(io.StringIO(completed.stdout)))
                if not rows:
                    raise RuntimeError(f"trial returned no rows: {label}")
                if writer is None:
                    writer = csv.DictWriter(file, fieldnames=rows[0].keys())
                    writer.writeheader()
                writer.writerows(rows)
                file.flush()
                print(label, rows[0].get("cpu_percent", rows[0].get("target_level_db")), flush=True)


if __name__ == "__main__":
    main()
