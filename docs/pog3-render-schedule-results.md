# POG3 long-render scheduling experiment — 2026-10-07

**Decision: retain the refined renderer schedule for the current 64-frame
configuration.** Two matched hardware rounds repeat substantially lower 64-frame
tails with essentially unchanged mean demand. This is a scheduling improvement,
not a throughput or live-admission claim. The 128-frame tails are mixed, including
a slightly higher candidate maximum. Both sizes still exceed their periods.
Four-source accumulation remains unchanged; freeze/gliss stays opt-in.

## Change and reviewed ownership

Baseline is `3a4cb365` (production DSP identical to `2e7efdb3`). Only the twelve
long-render due ages change in `PolyphonicPitchBank::process`:

| Layout | Due ages relative to primary frame | Long jobs per 64-sample interval |
| --- | --- | --- |
| Original | 17,38,81,102,129,145,161,177,193,209,225,241 | 2,2,4,4 |
| First, rejected | 65,80,96,111,127,129,141,153,166,178,191,193 | 0,5,6,1 |
| Refined, retained | 65,85,106,127,129,141,153,166,178,191,193,241 | 0,4,6,2 |

These intervals are relative frame ages, not the callback-end modulo labels in
[the preceding measured stage profile](pog3-callback-phase-results.md). The
refinement retains four long jobs in the first 128 samples and eight in the
second. Every recorded long job belongs to its original 128-frame callback in
the matched fixture; arbitrary callback alignment is not covered by that claim.

Analysis and interpretation times, low-before-primary Attack ordering, actual
Attack control evaluation, short Attack at age 8, freeze update at age 9 and
short-render ages 65/81/97/113 are unchanged. Primary and low frames/gains remain
immutable until the jobs finish. `frameWarp_` is captured at the original primary
boundary. Focus still mixes output at pop time. Freeze setters change requests
and state but not the held band, mix/dryMix or gain consumed by these renders;
those values change on the unchanged frame update. Each voice still renders
left before right, including the optional right-channel reference to left-held
phase data. Per-renderer history order and arithmetic stay unchanged.

`256 - longAge_` still stages every primary window to the original t+1+H onset;
short staging stays `128 - shortAge_`. Long jobs finish at age 241, preserving the
original 15-sample margin before the next frame, and short jobs finish at 113.
No heap, renderer stack, FFT/backend, prepared buffer, quality or control-cadence
change is intentionally introduced. These structural claims are supported by
the one-array source patch and numerical evidence below, not proof for every
compiler/input.

## Why the first layout was rejected

The first cost replay ranked only 64-frame callbacks. It predicted fewer peaks
but missed the 128-frame imbalance from moving the fifth long job into the first
half-hop. A complete first hardware ABBA passes exact output and both DSP suites,
then observes 64-frame overruns fall from 1130–1144 to 152–180/3000, while 128-frame
overruns rise from 5 to 23–24/1500 and maximum rises from 2776.315 to 2863.500 µs.
That layout is rejected, without pooling its results into the retained layout.
Its patch, hashes, tests, timing and restoration receipts remain in `trial-1/`.

The revised replay explicitly checks both callback sizes. It assigns each
recorded inclusive long-job cost to its proposed callback, keeping residual
analysis/Attack/short-job cost fixed. Refined modeled 64-frame overruns are
250/3000 versus 1184 originally; the 128-frame callback work vector is unchanged
within floating accounting tolerance, retaining 6/1500 modeled overruns. The
model verifies ordered jobs, bounded ages and identical total work. It cannot
predict cache, code/process placement, changed per-job costs or timer/page effects.
Only the uninstrumented results below select production behavior.

## Exact output and DSP gates

The new standalone automation oracle processes the actual bank and complete
processor for two reset/startup/drain passes (81,920 input samples, 1,966,080 float
outputs/diagnostics). It issues 14,160 control events around 48/128/256/512
boundaries and selected old/new due ages, alternates Attack zero/positive, Focus, Warp and
all available expression modes, and drains 4096 silent samples per pass. Bank
output includes every stereo voice; processor output includes mixed/wet/dry,
gain/Warp/filter diagnostics and freeze capture/target counts. Health/deadline
checks run throughout. It uses production FFT sizes and shared wisdom.

Baseline/refined output matches byte for byte across **1,966,080 automation floats
plus 384,000 moving-core full-processor floats on host and Pi**, separately per
architecture. The host additionally matches all automation floats in the opt-in
freeze build. Its default and opt-in hashes differ as expected and are not
cross-compared. Finite traces cannot establish universal equivalence. The Pi
pitch/Focus/Warp/overload suite passes before each layout's timing. Renderer
arithmetic was not changed; the preceding broad actual-renderer oracle remains
applicable to that unchanged path.

The refined source passes **eight default DSP suites** (45.08 s) and **nine
opt-in suites** (72.68 s), including full freeze/gliss and required-preload callback
C allocation/free checks. Its automated trace passes ASan/UBSan with leak
checking. Changed bank units pass strict host and AArch64 warnings with both
freeze option values; the trace passes strict host warnings both modes and ARM
warnings in the measured default mode. The final ordinary host build is restored
to freeze OFF and its registered test list has eight suites.

## Two matched hardware rounds

Both rounds use the exact same staged baseline/candidate ELFs and shared wisdom.
Baseline ELF `34b2090e...` matches the earlier retained four-source benchmark.
Both are built with Buildroot GCC 13.4.0, ordinary -O3/-DNDEBUG, static single-
precision NEON FFTW 3.3.10, freeze OFF and both profile options OFF. The callback
profile's instrumented timings are never included here.

Each round runs baseline-forward / candidate-forward / candidate-reverse /
baseline-reverse; reverse swaps callback-size order. Fixtures, input/settings and
four-second warmup/reset/four-second measured timeline remain identical. The
core uses changing stereo chords/noise, all six voices, processed dry, Focus,
Attack=.11, filter/detune/Spread and Off expression. Granular and spectral
identity controls remain included. CPU 2 uses SCHED_OTHER, with the existing
performance governor at 1.5 GHz. Service stop/restore surrounds each standalone
runner. Confirmation repeats timing only, after independent first-round numerical,
hash, wisdom and service checks.

| Mean reduction versus original schedule | 64 frames | 128 frames |
| --- | ---: | ---: |
| Refined first ABBA | −0.794% | +0.147% |
| Refined confirmation ABBA | +0.448% | +0.310% |
| Both rounds, four runs per build | −0.171% | +0.229% |

The mean is essentially unchanged; these small differences do not demonstrate a
throughput gain. Combined granular mean reductions are +0.040%/−0.039%, and
spectral controls are −0.902%/−0.095% at 64/128. Their per-round changes and all
outliers remain saved; no attempt is made to subtract controls into a corrected
CPU percentage or establish statistical significance from four repeats.

| Build, four runs each | Frames | Mean period demand range | Highest p99 µs | Worst µs | Over-period callbacks per run |
| --- | ---: | ---: | ---: | ---: | ---: |
| Original schedule | 64 | 86.37–87.08% | 1696.278 | 1884.055 | 1127–1169 / 3000 |
| Retained refinement | 64 | 86.36–87.65% | 1487.241 | 1674.852 | 221–275 / 3000 |
| Original schedule | 128 | 86.84–87.31% | 2640.648 | 2760.944 | 2–7 / 1500 |
| Retained refinement | 128 | 86.71–86.92% | 2639.204 | 2818.575 | 3–10 / 1500 |

Pooled 64-frame over-period counts fall from **4595 to 951 per 12,000 callbacks**,
a **79.304% reduction**; rates are 38.292% and 7.925%. Both individual rounds
repeat the improvement. Highest p99 and worst observed time fall about 12.3% and
11.1%, respectively. The current live configuration is 64 frames, so this large,
repeatable tail benefit justifies retaining the bounded change despite no mean
speedup. The 128-frame worst is about 2.1% higher and pooled over counts are 18
versus 24 per 6000; confirmation does not repeat a consistent 128-frame tail
regression or benefit. This mixed outcome is an explicit limitation, not an
admission result at that size.

All **48 refined uninstrumented rows** report zero C++ callback allocations.
Core preparation remains **1,618,560 bytes**, excluding FFTW/immutable diagnostic
storage. Maximum transforms per 64-frame core callback changes from 11 to 9;
it remains 16 at 128. Parsers verify stable bounds within each build rather than
incorrectly requiring scheduling to preserve the maximum in every callback.
All eight timing wisdom exports retain the shared header and 57 records without
additions/removals. That constrains planning records, not instruction-level plan
identity. The peak/mean distinction remains essential: no work reduction was
expected from changing due ages alone.

## Restoration, reproducibility and remaining CPU work for Luna

[Artifacts](../benchmark-results/pog3-pi4-render-schedule-2026-10-07/) retain both
layouts' patches, source/build context, executable and recursive artifact hashes,
original numerical receipts, automation source/build script, replay and summary
parsers, all raw aggregate tails, wisdom and service/thermal metadata. Only the
two identical-fixture refined rounds are combined. Numerical runners restore
and independently read back PIDs 8784 and 9530; refined timing and confirmation
restore PIDs 9887 and 10241. The first rejected timing restoration/cleanup is also
verified separately. All probes/runners return zero. All three scheduling probe
directories, plus the prior profile directory, are removed after retrieval and
hash/byte checks. Only this session's build containers are removed. Temporary float
blobs are removed narrowly after comparison; prepared caches remain.

No firmware/application, configuration, governor, live buffer, NAM quality or
public `mod/pog3` integration is changed. These unpaced offline wall durations
are not ALSA xruns or paced FIFO/chain endurance. Fixed benchmark callback
alignment, one core fixture and boundary thermal receipts are not all-offset,
all-workload or continuous thermal tests. The current effect still consumes
roughly 87% of a core period on average and retains over-period peaks. The next
work remains CPU reduction, before public integration or live admission.

1. Keep the refined four-source core as immutable baseline. Profile **Attack and
   primary interpretation internally**, using standalone copies with nested
   exclusive costs and counts. For Attack separate stereo observation/indexing,
   grouping candidate/support scoring, owner lookup, binding/birth reservation,
   excitation envelopes and canonical matching/indexing. For interpretation
   separate magnitude scan, peak frequency/phase estimation, candidate selection/
   sorting, prediction association/birth allocation and region/history updates.
   Record relevant observation/candidate/family/active-track counts to explain
   data-dependent cost. Avoid overlapping substage timers or unbounded records.
2. Preserve the actual control bits/input-end stamps from the measured update;
   use Off/Attack=.11 as the primary matched fixture, plus zero-Attack and rapid
   controls as separate numerical/workload gates. Do not pool different fixtures
   or diagnostics with ordinary comparison times. Validate diagnostic exact
   output, allocation behavior and metadata before pedal profiling.
3. Choose one hotspot from those measurements. A candidate worth investigating
   is `PolyphonicAttack::group` support scoring, whose bounded candidate ×
   observation loop repeatedly classifies harmonics. Frequency indexing/pruning
   may exclude impossible supports, but the original strict harmonic predicate,
   float-boundary behavior, original support accumulation order, duplicate
   rejection order and family tie decisions must remain authoritative. Existing
   harmonic rounding and stereo/canonical search optimizations are already
   present; do not reintroduce earlier algorithms or assume this loop dominates
   without measuring it. Interpretation's phase/log work and track association
   are alternative targets if they measure higher.
4. Require exact default/opt-in automation/core traces, independent Attack quality
   gates (held sustain plus new note, re-plucks, stereo/family identity), and
   threshold/half-tie tests appropriate to the chosen math/index change. Retain
   current N+H staging, control cadence, Attack chronology and per-voice history.
   Run ordinary target ABBA and confirm a mean improvement alongside tails; merely
   reducing modeled comparisons/accesses does not establish CPU benefit.
5. Before claiming standalone admission, measure different callback start offsets,
   worst supported presets/automation and then paced 64-frame intended-chain
   thermal/xrun endurance. Keep live buffer/governor/other DSP quality unchanged.
   Attack/interpretation pipelining is a larger alternative only after an explicit
   snapshot/publication design preserving all low/long/short ownership and control
   timestamps. Moving those mutable stages is not justified by this renderer-only
   experiment. Freeze/gliss fidelity, listening/calibration, total memory/chain
   headroom and public integration remain open.
