# POG3 deferred Attack gain experiment — 2026-10-07

**Decision: reject the production gain refactor.** Actual Attack gain calls fall
34.666% and sine-ramp evaluations 35.081% in the measured fixture, with exact
audio and all DSP checks passing. However, two ordinary Pi ABBA rounds show only
**0.090%/0.189% combined mean reductions at 64/128 frames**, mixed 64-frame
results and a higher 128-frame maximum. Uninvolved controls also vary. This does
not justify changing the current production path for CPU performance.

Production Attack files are restored exactly to `4de167c00694b53bd4e61d8fdba80659958d22a7`.
The retained compact birth-slot workspace, four-source rendering, refined due
ages and default-disabled freeze remain. The restored ordinary ARM benchmark
rebuild matches the immutable baseline ELF byte for byte. New standalone gain
counters and diagnostic generator corrections remain available for further work.
The rejected source patch is archived, not left as an unused production option.

## Hypothesis and ordering review

The original envelope function both updates the current partial's history and
evaluates its gain. Short-window canonical matching then frequently replaces
that gain with the corresponding low/primary history's gain. The candidate
changes `envelope` into a state-only `updateEnvelope`, retains its mutation
position, keeps canonical lookup after that update, selects the current or
matched canonical partial, and evaluates `gainAt` once for that selected history.

Every birth, excitation merge/release, sustain/magnitude/onset mutation and
capacity event remains in original order. Ownership and its family updates stay
before the envelope update. Canonical lookup keeps its original strict distance,
lowest-original-slot tie and release-age checks. It reads a different resolution
when substituting gain; that history is unchanged by the current update.
`gainAt` is const and only reads the selected partial and current Attack seconds.
The current partial cannot change during lookup. Attack zero still performs its
original state mutation and returns unity through the unchanged gain function.
No approximation, reduced history, phase change, cached gain or moved timestamp
is introduced. This is a structural equivalence argument supported by finite
traces, not universal numerical proof.

No member layout, workspace size, prepared allocation, FFT/backend, render job,
latency or public control changes. Original headers are still used for baseline
probes and candidate headers for candidate probes. All measured binaries and
candidate inputs are preserved before restoring production source.

## Actual work counts

Standalone Attack diagnostics now record actual `gainAt` entries, actual
nonzero-excitation sine-ramp evaluations, and canonical replacements, separately
from elapsed stage timing. Fixed record storage is touched outside processing;
no counter code reaches ordinary builds. Both original and candidate source
shapes are supported so the archived experiment is reproducible.

The 4-second Pi core fixture produces 2622 ordered Attack updates at either
callback size. Observation, candidate, family, birth, control and input-stamp
records match across builds and callback partitions. For every individual update,
the reduction in gain calls equals its canonical replacement count. Ramp counts
are bounded by four per gain call and never increase in the candidate.

| Resolution | Updates | Original gain calls | Candidate gain calls | Canonical replacements | Original sine ramps | Candidate sine ramps |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Primary | 749 | 194288 | 189657 | 4631 | 171120 | 167027 |
| Short | 1499 | 430468 | 216894 | 213574 | 382830 | 191553 |
| Low | 374 | 4691 | 4691 | 0 | 2968 | 2968 |
| All | 2622 | 629447 | 411242 | 218205 | 556918 | 361548 |

Short gain calls fall 49.614% and sine ramps 49.964%; primary changes are only
2.384%/2.392%, while low is unchanged. The matched fixture uses positive Attack.
These percentages are work-count reductions, **not whole-effect CPU speedups**.
In Attack-zero processing, the original envelope directly returns unity while
the prototype enters `gainAt` to return unity; it executes no sine ramp. The
positive-Attack count identity above must not be applied to zero-Attack counts.

Both diagnostic runs have 58,816 events per core partition. Stage accounting,
ordered 64/128 identity, control/input tags, deadlines and work-count bounds pass.
Both diagnostic full-core traces equal the ordinary baseline with recording
active. Their instrumented timings are excluded from the CPU comparison.
All baseline/candidate coarse/detail Attack units compile with strict host and
AArch64 warnings. The optional nested timer's envelope label measures state
plus current gain in the original version but state only in the prototype; its
final selected gain is in the partial remainder. Do not compare those child
durations as if their scopes represented unchanged work.

The generator also now closes its reservation timer before declaring the
retained birth-slot flag. Its former scope made the flag unavailable in the
subsequent partial loop when instrumenting the preceding birth-slot change.
This was a standalone generator issue, not a production DSP issue. The first
profile build exposed it; corrected baseline/candidate profiles and strict
coarse/detail compilations precede every measurement here. The own Ubuntu
container also required copying GNU make before building. Failed setup/build
attempts are not timing samples.

## Numerical and DSP checks

Original/prototype output matches byte for byte across **1966080 automation
floats plus 384000 moving full-core floats on host and Pi**, separately per
architecture. Host opt-in freeze automation also matches all 1966080 floats.
The retained oracle exercises two reset/startup/drain passes and 14160 control
events around frame/control boundaries, Attack zero/positive, Focus/Warp and
available expression modes. It emits every stereo voice, mixed/wet/dry output
and control/freeze diagnostics, checking health and staged deadlines throughout.
Existing Attack and expression tests cover re-plucks, held/new/shared harmonics,
Focus reversal, state retention, partition invariance and reset. Exact output
does not establish a useful CPU improvement.

All nine default DSP suites pass (45.64 s), as do ten opt-in suites (71.77 s),
including full freeze/gliss and required-preload C callback allocation/free
checks. Automation passes ASan/UBSan with leak checking. Changed Attack units
pass strict host/AArch64 warnings with both freeze definitions. The Pi pitch,
Focus, Warp and overload checks pass before timing. Restored ordinary host
library/test targets are rebuilt with freeze OFF; nine suites are registered.
Their production source equals the preceding tested baseline exactly, so the
candidate suites are not represented as a new test run of the restored source.

## Two ordinary hardware rounds

Both rounds use the same hashed original/prototype ELFs and shared 57-plan FFTW
wisdom. Baseline is the preceding retained compact-birth-slot candidate ELF.
Buildroot GCC 13.4.0 uses -O3/-DNDEBUG and static float NEON FFTW 3.3.10. Freeze
and all profiling are OFF. Each round runs baseline-forward, candidate-forward,
candidate-reverse, baseline-reverse; reverse swaps callback-size order. Fixtures
are unchanged: 4-second warmup, reset and 4-second measured moving stereo core,
all voices, processed dry, Focus, Attack, filter/detune/Spread and Off expression,
with separate granular/spectral controls.

The Pi 4 uses CPU 2, unpaced SCHED_OTHER, 48 kHz and 64/128 frames. Boundary
snapshots remain performance/1.5 GHz; first-round initial temperature is 55.99°C
and final confirmation 57.94°C. These offline over-period durations are not
measured ALSA xruns or paced FIFO/intended-chain admission. Four repeats do not
establish statistical significance, and offsets/presets/endurance remain open.

| Core mean reduction; negative means slower | 64 frames | 128 frames |
| --- | ---: | ---: |
| First ABBA | −0.323% | +0.110% |
| Confirmation ABBA | +0.500% | +0.269% |
| Combined, four runs per build | +0.090% | +0.189% |

| Build | Frames | Mean period demand range | Highest p99 µs | Worst µs | Over-period callbacks per run |
| --- | ---: | ---: | ---: | ---: | ---: |
| Original Attack | 64 | 84.37–84.90% | 1362.278 | 1502.593 | 55–78 / 3000 |
| Deferred gain | 64 | 84.27–84.76% | 1377.111 | 1494.333 | 46–94 / 3000 |
| Original Attack | 128 | 84.19–84.86% | 2602.592 | 2747.667 | 2–5 / 1500 |
| Deferred gain | 128 | 84.05–84.56% | 2601.148 | 2958.519 | 3–6 / 1500 |

Combined mean callback times are 1127.877 → 1126.865 µs at 64 and
2254.508 → 2250.235 µs at 128. Pooled over-period counts are essentially
unchanged at 64 (**273 → 272 / 12000**) and worse at 128 (**15 → 18 / 6000**).
The candidate's worst 128-frame callback is 7.674% higher; it remains retained
in raw evidence, not discarded as an outlier. Both builds still exceed periods.

Combined granular mean reductions are −0.071%/−0.058% at 64/128, and spectral
control reductions +0.477%/+0.287%. In confirmation, spectral controls improve
1.742%/0.642%, exceeding the core's changes. Controls are not subtracted into
a corrected result. The first 64-frame regression, tiny pooled gain, unchanged
64-frame over-period counts and worse 128-frame maximum support rejection.
The evidence does not identify a unique compiler/cache cause, nor prove that
eliminating gain work cannot help another configuration.

All 48 ordinary timing rows have zero callback C++ allocations. Core FFT maxima
remain 9/16 and requested C++ preparation 1622696 bytes at 64/128, unchanged
between builds; FFTW internals are excluded. All eight timing and two diagnostic
wisdom exports retain the same header and 57 planning records. Existing service
restoration is independently checked after each runner; unique probe files and
comparison float blobs are removed after retrieval. No firmware/application,
configuration, governor, buffer or NAM quality change is deployed.

## Reviewed next CPU work for Luna

Keep production exactly at the compact-birth-slot baseline. Do not start with
approximate sine or a different math library on the strength of the count result.
Prioritize **primary interpretation substage attribution**, with track birth
selection as a concrete memory-access candidate, and harmonic scoring as the
alternative if counts show that birth scans are small.

1. Add standalone coarse scopes inside `PitchFrame::update` for magnitudes,
   peak/frequency estimation, overflow/ordered candidate sorting, track aging and
   prediction sorting, association/birth allocation, and region/history finalization.
   Preserve input stamps, channel/resolution labels and exact 64/128 event identity.
   Count peaks before/after the 256 cap, phase versus log estimation, predicted
   tracks, matched versus new tracks, and birth scan work. Avoid per-bin clocks.
2. The current birth selector first chooses the lowest unused slot whose track
   is empty **or** has `missed > 4`. Empty and expired slots share one priority
   tier; empty must not take precedence over a lower-index expired slot. If none
   is available, it chooses the largest positive `missed`, breaking ties by lowest
   index; generated `missed == 0` is excluded by the original strict `> oldest`.
   This differs from Attack's empty-then-oldest rule.
3. Tracks are aged once before association with `min(missed + 1, 5U)`. Unselected
   keys remain unchanged; selected histories become used and reset missed to zero.
   Sorted prediction keys remain the original pre-loop keys. If births are dense,
   test one lazy compact slot snapshot per frame update, ordered by the combined
   empty/expired tier then missed 4/3/2/1, each ascending index. A fixed counting
   arrangement can avoid general sorting. Skip slots used by intervening predicted
   matches. Preserve the existing fallback/sentinel and every capacity event.
4. Preserve the original prediction window, strict distance and lowest-track ties,
   ascending-bin observation order, velocity clamp, generation identity, missed
   aging, region partitioning, previous-spectrum copy and startup/log/phase fallback.
   Do not reserve predicted matches unless the baseline already did so: earlier
   births may legitimately consume a slot a later prediction would otherwise use.
5. A member workspace per frame changes bank size (six channel/resolution frames).
   Report its actual preparation delta; prefer a compact fixed representation.
   Use each build's matching headers for every baseline/candidate probe. Add an
   independent original birth-selector differential oracle with empty/expired
   priority ties, intervening uses, reset/exhaustion and all bounded missed ages.
6. Verify exact actual frame/track identity as well as bank/full-core automation.
   Serialize each public region field, center timestamp and capacity count;
   do not compare struct padding as DSP state. Include births, stable/dense/sparse
   peaks, resets, startup, invalid frames and prediction-distance boundary cases.
   Check host/Pi default and host opt-in, nine/ten DSP suites, allocation and
   affected sanitizers. Only select production behavior after ordinary matched
   ABBA plus confirmation, recording all controls/tails/FFT/preparation/wisdom and
   service receipts. A profiler count reduction alone is insufficient.

If phase/frequency estimation dominates, count how often a detected peak at bin
k already required phase estimation at that same bin in the preceding frame.
A sparse-valid previous-phase cache could reuse the identical float `std::arg`
result rather than recomputing it from the saved complex value. Measure that
reuse before implementation; calculating phase for every bin would add work.
Retain Cartesian fallback for cache misses, startup/log estimation, current-bin
thresholds and invalid-frame/reset behavior. Validity must describe exactly the
preceding frame, not any older detected peak. Preserve original float subtraction
before the double phase-step expression and principal wrapping. Count memory and
prove frame/track identities before testing ordinary CPU benefit.

If association births are not material, inspect Attack's candidate × observation
support scorer (the preceding coarse profile measured 93.500 µs per primary
update). Compact frequency/magnitude keys may improve reads. Retain original
division/strict harmonic classification, accepted-candidate order, support
accumulation order and score/family ties. Avoid replacing division with a rounded
reciprocal or reordering sums without a separately justified numerical contract.

## Evidence and reproduction

[Retained artifacts](../benchmark-results/pog3-pi4-attack-gain-2026-10-07/) contain
the rejected patch against `4de167c`, candidate and original-header/source/build
hashes, profile generator snapshot, strict-warning and DSP/sanitizer logs,
immutable/target ELF receipts, ordinary first/confirmation results, every control
distribution, FFTW wisdom and service/cleanup receipts. `diagnostic/` contains the
successful numerical gate, exact active-profile traces, compressed raw counter
logs and independent accounting/count/wisdom parsers. The ordinary CPU rows are
kept outside that directory. Reproduction instructions and a recursive checksum
manifest are included; float blobs and executables are not committed.
