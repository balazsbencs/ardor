#!/usr/bin/env python3
"""Attribute hardware PC samples only inside measured DSP callback windows.

Usage: python3 analyze.py ARTIFACT_DIR EXECUTABLE_NM LIBM_NM
Symbol tables use `nm -S -n -C` (libm additionally uses -D).
"""
import bisect
import collections
import csv
import gzip
import pathlib
import sys


def open_text(path):
    path = pathlib.Path(path)
    if not path.exists():
        path = path.with_name(path.name + ".gz")
    return gzip.open(path, "rt") if path.suffix == ".gz" else path.open()


def symbols(path):
    result = []
    for line in open_text(path):
        fields = line.strip().split(maxsplit=3)
        if len(fields) == 4 and fields[2] in ("T", "t", "W", "w", "i"):
            result.append((int(fields[0], 16), int(fields[1], 16), fields[3]))
    return sorted(result)


def lookup(table, ip):
    index = bisect.bisect_right(table, (ip, float("inf"), "\uffff")) - 1
    if index >= 0:
        address, size, name = table[index]
        if ip < address + size:
            return name
    return f"unresolved@0x{ip:x}"


def main():
    root = pathlib.Path(sys.argv[1])
    exe = symbols(sys.argv[2])
    libm = symbols(sys.argv[3])
    rows = list(csv.DictReader(open_text(root / "core-sample.callbacks.csv")))
    starts = [int(row["start_ns"]) for row in rows]
    counts = collections.Counter()
    phases = collections.defaultdict(list)
    mappings = []
    for line in open_text(root / "core-sample.maps"):
        fields = line.split()
        if len(fields) >= 6 and "x" in fields[1]:
            lo, hi = (int(part, 16) for part in fields[0].split("-"))
            mappings.append((lo, hi, int(fields[2], 16), fields[-1]))
    outside = 0
    for row in csv.DictReader(open_text(root / "core-sample.samples.csv")):
        ip, time = int(row["ip"], 16), int(row["time_ns"])
        index = bisect.bisect_right(starts, time) - 1
        if index < 0 or time > starts[index] + float(rows[index]["wall_us"]) * 1000:
            outside += 1
            continue
        name = lookup(exe, ip)
        if name.startswith("unresolved"):
            for lo, hi, offset, path in mappings:
                if lo <= ip < hi:
                    relative = ip - lo + offset
                    name = ("libm:" + lookup(libm, relative) if "/libm.so" in path
                            else f"{path}@0x{relative:x}")
                    break
        counts[name] += 1
    total = sum(counts.values())
    with (root / "pc-attribution.csv").open("w") as out:
        writer = csv.writer(out, lineterminator="\n")
        writer.writerow(["samples", "percent_of_dsp_samples", "symbol"])
        for name, count in counts.most_common():
            writer.writerow([count, f"{100 * count / total:.4f}", name])
    print(f"DSP-window samples: {total}; outside: {outside}")
    for name, count in counts.most_common(25):
        print(f"{100 * count / total:6.2f}% {count:6d} {name}")
    paths = {str(path).removesuffix(".gz") for path in root.glob("*.callbacks.csv*")}
    for name in sorted(paths):
        for row in csv.DictReader(open_text(name)):
            phases[(pathlib.Path(name).stem, int(row["transforms"]))].append(float(row["wall_us"]))
    with (root / "transform-timing.csv").open("w") as out:
        writer = csv.writer(out, lineterminator="\n")
        writer.writerow(["run", "transforms", "callbacks", "mean_us", "p99_us", "max_us"])
        for (label, transforms), values in sorted(phases.items()):
            values.sort()
            writer.writerow([label, transforms, len(values), sum(values) / len(values),
                             values[int(.99 * (len(values) - 1))], values[-1]])


if __name__ == "__main__":
    main()
