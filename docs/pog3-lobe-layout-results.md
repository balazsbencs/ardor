# POG3 Hann-lobe layout experiment — 2026-10-07

This experiment changes only the prepared Hann-lobe coefficient layout in the
production pitch plan, against the real FFT/packed-synthesis baseline
`977a00698f58e9e48206d3c9a14cef2b7dccc970`.
The 25% figure remains a chain-budget planning target. Standalone mean demand,
callback tails and eventual intended-chain endurance determine feasibility.

## Hypothesis and arithmetic contract

Low-band reconstruction and held oscillators evaluate the Hann lobe at
consecutive integer destination bins. The old distance-major table spaces
coefficients at successive integer distances by 512 floats. The candidate
stores the same 8,193 floats by fractional phase: each of 512 rows contains
16 successive integer distances. Adjacent interpolation coefficients therefore
usually come from two short rows. Logical coefficient 8192, the distance=16
endpoint, occupies the final slot after those rows.

Logical indices 0–8191 map to `(i % 512) * 16 + i / 512`; index 8192 stays 8192.
Coefficient preparation retains its original double-precision expression and
iteration order. Runtime retains the original absolute distance, support limit,
linear interpolation expression and handling of out-of-support distances.
The immutable table size and callback allocations are unchanged. FFT plans,
windows, support, phase evolution, controls, staging and latency are untouched.
Closer coefficient placement is a hypothesis about locality; this experiment
does not measure cache misses. Additional address arithmetic can offset it.

## DSP validation

An independent historical distance-major table and lookup compare the actual
`PitchPlan::lobe()` result bit for bit, without using the new storage mapping.
The regression evaluates every knot and its immediate neighboring float values,
interpolation interiors, positive/negative distances, support endpoints,
silence/subnormal distances and random distances at N=1024/2048/4096.
All **393,258 lookups** pass on the host, under ASan/UBSan with leak checking,
and in the Pi's complete pitch suite. This is exact arithmetic compatibility,
not a relaxed audio threshold.

All **nine Release DSP CTests pass** (76.79 s): controls, FFT/stream foundations,
pitch/low-chord quality, independent Attack, voice stages, expression,
freeze/gliss and required-preload callback allocation checks. Changed source/test
units pass `-Wall -Wextra -Wpedantic -Werror`. The Pi's foundation and full pitch
suite finish before CPU timing; their complete logs are retained.

## Target comparison

The immutable baseline ELF matches the preceding checkpoint's final candidate
SHA256 exactly. Both builds use Buildroot GCC 13.4.0, ordinary -O3/-DNDEBUG,
the same static single-precision NEON FFTW 3.3.10 archive, unchanged benchmark
fixtures and profiling OFF. The sequential comparison runs baseline/candidate/
candidate/baseline on CPU 2, SCHED_OTHER, at 48 kHz with 64/128-frame fixtures.
Each fixture has warmup/reset and a four-second measured audio timeline.
No other heavy target probe runs during timing.

The runner stops/restores the previously running application. These unpaced
standalone measurements measure DSP demand and callback tails; they do not
measure ALSA xruns, paced FIFO behavior or full-chain endurance.

| Full workload | Frames | Baseline mean period demand | Candidate mean period demand | Two-run mean reduction | Highest candidate p99 (µs) | Candidate worst (µs) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Static filter/space/pan/Attack | 64 | 93.22–94.32% | 92.17–92.44% | 1.56% | 1878.389 | 2056.333 |
| Expression/filter/space/pan/Attack | 64 | 96.91–97.35% | 95.80–96.18% | 1.18% | 2051.907 | 2282.907 |
| Freeze/gliss/capture/assignment | 64 | 106.21–107.11% | 103.26–103.87% | 2.90% | 2125.926 | 2526.445 |
| Static filter/space/pan/Attack | 128 | 93.95–94.00% | 92.23–92.64% | 1.64% | 2794.740 | 2931.000 |
| Expression/filter/space/pan/Attack | 128 | 97.58–97.85% | 95.60–95.87% | 2.03% | 3180.759 | 3416.963 |
| Freeze/gliss/capture/assignment | 128 | 106.04–106.19% | 103.81–103.90% | 2.13% | 3163.704 | 3514.000 |

The observed full-workload mean reduction is **1.18–2.90%**. Freeze/gliss remains
**103.26–103.90%** of a period on average. All full-path p99s still exceed their
1333.333/2666.667 µs periods; some individual static/expression tails worsen.
The layout is retained, but these results do not admit live use or M8 integration.

Granular control mean changes are -0.176% and -0.005% reduction. The uninvolved
spectral identity control also changes: 2.147% and 0.909% mean reduction, with
noticeable 128-frame variation between candidate runs. Consequently, the full
processor changes cannot all be attributed uniquely to table locality. FFTW
measurement-based planning and process/run variation remain possible contributors;
no cache-counter evidence is collected. These are observed build comparisons,
not a statistically isolated estimate of the layout's causal saving.

All four timing invocations return zero, with all **56 aggregate rows** retained
and zero C++ callback allocations. Each preparation-byte count equals its matched
baseline. The numerical probe also returns zero. Runner exit is zero; restored
service PID **11441** matches the independent readback. Boundary temperature
samples range **54.530–58.913°C**; all clock/governor receipts remain
1.5 GHz/performance. Complete target memory and C/FFTW allocations remain unmeasured.

[Raw comparison evidence](../benchmark-results/pog3-pi4-lobe-layout-2026-10-07)
retains the original source patch, ELF/artifact hashes, complete tails, numerical
logs, host checks, service/telemetry receipts and reproducible summary arithmetic.
The compressed patch preserves its original bytes; source.txt hashes those
uncompressed bytes. Two baseline runs and two candidate runs are averaged per
case; no earlier checkpoint measurements are pooled into the reported reduction.

## Next CPU work

Held rendering advances phase before applying its complementary low/high crossover.
It still calculates carrier sin/cos and scatters a full lobe when the resulting
amplitude is exactly zero. Test skipping only that inaudible reconstruction after
phase updates, preserving silent oscillator histories as partials glide back into
an audible band. Compare to this layout checkpoint, retain exact-output evidence
and complete DSP gates, then repeat target timing. Lower average demand alone
will not resolve the remaining callback-tail and intended-chain admission work.
