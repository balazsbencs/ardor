# POG3 sparse previous-phase cache — 2026-10-08

## Decision and scope

Two exact cache representations were compared separately against retained HEAD
`35b64bca2aca6fe6ec65bd1fca8c3eac3736fa6e`. The per-bin validity layout is
rejected: its combined mean demand rises 0.540%/0.497% at 64/128 frames.
The bulk-cleared layout is also rejected: its combined mean demand rises
**0.559%/0.786%** at 64/128. Neither layout gives a useful ordinary benefit,
despite removing **31.566%** of actual phase-argument calls.

The retained DSP keeps exact top-256 partitioning, compact frame/Attack birth
slots, refined render due ages and four-source rendering. Freeze/gliss stays
opt-in, default OFF. Production previous-phase caching is absent; C++ preparation
remains **1625912 bytes**, excluding FFTW storage. No application/firmware,
public catalog, buffer, governor or NAM-quality changes are made.

## Exact phase reuse and validity review

Both prototypes cache the exact float `std::arg(spectrum[k])` only when the
existing phase-estimation branch already computes it, including peaks later
discarded by the 256 cap. Previous Cartesian spectrum and its original magnitude
gate remain. A miss computes the original previous-spectrum argument. Current and
previous float arguments are subtracted before the original double bin/step and
principal remainder; no approximate argument or all-bin phase calculation is used.

Validity represents the immediately preceding accepted finite frame. Wrong-size
updates preserve history. Nonfinite updates invalidate `previous_`; the next
finite frame uses the original startup/logarithmic path, preventing stale reuse.
Reset/reprepare invalidates entries. DC/Nyquist and logarithmic branches never
mark phase validity. Absent, nonpeak and below-threshold bins cannot reuse an
older phase when they return. Peak gates, ordering, track assignment, generations,
region boundaries, capacity events and frame/control timestamps remain unchanged.

- **Per-bin layout:** one float array and one byte-validity array, sized
  `peakLimit + 1`. Each scanned bin reads prior validity and clears it before any
  peak early-continue. Only the existing phase branch writes its current phase
  and sets validity. Additional requested storage is **16028 bytes**: 3148
  entries across six frame objects × 5 bytes, plus twelve 24-byte vector objects.
  Measured C++ preparation is **1641940 bytes**.
- **Bulk layout:** one float array and two byte-validity arrays. Clear the current
  validity map once per accepted finite frame; read the previous map only for
  phase peaks, write current validity there, then swap maps after the original
  Cartesian history copy. Each bin is visited once and reads its prior phase
  before overwriting the same float slot. Additional storage is **19320 bytes**:
  3148 × 6 bytes plus eighteen 24-byte vector objects. Measured preparation is
  **1645232 bytes**. This removes the validity read/clear from nonpeak scan bins.

These storage calculations match target preparation receipts. Fewer argument
calls do not guarantee lower total CPU: cache traffic and validity bookkeeping
also have a cost. The experiment does not separately attribute the reason for
the ordinary timing result.

## Actual phase-call diagnostics

Standalone generated copies count actual argument entries and independently
shadow previous-frame validity. Cache hit flags must equal the shadow flags.
Both partitions publish the same **5247 frame records / 74566 stage events**;
all nested accounting, renderer deadlines, input/control stamps and nonphase
work counts pass. Both layouts independently reproduce these actual counts
and all baseline nonphase work identities.

| Frame size | Phase peaks | Reusable previous phase | Baseline previous args | Candidate previous args |
| ---: | ---: | ---: | ---: | ---: |
| 1024 | 419574 | 264202 | 419574 | 155372 |
| 2048 | 419451 | 264803 | 419451 | 154648 |
| 4096 | 5518 | 4164 | 5518 | 1354 |
| Total | 844543 | 533169 | 844543 | 311374 |

Current argument calls remain 844543. Total actual phase arguments fall
**1689086 → 1155917 (31.566%)**. Long/short previous-phase reuse is about 63%.
Peak, prediction, association, birth, snapshot and region counts match the
baseline exactly. Active-profiler full-core output matches the ordinary original
Pi output byte for byte. Diagnostic timings are excluded from the ordinary CPU
comparison; these counts establish work avoided, not a retained CPU benefit.

## Ordinary hardware comparison

Each candidate has its own first and confirmation ABBA rounds and is not pooled
with the other candidate. Both use immutable matched-header baseline probes,
unchanged candidate ELFs and identical imported 57-plan FFTW wisdom. Order is
baseline forward / candidate forward / candidate reverse / baseline reverse.
Each row measures four seconds after a four-second warmup/reset, with 3000/1500
callbacks at 64/128 frames. Existing matched fixtures exercise all voices,
processed dry, Focus, Attack, Warp, filters, doubling, Spread and Off expression;
uninvolved granular/spectral controls are retained.

Pi 4 Model B 1.5, 48 kHz, CPU 2, SCHED_OTHER unpaced offline probes, existing
performance governor at 1.5 GHz; Buildroot GCC 13.4, Release -O3/-DNDEBUG, static
NEON float FFTW 3.3.10. Freeze and all diagnostic profiles are OFF for timing.
Each candidate has 48 ordinary rows across both rounds, zero measured C++ callback
allocations, and unchanged 9/16 maximum core transforms per 64/128 callback.

| Layout | Round | Mean change at 64 | Mean change at 128 |
| --- | --- | ---: | ---: |
| Per-bin | First | 0.010% faster | 0.109% slower |
| Per-bin | Confirmation | 1.088% slower | 0.886% slower |
| Per-bin | Combined | 0.540% slower | 0.497% slower |
| Bulk | First | 0.456% slower | 0.344% slower |
| Bulk | Confirmation | 0.662% slower | 1.229% slower |
| Bulk | Combined | 0.559% slower | 0.786% slower |

| Layout/build | Frames | Four-run mean µs | Highest p99 µs | Worst µs | Pooled over-period callbacks |
| --- | ---: | ---: | ---: | ---: | ---: |
| Per-bin/baseline | 128 | 2121.526 | 2429.814 | 2556.463 | 0/6000 |
| Per-bin/baseline | 64 | 1061.319 | 1289.555 | 1382.130 | 24/12000 |
| Per-bin/candidate | 128 | 2132.080 | 2469.389 | 2836.815 | 2/6000 |
| Per-bin/candidate | 64 | 1067.054 | 1294.629 | 1372.574 | 16/12000 |
| Bulk/baseline | 128 | 2119.162 | 2434.315 | 2569.908 | 0/6000 |
| Bulk/baseline | 64 | 1064.484 | 1293.871 | 1376.315 | 24/12000 |
| Bulk/candidate | 128 | 2135.813 | 2438.852 | 2566.315 | 0/6000 |
| Bulk/candidate | 64 | 1070.438 | 1296.870 | 1555.834 | 36/12000 |

All raw means, p99/maxima, over-period counts and control distributions are saved.
No outlier is deleted or subtracted using control timing. Small changes within
control variation cannot establish a useful cache advantage. These finite offline
runs do not establish paced ALSA/FIFO, thermal/xrun or intended-chain admission.

## DSP validation and retained tests

Both layouts pass **10 default / 11 opt-in DSP suites**, including allocation,
Attack, expression and freeze/gliss checks. Host default and opt-in bank/processor
automation each match all **1966080 floats**; default host/Pi full-core output
matches **384000 floats**. Expanded exact host/Pi frame traces match **2498
frames / 173482 regions / 4226018 bytes** per trace, with public fields serialized
without padding. They add 960 explicit phase-history calls to the preceding
cutoff, dense/sparse/migrating, reset, saturation and capacity fixtures.

The retained `phaseHistory()` test uses an independent original Cartesian
phase/logarithmic-frequency oracle, never cached phase/validity. It checks **464
published frequencies**, bit for bit, at N=64/1024/4096 and 400 Hz/full ceilings:
hits, migration, absent/return, nonpeak shadows, magnitude-threshold crossings,
signed-zero phase seams, wrong-size interruption, nonfinite reset and
reset/reprepare. The test also passes independently linked against immutable
original headers/library. The normal pitch suite runs it, or invoke
`pedal-pog3-pitch-quality --phase-history`. Existing 810-frame peak-selection
and birth-selector oracles remain.

Affected automation, expanded frame and phase-history ASan/UBSan/leak checks and
strict host/AArch64 warning builds pass for both prototypes. Target full pitch,
Focus, Warp, overload and both new oracles precede timing.

After removing both production caches, the final ordinary host build is freeze
OFF and all **10 suites pass** again. The rebuilt ordinary ARM full-core ELF
is byte-identical to immutable HEAD35b baseline (SHA-256
`d6352129bcd9b7742db904d996f387a1f5bbaa0efa6deac31314fb817d9d5299`).
Production header/source also match HEAD35b exactly. Actual phase counters remain
confined to standalone diagnostic generated copies; no DSP profiling is added.

## Reviewed Luna next step

1. Keep the retained DSP baseline and both rejected patches as separate evidence.
   Do not add a production cache switch or approximate phase math on the strength
   of a call-count reduction. Do not add these percentages to earlier experiments.
2. Try **skipping the final bin sort only for nonoverflow frames**. The peak scan
   appends each accepted bin in ascending order. When candidate count is at most
   256, no partitioning has changed that order; the final bin sort is redundant.
   Put the original final bin sort inside the overflowing branch, immediately
   after `nth_element` and truncation. Preserve all phase work before selection,
   original comparator, cutoff ties and capacity-event increments.
3. Review zero/one/255/256/257 candidates, ties and repeated overflow/nonoverflow
   transitions. Retain the independent original full-sort oracle and exact
   frame/track/generation/region/timestamp histories. Expect no prepared-memory
   increase or callback allocation. This is a separate small candidate, not an
   assumed sufficient headroom improvement.
4. Measure ordinary matched Pi ABBA and confirmation with fixed wisdom, hashed
   ELFs and both controls; report means alongside p99/maxima/period overruns.
   Retain only useful repeatable benefit. If negligible, inspect Attack support
   scoring with compact frequency/magnitude read keys as a separate experiment.
   Preserve original division, harmonic thresholds, candidate/observation order,
   accumulation order and family/score ties; avoid rounded reciprocal substitution.
5. CPU feasibility still precedes public integration. Once standalone headroom
   warrants it, perform paced intended-chain and thermal/xrun endurance and
   listening/calibration checks. Freeze/gliss remains deferred meanwhile.

[Complete experiment and reproduction](../benchmark-results/pog3-pi4-phase-cache-2026-10-08/README.md).
