#!/usr/bin/env python3
"""Summarize pedal-nam-bench CSV without third-party dependencies."""

from __future__ import annotations

import argparse
import csv
import statistics
import sys
from collections import defaultdict
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path, help="CSV emitted by pedal-nam-bench")
    parser.add_argument(
        "--tail-limit",
        type=float,
        default=70.0,
        help="maximum allowed worst-case p99.9 budget percentage (default: 70)",
    )
    return parser.parse_args()


def read_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as source:
        lines = (line for line in source if not line.startswith("#"))
        return list(csv.DictReader(lines))


def short_model(path: str) -> str:
    return Path(path).name


def main() -> int:
    args = parse_args()
    rows = read_rows(args.csv)
    if not rows:
        print(f"No benchmark rows in {args.csv}", file=sys.stderr)
        return 1

    grouped: dict[tuple[str, int], list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        grouped[(row["tier"], int(row["block_frames"]))].append(row)

    print("NAM benchmark summary")
    print()
    print(
        "tier      frames  quantum   cases  mean CPU (median/worst)  "
        "worst p99.9  misses  slowest model"
    )
    print(
        "--------- ------ --------- ------ ------------------------- "
        "------------ ------- -------------"
    )
    viable: dict[str, list[int]] = defaultdict(list)
    for (tier, frames), cases in sorted(grouped.items(), key=lambda item: (item[0][0], item[0][1])):
        mean_budget = [float(case["mean_budget_pct"]) for case in cases]
        worst_mean = max(cases, key=lambda case: float(case["mean_budget_pct"]))
        worst_tail = max(float(case["p999_budget_pct"]) for case in cases)
        misses = sum(int(case["deadline_misses"]) for case in cases)
        quantum_ms = float(cases[0]["budget_us"]) / 1000.0
        if misses == 0 and worst_tail <= args.tail_limit:
            viable[tier].append(frames)
        print(
            f"{tier:<9} {frames:>6} {quantum_ms:>7.3f}ms {len(cases):>6} "
            f"{statistics.median(mean_budget):>8.2f}%/{max(mean_budget):>6.2f}% "
            f"{worst_tail:>11.2f}% {misses:>7} {short_model(worst_mean['model'])}"
        )

    print()
    print(f"Viability rule: zero measured misses and worst model p99.9 <= {args.tail_limit:.1f}% of quantum.")
    for tier in sorted({tier for tier, _ in grouped}):
        sizes = viable.get(tier, [])
        if sizes:
            smallest = min(sizes)
            quantum_ms = smallest / 48.0
            print(f"  {tier}: smallest passing compute quantum is {smallest} frames ({quantum_ms:.3f} ms).")
        else:
            print(f"  {tier}: no tested quantum passes the rule.")

    print()
    print("Ten hottest cases by p99.9 budget")
    hottest = sorted(rows, key=lambda row: float(row["p999_budget_pct"]), reverse=True)[:10]
    for row in hottest:
        print(
            f"  {row['tier']:<5} {int(row['block_frames']):>3}f "
            f"mean={float(row['mean_budget_pct']):>6.2f}% "
            f"p99.9={float(row['p999_budget_pct']):>6.2f}% "
            f"max={float(row['max_us']):>8.2f}us  {short_model(row['model'])}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
