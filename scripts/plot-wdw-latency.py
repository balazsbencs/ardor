#!/usr/bin/env python3
"""Plot measured WDW mixer transfer functions (requires matplotlib)."""
import argparse
import csv
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--results", type=Path, default=Path("benchmark-results/wdw-latency"))
args = parser.parse_args()
with (args.results / "spectrum.csv").open() as source:
    rows = list(csv.DictReader(source))
fig, axes = plt.subplots(2, 1, figsize=(10, 7), sharex=True, constrained_layout=True)
for ax, relative in zip(axes, [0, -10]):
    series = [row for row in rows if float(row["wet_relative_db"]) == relative]
    hz = [float(row["hz"]) / 1000 for row in series]
    ax.plot(hz, [float(row["unaligned_db"]) for row in series], color="#b34332",
            label="15-frame mismatch")
    ax.plot(hz, [float(row["aligned_db"]) for row in series], color="#247452",
            label="Declared latency alignment")
    ax.set(title=f"Wet amp copy at {relative} dB relative to dry", ylabel="Gain relative to dry alone (dB)")
    ax.set_ylim((-40, 8) if relative == 0 else (-4, 4))
    ax.grid(alpha=.2)
    ax.legend(loc="lower right")
axes[-1].set(xlabel="Frequency (kHz)", xlim=(0, 20))
fig.suptitle("Measured WDW mixer response at 48 kHz", fontsize=16)
fig.savefig(args.results / "comb-response.png", dpi=160)
