# POG3 frame interpretation CPU experiment — 2026-10-08

**Decision: retain compact frame birth slots.** Two ordinary matched Pi ABBA
rounds show **4.933%/4.527% combined mean CPU reductions at 64/128 frames**.
Pooled 64-frame over-period callbacks fall **361 → 21 / 12000 (94.183%)**;
128-frame counts fall **17 → 0 / 6000**. Exact frame/audio histories and all
10 default / 11 opt-in DSP checks pass. Prepared C++ storage grows by **3216
bytes**, from 1622696 to 1625912, excluding FFTW's own allocations.

The starting commit is `c5f4b1b1a81ff28d8a219398bd924e745c6b3e7f`; its production
DSP equals the retained `4de167c` baseline. Compact Attack birth slots, the
refined long-render schedule and four-source rendering remain. The preceding
Attack gain refactor remains rejected. Freeze/gliss remains opt-in, default OFF.
No selectable effect or live application deployment is added.

## Attribution and actual work

A standalone-only diagnostic splits interpretation into magnitudes, peak
frequency, candidate sorting, track aging/prediction, association and final
region/history work. Fixed event/record storage is touched before recording.
There are no per-bin timers. Ordinary builds leave all profiling options OFF.
Shadow flags count where an already-required peak phase could be reused from
exactly the previous frame; they do not cache or change DSP values.

The measured four-second fixture yields **5247 frames** and **74566 events**
per callback partition. All frame/event metadata, work counts, input/control
stamps, render due ages and nested parent/child accounting match at 64 and 128.
The immutable per-frame counts also match across baseline/candidate.

| Diagnostic stage, long frame | Original left/right µs | Candidate left/right µs |
| --- | ---: | ---: |
| Magnitudes | 9.326 / 9.000 | 9.436 / 9.215 |
| Peak frequency | 61.149 / 60.223 | 60.228 / 59.853 |
| Candidate sorting | 49.060 / 46.579 | 48.979 / 46.782 |
| Predictions | 19.359 / 18.442 | 18.670 / 18.949 |
| Association / births | 156.198 / 156.078 | 34.844 / 35.211 |
| Finalization | 2.518 / 2.314 | 2.288 / 2.235 |

These timings include instrumentation. In particular, the original birth loops
increment counters on every scanned slot; the candidate counts snapshot reads
once per build. The large diagnostic association reduction is **not** an
ordinary-build CPU reduction. Only the two uninstrumented rounds below support
the retention decision.

| Resolution | Frames | Original birth scan iterations | Candidate snapshots | Snapshot track reads |
| --- | ---: | ---: | ---: | ---: |
| Short, N=1024 | 2999 | 2206069 | 2996 | 1533952 |
| Long, N=2048 | 1499 | 59373528 | 1499 | 767488 |
| Low, N=4096, 400 Hz ceiling | 749 | 1821 | 211 | 108032 |

Snapshot reads count both fixed 256-track passes. Consumption additionally
checks used flags and up to five tier cursors; each stored index is advanced at
most once per frame. No original birth-scan loop remains. Low frames do more
snapshot work than their tiny original scans; the overall measured improvement
is driven by dense long frames. Short association is modestly cheaper, and low
association slightly more expensive in the diagnostic. No isolated low-stage
speedup is claimed.

The long fixture has 420211 pre-cap peaks, 419451 phase estimations, 92 logarithmic
fallbacks, 264803 reusable previous phases (63.131%), 382193 predictions,
266059 matches and 116355 births. Short phase reuse is 62.969%; low is 75.462%.
These figures support considering a sparse phase cache later, after peak
selection work. Phase math and Cartesian history are unchanged here.

## Ordering review

`FrameBirthSlots<256>` owns 256 uint16 indices and five uint16 begin/end cursors:
**532 bytes**. Six aligned `PitchFrame` objects increase prepared processor
storage by **3216 bytes**. It performs no allocation or sorting in the callback.
The first birth lazily builds one snapshot after original once-per-frame aging.
Frames without births do not touch its workspace.

The original selector first scans ascending indices for an unused **empty OR
expired** track, as one combined tier. It then scans for the greatest **positive**
missed age, breaking ties by the lowest original index. The snapshot uses this
exact ordering: combined empty/expired, then ages 4, 3, 2, 1. Generated age zero
is excluded. This differs from Attack's separate empty-first rule, so the Attack
selector is not reused.

Two bounded passes count tiers and scatter original indices in ascending order.
A selection skips indices already used by earlier births or predictions. Only
selected tracks change keys during the association loop, and their used flags
never return to false; unused keys retain their snapshot priority. Predictions
are still collected and sorted from the original pre-loop keys, and future
predicted matches are not reserved from earlier births. Lazy preparation after
some matches is equivalent because those already-selected tracks remain used.

Every successful slot still receives the original birth reset, generation,
velocity, frequency, missed age and used mutation in the original order.
Candidate membership/order, distance boundaries, float ties, capacity events,
region publication/partition, Cartesian history and timestamps remain intact.
A local readiness flag is recreated for each update; reset, reprepare and rejected
frames cannot reuse stale cursors. This is a structural equivalence argument
supported by finite exact traces, not a universal numerical proof.

## Ordinary Pi results

Both rounds use the same unchanged baseline/candidate ELFs, static NEON float
FFTW 3.3.10, Buildroot GCC 13.4, Release `-O3/-DNDEBUG`, freeze/profiling OFF,
48 kHz, CPU 2, SCHED_OTHER and the existing performance governor at 1.5 GHz.
The runner stops the existing service for standalone probes and restores it on
exit; independent PID readbacks follow every run. No buffer, governor, NAM
quality, firmware or installed application changes are made.

Every probe imports the same **57-plan FFTW wisdom**. Each workload has four
seconds warmup, reset, then the same four-second measured audio/control timeline.
Order is baseline-forward, candidate-forward, candidate-reverse, baseline-reverse.
The fixture exercises all stereo voices, processed dry, Focus, Attack, Warp,
filter, doubling and Spread with Off expression. Uninvolved granular and spectral
controls are retained alongside the core. All 48 ordinary rows have zero C++
callback allocations and unchanged core FFT maxima, 9/16 at 64/128 frames.

| Mean reduction | 64 frames | 128 frames |
| --- | ---: | ---: |
| First round | 4.788% | 4.239% |
| Confirmation | 5.077% | 4.813% |
| Both rounds | **4.933%** | **4.527%** |

| Four runs per build | Frames | Mean period demand | Highest p99 µs | Worst µs | Over-period counts per run |
| --- | ---: | ---: | ---: | ---: | ---: |
| Original frame scans | 64 | 84.285–85.791% | 1379.092 | 1561.130 | 73–112 / 3000 |
| Compact frame slots | 64 | 80.327–81.568% | 1293.370 | 1381.278 | 4–9 / 3000 |
| Original frame scans | 128 | 84.324–85.315% | 2613.204 | 3005.130 | 3–6 / 1500 |
| Compact frame slots | 128 | 80.505–81.210% | 2471.388 | 2609.278 | 0 / 1500 |

Combined means are 1134.449 → 1078.481 µs at 64, and 2259.607 → 2157.311 µs
at 128. These are relative reductions in ordinary callback work, not percentage
points of the callback period. Combined control reductions are granular
−0.091%/+0.096%, spectral +0.405%/+1.119% at 64/128. Full distributions and
control outliers are retained. No corrected CPU result or formal significance
claim is made.

64-frame callbacks still exceed their 1333.333 µs period. The 128-frame candidate
stays below its period in these finite offline runs, which does not establish
live admission. Offline over-period durations are not ALSA xruns. Paced FIFO,
intended-chain CPU/memory margin, thermal endurance and listening remain open.

## Validation

- `pog3_frame_birth_slots.cpp`: **758880 selections** equal independent original
  scans, including exhaustive four-slot arrangements of empty/generated ages
  0–5 and every used mask, then 2048 full-capacity scenarios. Covers combined
  empty/expired priority, age/index ties, generated age zero, intervening uses,
  exhaustion and fresh-frame cursor renewal. Host, Pi and ASan/UBSan pass.
- Direct frame oracle: baseline and candidate are separately linked against
  immutable original headers/library and current headers/library. Explicit
  field serialization avoids padding. **1088 frames / 79066 published regions**
  produce identical 1924784-byte traces separately on host and Pi. Covers sizes
  64, 1024, 2048, 4096 and 32768, full/400 Hz ceilings, dense/sparse/silent/equal
  peaks, migration, threshold/DC/Nyquist/subnormal cases, nonfinite or wrong-size
  histories, reset/reprepare and repeated capacity saturation.
- Audio: **1966080 automation floats + 384000 full-core floats** match exactly
  separately on host and Pi. Host opt-in automation matches all 1966080 floats.
  Includes mixed/wet/dry/control/freeze diagnostics, all stereo voices, reset,
  startup/drain, Focus/Warp, Attack and control/frame boundaries. Baseline probes
  use original headers because the new workspace shifts inline bank offsets.
- **10 default and 11 opt-in DSP checks pass**, including the existing Attack
  oracle, full freeze/gliss and required-preload C callback allocation/free test.
  Ordinary targets are returned to freeze OFF; affected host/ARM strict warning
  builds, automation/frame/selector ASan/UBSan and leak checks pass. Pi pitch,
  Focus, Warp and overload checks run before CPU timing.
- Active instrumented processor output matches the ordinary trace on host for
  baseline and on Pi for baseline/candidate. All nested event/count/accounting
  checks pass. All eight ordinary and both diagnostic wisdom exports retain the
  header and 57-record multiset. Diagnostic and ordinary memory measurements
  exclude diagnostic BSS and FFTW internal allocation.

## Reviewed Luna next steps

1. **Retain this exact baseline.** Keep frame and Attack slot snapshots, refined
   render ages, four-source rendering and default-disabled freeze. Save immutable
   source, headers, ELFs and seed wisdom before the next candidate. Continue DSP
   CPU work; public effect integration remains blocked on admission.
2. **Try bounded top-peak selection first.** Long candidate sorting still costs
   about 47–49 instrumented µs per frame. The original path sorts every peak by
   descending magnitude / ascending bin, keeps 256, then sorts those by bin.
   Test `std::nth_element(begin, begin + 256, end, originalMagnitudeComparator)`
   only when pre-cap count exceeds 256, then retain the original 256/bin sort.
   Each candidate has a unique bin and finite magnitude, so the comparator gives
   an unambiguous top set including magnitude ties. Keep phase/frequency
   estimation for every peak before selection and increment the same capacity
   event once. Do not change peak thresholds, edge-bin ties or track association.
   Do not equate average-complexity savings with a measured speedup; cap work
   remains bounded by prepared bins, and tail timings determine retention.
3. **Validate actual published histories.** Reuse the independent frame traces
   and full automation; explicitly cover 255/256/257+ peaks, equal-magnitude
   cutoff ties, both ceilings, large supported N, dense repeated overload,
   migration and reset. Preserve region membership, ascending publication,
   generations, capacities and Cartesian histories. Add a narrowly focused
   cutoff fixture if current traces do not cover exact boundary counts.
4. **If selection does not help, measure a sparse previous-phase cache.** About
   63% of long/short phase-estimation bins had an already-computed phase in the
   preceding frame. Cache only those exact float `std::arg` results, with validity
   for the immediately previous frame, and retain Cartesian fallback. Never
   compute all-bin phase. Preserve original float phase subtraction before double
   step/remainder arithmetic and every zero/log/startup/reset/nonfinite path.
   Account for new storage/alignment and clearing costs; compare actual output,
   not a tolerance-based substitution. Do not add approximate atan or sine.
5. **Measure one change at a time.** Run required default/opt-in, allocation,
   affected sanitizer and strict-warning checks; then compare ordinary profile-OFF
   ELFs with the same wisdom/fixtures/control rows in ABBA plus confirmation.
   Archive/reject mixed or noise-sized results. Retain raw distributions, FFT
   bounds, memory growth, control variation and service readbacks. Current
   64-frame tails and intended-chain admission remain the deciding constraints.

Artifacts: [complete experiment](../benchmark-results/pog3-pi4-interpretation-2026-10-08/README.md).
