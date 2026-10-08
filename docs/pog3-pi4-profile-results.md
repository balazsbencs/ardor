# POG3 Pi 4 profiling and magnitude optimization — 2026-10-07

This checkpoint profiles the production FFTW processor on the actual pedal and
tests an inline audio-range norm in place of per-bin complex norm library calls.
The first [hardware checkpoint](pog3-pi4-cpu-results.md) remains the unmodified
capacity baseline. Hardware demand and callback tails determine feasibility;
the original 25% figure remains a chain-budget planning target.

## Target attribution

The device has no `perf` executable. A separate optional diagnostic build adds
nested steady-clock scopes at frame-job entry/exit to **copies** of the sources.
Production sources and the normal engine/CI binaries contain no timers.
The unchanged full benchmark warms/resets before each measured four-second
fixture. Exclusive totals subtract nested scopes; renderer totals exclude FFT
and held-render children. This reports approximate work attribution, with
instrumentation overhead and unscoped processing left in the residual. These
instrumented timings are not performance-admission or optimization-gain evidence.

One fourteen-row profile run on CPU 2 completed successfully and restored the
live service (PID receipt 3630). All rows record zero C++ callback allocations.
Full-feature cases perform 20231 transforms and 5247 frame interpretations per
measured fixture. Static rendering has no held jobs; expression has 4704 and
freeze/gliss has 16224. Profile evidence is retained in
[`benchmark-results/pog3-pi4-profile-2026-10-07`](../benchmark-results/pog3-pi4-profile-2026-10-07).

Percentages below are exclusive time divided by total measured callback time,
for 128-frame callbacks; the 64-frame run has the same broad cost ordering.

| Full workload | FFT | Live render excluding FFT/held | Frame interpretation | Attack | Freeze update | Held render | Residual |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Static filter/space/pan/Attack | 22.72% | 29.21% | 17.59% | 16.56% | 2.44% | 0% | 11.48% |
| Expression/filter/space/pan/Attack | 21.95% | 27.30% | 16.93% | 15.84% | 2.47% | 5.16% | 10.35% |
| Freeze/gliss/capture/assignment | 20.33% | 20.56% | 15.71% | 14.44% | 3.23% | 16.25% | 9.48% |

These target costs differ materially from the earlier host profile, especially
FFT cost. GCC 13.4's ARM assembly already vectorizes the interior 24-tap renderer
scatter loop; merely proposing its vectorization would duplicate existing work.
Frame interpretation instead makes a `cabsf` library call for every inspected
bin and for previous-spectrum peaks. This is the first optimization tested here.

## Magnitude arithmetic and audio contract

The first candidate promotes every float component to double before the norm.
It matches the sampled library outputs exactly, but the first target timing
passes show no useful saving. Merely removing the `cabsf` call does not establish
an optimization. The candidate tested next uses a float square root when the
squared norm is finite and normal; tiny/overflowing squared values use the
wide calculation. All finite binary32 component squares fit the binary64
exponent range, including subnormals. Final unrepresentable magnitude still
rejects the frame. No fast-math flags, FFT/window sizes, interpolation support,
track limits, Attack/freeze models, timestamps or callback scheduling change.

The actual `PitchFrame` regression compares against the independent library
complex norm over 65064 finite input pairs, including sparse peaks, tiny values,
1e20 components and float-maximum overflow. It checks one-float-ulp agreement,
silence, peak selection and the previous-frame phase path. It is included in the
normal pitch quality suite and available separately for the device. Existing
audio quality thresholds remain unchanged.

An exploratory eight-million random-bit-pair probe observes **7937952 finite
representable pairs** on each machine. The first double-only candidate has zero
norm differences. The float fast path differs for 70804 host and 60849 ARM pairs,
with maximum error **one float ULP** and unchanged finite/overflow classification.
This is sampled evidence, not an exhaustive bitwise or audio identity proof.
The probes and receipts are retained with the measurements.

The final float-path implementation passes all nine production Release CTests
(70.75 s), including the existing tuning, spur/alias, Attack, freeze/gliss,
latency/partition and required-preload allocation checks. The ARM FFTW
foundation/reference suite and frame-range regression pass. Changed
production/test units pass `-Wall -Wextra -Wpedantic -Werror`.

## Uninstrumented hardware comparison

The baseline is commit `42beeaf25e7635c5966434bc4153d6303c87019b`; immutable baseline
ELFs were copied before the source change. Each candidate differs in the norm
calculation only. Both builds use the same Buildroot GCC 13.4.0, ordinary `-O3`,
static single-precision NEON FFTW 3.3.10 archive and **profiling OFF**.
The final runner executes baseline/candidate/candidate/baseline sequentially, preserving
all fourteen workload rows per run and individual receipts. Numerical checks
precede timing. CPU 2, SCHED_OTHER, governor and buffer-size scope are the same
as the first target checkpoint. Offline timing does not measure ALSA xruns or
complete-chain endurance.

The earlier double-only trial is retained separately in
[`benchmark-results/pog3-pi4-magnitude-double-2026-10-07`](../benchmark-results/pog3-pi4-magnitude-double-2026-10-07).
Its final baseline pass accidentally overlaps the exploratory norm probe and
is excluded from optimization comparisons. The first baseline and both candidate
passes show no useful saving (128-frame full-path means: baseline
101.51/105.96/114.34%, candidates
101.86–102.26/106.11–106.25/114.76–115.12%). Final float-path timing uses a fresh sequential
four-probe run with numerical work completed before timing begins.

## Measured result and decision

The float fast path saves **1.04–1.47%** of full-workload mean time, averaging the
two runs per build for each workload/buffer pair. This is a small observed gain,
not a confidence interval or an exhaustive workload claim. The two uninvolved
control workloads are 0.03–0.41% slower; the changed full paths
all improve in the aggregated comparison. These controls give useful context
for the small effect size and do not establish a universal speedup.

| Full workload | Frames | Baseline mean period demand | Candidate mean period demand | Two-run mean saving | Candidate highest p99 µs | Candidate worst µs |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Static filter/space/pan/Attack | 64 | 101.84–102.20% | 100.57–100.67% | 1.37% | 1970.130 | 2105.500 |
| Expression/filter/space/pan/Attack | 64 | 106.16–106.27% | 104.47–104.83% | 1.47% | 2145.926 | 2352.852 |
| Freeze/gliss/capture/assignment | 64 | 114.25–115.10% | 113.44–113.53% | 1.04% | 2261.018 | 2646.371 |
| Static filter/space/pan/Attack | 128 | 102.26–102.30% | 100.68–100.94% | 1.44% | 3021.797 | 3146.592 |
| Expression/filter/space/pan/Attack | 128 | 105.58–106.13% | 104.20–104.60% | 1.38% | 3495.463 | 3759.537 |
| Freeze/gliss/capture/assignment | 128 | 114.32–114.80% | 112.63–113.24% | 1.42% | 3420.574 | 3771.241 |

The candidate still consumes **100.57–113.53%** of a period in the tested full
paths. Every full-path p99 remains above its period (1333.333/2666.667 µs).
Some tails improve, but the 128-frame freeze/gliss worst callback increases from
3759.241 to 3771.241 µs; all tails are retained. This is **not standalone admission**,
even with a relaxed 25% planning budget. No public M8 integration follows.

All **56 aggregate timing rows**, both source identities, ELF hashes, numerical
receipts, thermal/clock samples and service receipts are checked in under
[`benchmark-results/pog3-pi4-magnitude-fast-2026-10-07`](../benchmark-results/pog3-pi4-magnitude-fast-2026-10-07).
All timing invocations return zero and all C++ callback allocation counts are
zero. The runner returns zero; the service restarts with PID 5518, independently
confirmed afterward. Final-run metadata temperatures range 58.426–59.887°C; these are boundary
samples, not a continuous peak.
The sampled clock remains 1.5 GHz/performance. Firmware, presets and live buffers
remain unchanged. Retrieved temporary probes are removed after verification.
Host C allocation checks pass; target C/FFTW allocation and total storage are not
newly measured by this experiment.

## Next CPU experiment

Prioritize a real-input/Hermitian FFT path on the target: FFT accounts for about
20–23% of this instrumented full workload, materially more than on the host.
Use prepared single-threaded FFTW r2c/c2r plans and existing per-stream storage;
preserve the public generic complex-transform behavior and its fallback. Do not
silently apply a real inverse to arbitrary complex spectra. Account for packing,
conjugate reconstruction, alignment classes, one inverse normalization, extra
plan memory and all callback allocation calls. Test independent FFT accuracy,
identity/latency, shared-plan concurrency, extreme-value frame behavior, full
pitch/Attack/freeze quality and arbitrary callback partitions before target A/B.
The percentage attributed to FFT is an opportunity bound, not a predicted gain.

Keep the staged analysis/render deadlines and original buffer size. If this
creates standalone margin, proceed to paced FIFO/ALSA and intended-chain thermal
endurance. Rendering/held-carrier work and Attack remain further measured
optimization targets; this checkpoint does not resolve their combined cost.
