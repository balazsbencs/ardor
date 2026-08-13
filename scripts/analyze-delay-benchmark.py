#!/usr/bin/env python3
"""Summarize pedal-delay-bench CSV without third-party dependencies."""

import argparse
import csv
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("csv", type=Path)
    args = parser.parse_args()

    with args.csv.open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(line for line in stream if not line.startswith("#")))
    if not rows:
        print(f"No benchmark rows in {args.csv}", file=sys.stderr)
        return 1

    print("Hosted delay benchmark summary")
    print()
    print("mode       scenario    frames   mean us   p99.9 us   mean budget   p99.9 budget   misses")
    print("---------- ----------- ------ --------- ---------- ------------- --------------- --------")
    for row in rows:
        print(
            f"{row['mode']:<10} {row['scenario']:<11} {int(row['block_frames']):>6} "
            f"{float(row['mean_us']):>9.2f} {float(row['p999_us']):>10.2f} "
            f"{float(row['mean_budget_pct']):>12.2f}% "
            f"{float(row['p999_budget_pct']):>14.2f}% {int(row['deadline_misses']):>8}"
        )

    hottest = sorted(rows, key=lambda row: float(row["p999_budget_pct"]), reverse=True)[:8]
    print()
    print("Hottest cases by p99.9 callback budget")
    for row in hottest:
        print(
            f"  {row['mode']}/{row['scenario']} {row['block_frames']}f: "
            f"mean {float(row['mean_budget_pct']):.2f}%, "
            f"p99.9 {float(row['p999_budget_pct']):.2f}%, "
            f"max {float(row['max_us']):.2f} us"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
