# Poly Octave 3 implementation status

Updated: 2026-10-08. This records the implementation increments against
[the implementation plan](pog3-effect-implementation-plan.md). The parameter
contract, audible granular comparison harness, and streaming spectral foundation
are implemented, along with a spectral five-voice pitch bank, continuous Warp,
and reversible Focus switching. Independent spectral attack and a reversible
Dry Attack router, separate filter AD, LP/BP/HP buses, detune/doubling,
asymmetric Spread, voice pan, and the static sound path are now implemented.
The prepared processor supports Off, Volume, Crossfade, Warp and Filter by
default. Both freeze modes and their gliss/hysteresis/eligibility behavior remain
implemented in an opt-in experimental build. The preceding matched default-core
experiment observes 85.94–86.66% of a Pi callback period after inactive freeze
work is removed and four-source accumulation is added; callback peaks still exceed
deadlines. The batching comparison observes a modest 0.94–1.28% mean reduction,
with smaller changes also present in uninvolved controls. A subsequent uniform
two-source experiment is rejected: it is slower at 64 frames and has worse
over-period counts at both sizes. A hybrid pairing only leftovers also adds too
little repeatable CPU benefit to retain (0.440%/0.085% combined mean reductions;
confirmation 128-frame demand regresses). Four-source batching remains in
production. A callback-phase profile identifies analysis/Attack/render collisions. A refined
long-render schedule is now retained: two matched Pi ABBA rounds reduce pooled
64-frame over-period callbacks by 79.3%, with essentially unchanged mean demand
(−0.171% mean reduction at 64, +0.229% at 128). The initial layout is rejected
for worsening 128-frame tails; the refinement keeps the original 128-frame job
assignment in the fixture. The 128-frame tails remain mixed and both sizes still
exceed period. Attack substage profiling then identifies repeated partial-table
birth scans. A compact, lazily built birth-slot workspace is retained: two matched
Pi ABBA rounds repeat 2.080%/2.364% lower mean demand at 64/128 and reduce pooled
64-frame over-period callbacks by 67.865% (862 → 277/12000). Candidate demand
remains 84.11–85.40% and callback tails still exceed periods. Original slot
priority, histories, envelopes and timing are preserved; exact automated audio,
a 528384-selection differential test and all nine/ten default/opt-in suites pass.
See [Attack results and reviewed Luna next steps](pog3-attack-cpu-results.md).
The workspace adds 4136 bytes, bringing requested C++ preparation to 1622696,
excluding diagnostic BSS and FFTW memory. A subsequent deferred-gain prototype
avoids 34.666% of Attack gain calls and 35.081% of sine ramps, but its two Pi ABBA
rounds yield only 0.090%/0.189% combined mean reductions at 64/128, mixed 64-frame
results and a worse 128-frame maximum. It is rejected and original production
Attack is restored exactly; the rebuilt ordinary ARM benchmark matches the
immutable baseline ELF. Standalone gain counters and generator scope/signature
handling are retained. Next, profile primary interpretation internally before
choosing a compact track-birth snapshot or sparse-valid previous-phase cache;
see [the rejected gain experiment and detailed next steps](pog3-attack-gain-results.md).
CPU tails, complete memory/combined-chain endurance, calibration/listening and
public integration remain open.
There is no selectable `mod/pog3` entry yet.

Development continues in `/home/bbalazs/projects/ardor-pog3` on
`feat/pog3-polyphonic-octave`, based on current `main`, in
[draft PR #113](https://github.com/balazsbencs/ardor/pull/113). The original
workspace and its unrelated changes remain separate.

## Implemented code

The latest device milestone is a **passing fixed two-block CPU3 worker probe**:
POG3 plus the reference serial NAM/EQ chain finishes two 30-second cases and a
180-second case with zero xruns or pipeline misses. The longer run averages
1160.527 µs on CPU2 and 2324.920 µs on CPU3 per 2666.667 µs period. Direct serial
and one-block worker controls still fail. Added delay is **256 samples / 5.333 ms**;
its playing feel and the normal MMAP/UI backend remain to be tested. This is not
general routing or public integration. See
[the complete results, ownership review and next steps](pog3-worker-pipeline-results.md).

| File / target | Responsibility |
| --- | --- |
| `src/daisyfx/pog3/Pog3Parameters.{h,cpp}` | Stable 33-index registry, defaults, normalized-to-physical mappings, display formatting, configuration validation, expression endpoints, Warp and dry-freeze eligibility helpers, lock-free control targets |
| `src/daisyfx/pog3/SpectralFrameStream.{h,cpp}` | Shared immutable FFT/window plans, causal streaming analysis, preallocated inverse FFT and overlap-add synthesis |
| `src/daisyfx/pog3/PolyphonicPitchBank.{h,cpp}` | Shared short/long/low analysis, bounded persistent partial tracks, fractional spectral translation, low-band reconstruction, independent stereo synthesis, continuous Warp, Focus fades, staged render jobs |
| `src/daisyfx/pog3/PolyphonicAttack.{h,cpp}`, `AttackBirthSlots.h` | Linked stereo harmonic families/residual partials, fixed old/new excitation history, input-timestamp attack ages, coherent gains across resolutions, compact exact-order birth allocation |
| `src/daisyfx/pog3/SpectralFreeze.{h,cpp}` | Fixed stereo capture/target storage, startup validity, heel/toe state machine, linked target proposals, bounded matching and partial gliss |
| `src/daisyfx/pog3/Pog3VoiceStages.{h,cpp}` | Separate linked detector/filter AD, stereo dry/generated TPT filter buses, levels/pan, upper/dry doubling, eligible 1:3 Spread, final master, static sound-path composition |
| `src/daisyfx/pog3/Pog3Processor.{h,cpp}` | Prepared lifecycle, immutable endpoint configuration, 33 key/index targets, 48-sample control cadence, separate base/effective values, all seven expression modes and freeze diagnostics |
| `tests/pog3_granular_reference.h` | Five independent stereo pitch shifters using the existing Whammy/Harmonizer primitive; dry, six levels/pans, input gain, and final master |
| `tests/pog3_controls.cpp` | Persisted index contract, both target setters, mappings, morph ownership, rejection/clamping, Warp and eligibility behavior |
| `tests/pog3_quality.cpp` | Active FFTW numerical accuracy, exact shared fallback, concurrent shared-plan execution, identity/delay/startup/drain/chunking/reset, isolated reference tuning, stereo/pan/gain, nonfinite/overflow rejection and optional WAV renders |
| `tests/pog3_pitch_quality.cpp` | Spectral tuning/spurs/leakage, resolved and ordinary low chords, alias rejection, track continuity, Focus reversal, staged identity/deadlines, callback partitioning, Warp, overload/drain, envelope latency, close-pair diagnostic |
| `tests/pog3_frame_birth_slots.cpp` | Independent original frame-birth oracle: 758880 selections across bounded missed ages, combined empty/expired tiers, ties, intervening uses and exhaustion |
| `tests/pog3_attack_birth_slots.cpp` | Differential original-scan oracle for 528384 birth selections, including reservations/ties/intervening uses/reset/exhaustion and timestamp boundaries |
| `tests/pog3_attack_quality.cpp` | Held/new notes, shared harmonics, low bass, re-plucks, arpeggios, bends, Focus reversals, activation, exact-off dry, reset, partition invariance, optional attack WAV renders |
| `tests/pog3_voice_stages.cpp` | AD timing/retrigger, held-tone/chord detector, sensitivity/re-plucks, filter transfer and resonance, routing eligibility, delay endpoints/queues, pan, rapid automation/partition/reset, gain/overload/recovery, optional full-path WAV renders |
| `tests/pog3_expression_quality.cpp` | Processor key/index publication, cadence, immutable ownership, exact endpoints/units, processed-dry exclusions, 30 Warp tuning cases, Filter/envelope interaction, 7-bit mode/reverse/Focus automation, callback partitions, configuration/reset and optional expression WAV renders |
| `tests/pog3_freeze_quality.cpp` | Stationary pitch/level/stereo, genuine moving-carrier gliss, mid-glide latch/resume, strict dry/Focus eligibility, hysteresis/Reverse, startup/silent capture, dense input/reset, partition invariance, two 60-second holds and optional freeze WAV renders |
| `tests/pog3_artifacts.h` | Shared offline float WAV writer; preserves raw levels |
| `tests/pog3_bench.cpp` | Prepared mean/percentile callback timing, transform burst counts, reset cost, allocation/health instrumentation and CSV output |
| `tests/pog3_malloc_probe.c` | Test-only glibc C allocation/free interposer for the complete prepared benchmark; absent from DSP/application linking |
| `src/daisyfx/hosted/dsp/pitch_shifter.cpp`, `tests/pitch_effect_quality.cpp` | Correct rounded negative ring positions before interpolation; public-API regression covers upward Warp at the ring endpoint |
| `ardor_realtime_fft` | Sole CMake ownership of the existing `RealtimeFft.cpp`; shared with the existing DSP/convolver target |
| `ardor_pog3` | Independent parameter/spectral/pitch/attack/voice-stage/processor library; links single-precision FFTW and the shared fallback without a Daisy/DSP dependency cycle |

The CMake edits retain the unrelated changes already present in the workspace.
Existing catalog entries, scene indices and tracked device binaries remain
unchanged. FFTW changes active transform arithmetic within the numerical/audio
contract documented in the latest checkpoint. The demonstrated shared granular
read-index correction is documented with its original-reader reproduction below.

## Latest CPU checkpoint — nonoverflow bin sort, 2026-10-08

Retain skipping the final bin sort for at most 256 candidates: the peak scan
already appends bins in ascending order. Overflow still uses exact top-256
partitioning and the original bin sort. Two ordinary matched Pi ABBA rounds
repeat lower mean demand, **0.912%/0.562% combined at 64/128** against HEAD816.
Preparation stays **1625912 bytes**. Mean demand is
**78.883–79.460% / 79.153–79.508%**. Highest p99 falls at both sizes, but 64
maximum rises **1378.482 → 1457.204 µs** and pooled over-period counts rise
**16 → 25/12000**; 128 remains **0 → 0/6000**. This is a small mean improvement
with mixed tails. Transform-only spectral control improves too, so the full
observed difference is not isolated to sorting. Live/chain admission remains open.

All **10 default / 11 opt-in DSP suites**, affected sanitizers, strict warnings,
exact **2498-frame / 173482-region** host/Pi traces, default audio and host
opt-in automation pass. Existing 810-frame cutoff and 464-frequency phase-history
oracles remain. Final ordinary host targets are rebuilt freeze OFF. Both sparse
phase caches and the deferred gain split remain rejected.

Next try compact exact frequency/magnitude read keys in Attack support scoring,
filled after the original observation sort/cap, preserving division, strict
thresholds, support/score ordering and family history. Measure gather traffic,
prepared memory and ordinary CPU; do not assume a layout win. Keep both birth
snapshots, due ages, four-source rendering and opt-in-only freeze.
See [the reviewed ordering, measured tradeoffs and detailed Luna next step](pog3-bin-sort-results.md).

## Phase-cache rejection checkpoint — 2026-10-08

Two sparse exact previous-phase cache layouts are **rejected**. The per-bin
layout raises combined ordinary mean demand **0.540%/0.497%** at 64/128 frames;
the bulk-cleared double-validity layout raises it **0.559%/0.786%**. Each has
separate matched Pi ABBA and confirmation rounds. Actual standalone diagnostics
verify **533169 fewer previous-phase arguments**, a **31.566%** total argument
reduction, with identical frame/track/birth work. That call reduction does not
produce a useful CPU benefit.

Production caches are removed: preparation stays **1625912 bytes** and the
rebuilt ordinary ARM full-core ELF equals retained HEAD35b byte for byte. Both
candidate layouts pass **10 default / 11 opt-in suites**, exact **2498 frames /
173482 regions**, host/Pi default audio and host opt-in automation, affected
sanitizers and strict warnings. Final restored default suites pass again. Retain
an independent **464-frequency original Cartesian phase-history oracle** and
standalone actual-argument counters, alongside the existing peak/birth oracles.
Service restoration is independently read back; only session probe directories
and its cross-build container are removed.

Its proposed skipped nonoverflow bin-sort follow-up is now retained; see the
latest checkpoint above.
Keep birth snapshots, refined scheduling, four-source rendering and opt-in-only
freeze. CPU, listening and live/chain admission remain open.
See [both rejected layouts and the detailed Luna handoff](pog3-phase-cache-results.md).

## Exact peak selection checkpoint — 2026-10-08

Retain an in-place top-256 peak partition instead of the overflowing-frame full
magnitude sort. Unique bins retain the original magnitude-tie cutoff, and the
original final bin sort preserves publication. All phase calculations still run
before selection. Two ordinary matched Pi ABBA rounds show **1.976%/0.967%**
combined mean reductions at 64/128 against the retained frame-birth baseline.
Preparation stays **1625912 bytes**, excluding FFTW internals. Mean period
demand is **79.633–80.363% / 79.728–80.693%**. Pooled 64 over-period counts
fall **37 → 22/12000**; 128 is **0 → 0/6000** in these finite offline runs.
p99 improves, but recorded maxima worsen slightly (1429 vs 1417 µs at 64;
2610.037 vs 2601.685 µs at 128). Live intended-chain admission remains open.

All **10 default / 11 opt-in suites** pass. The pitch suite now includes an
independent original full-sort oracle over **810 published frames**, including
255/256/257 boundaries and cutoff ties. **1538 frame histories / 172698 regions**
are byte-exact on host/Pi, alongside default audio and host opt-in automation.
Affected sanitizer, strict-warning, C/C++ allocation and target pitch checks pass.
Retain both birth snapshots, refined scheduling, four-source rendering and
opt-in-only freeze. Its proposed sparse phase-cache follow-up is now rejected;
see the latest checkpoint above.
See [results and the detailed reviewed Luna handoff](pog3-peak-selection-results.md).

## Frame birth CPU checkpoint — 2026-10-08

Retain a second compact slot workspace for **frame track births**. It preserves
the original combined empty/expired ascending priority, then greatest positive
missed age / lowest index. Lazy per-frame preparation keeps original predictions,
track generations/velocities, region publication, phase math and timestamps.
Two ordinary matched Pi ABBA rounds show **4.933%/4.527% combined mean
reductions at 64/128 frames** against the retained compact Attack baseline.
Mean demand is now **80.327–81.568% / 80.505–81.210%**. Pooled over-period
counts fall **361 → 21/12000 at 64**, **17 → 0/6000 at 128**. The candidate's
worst 64-frame duration still exceeds its period; live intended-chain admission
is open. Preparation grows **3216 bytes to 1625912**, excluding FFTW internals.

All **10 default / 11 opt-in DSP checks** pass. Baseline/current frame fields
match exactly for 1088 synthetic histories; the real selector equals independent
original scans across 758880 selections. Host/Pi default processor/bank audio and
host opt-in automation remain byte-exact; affected sanitizer, strict warnings,
C/C++ callback allocation and target pitch checks pass. Standalone interpretation
attribution finds nearly 60 million original long-frame birth-scan iterations;
counts and all 5247 frame identities match across partitions/builds. Ordinary
benchmarks, rather than instrumented stage timings, justify retention.

Next, test bounded top-peak selection before phase caching. Keep frame/Attack
slot snapshots, refined long-render ages, four-source rendering and opt-in-only
freeze. The rejected gain split remains absent. See
[the complete results and reviewed Luna next-step plan](pog3-interpretation-cpu-results.md).

## Contracts established

- Numeric controls use the exact 0–32 index order and defaults in the plan.
  Keys and indices publish through the same validation path. Index 33 and
  nonfinite values fail; finite out-of-range values clamp to `[0,1]`.
- Control targets use fixed, lock-free float atomics. Reading the array does not
  promise an indivisible whole-scene snapshot. The future processor must consume
  targets at its control cadence and implement smoothing separately.
- Configuration parsing occurs off the callback and is transactional. Known
  values must be numeric and finite. Unknown editor data is ignored by DSP.
  Optional partial `crossfade_heel` / `crossfade_toe` objects use the configured
  base for missing values. Enumerated controls and expression settings cannot
  be morphed. Effective values preserve the original base controls.
- Crossfade interpolation occurs in normalized space. Filter expression maps
  the normalized heel/toe range. Reverse changes the expression coordinate.
  These are pure control helpers; Volume, Warp, and Freeze audio routing are
  not implemented by `effectiveValues`.
- Warp scales the nominal semitone intervals toward unison; Focus-off upper
  octaves remain fixed. Dry-freeze eligibility requires Dry Attack enabled and
  normalized Attack strictly greater than `0.1`.
- Plans are constructed off the callback. Each analysis owns its history and
  spectrum; renderers share only the immutable plan. `push`, `pop`, `addFrame`,
  and `reset` retain their allocated capacities.
- Use `synthesis.pop()` before `analysis.push(sample)` for every host sample;
  on a completed frame, call `synthesis.addFrame(analysis.spectrum())` before
  the next sample. The analysis frame ends at the sample just pushed; its first
  synthesized sample is scheduled for the next `pop()`.
- This causal schedule has an exact identity delay of **N samples**, including
  startup. At 48 kHz this is **21.333 ms** for 1024 and **42.667 ms** for 2048.
  These are foundation identity delays, not measured production wet latency.
  Spreading transform jobs across samples will require explicitly revising the
  schedule and retesting its delay, as described in the plan.
- Periodic Hann analysis windows use a synthesis normalization derived from
  the sum of overlapping squared windows. The tested overlap factors include
  `N/8`, `N/4`, and `N/2`; no fixed Hann gain constant is assumed.
- Nonfinite input samples become zero history. Invalid spectrum sizes,
  nonfinite bins, inverse FFT overflow, and OLA overflow reject the entire frame
  before changing pending output. Repeated reset clears histories and timelines.

## Verified results

The six baseline tests passed before the new code. At the foundation/pitch/Attack
checkpoint, all eleven selected regression tests passed (latest M7 results below):

```text
pedal-scheduled-convolver-smoke
pedal-scene-plan-smoke
pedal-daisy-fx-catalog-smoke
pedal-manager-effect-catalog-smoke
pedal-pitch-effect-quality
pedal-pog3-controls
pedal-pog3-quality
pedal-pog3-pitch-quality
pedal-pog3-low-chord-quality
pedal-pog3-attack-quality
pedal-harmonizer-quality
```

The `pedal-poc` host executable also builds and links. The parameter, spectral
pitch, and ordinary low-chord suites pass under AddressSanitizer and
UndefinedBehaviorSanitizer; the foundation/reference suite passed in the prior
increment. The envelope-latency diagnostic also passes under both sanitizers.
The Warp onset regression and envelope-latency diagnostic were rerun under both
sanitizers after final review. The new C++ files compile cleanly with
`-Wall -Wextra -Wpedantic`.

On the isolated M4 branch, the full attack suite, parameter suite, low-chord
suite, and elapsed-time Warp diagnostic also pass ASan/UBSan. The instrumented
attack suite takes about 344 seconds; the optimized suite takes about 19 seconds.

Identity tests use deterministic noise, startup and drain, impulse delay, absolute
hop/frame timestamps, and callback partitions of 1, 17, 48, 64, 128, and 256 samples.
Partitioned and contiguous results are exactly equal. Maximum per-sample error
is below `1e-6`; residual RMS relative to the input ranges from **−139.91 to
−137.10 dB**, exceeding the −90 dB gate.

The granular reference's isolated 196 Hz test has these tuning errors:

| Interval | Error (cents) |
| --- | ---: |
| −24 semitones | +0.0452 |
| −12 semitones | −0.0644 |
| +7 semitones | +0.0963 |
| +12 semitones | +0.0000124 |
| +24 semitones | −0.00504 |

Additional reference checks verify immediate dry identity, centered anti-phase
stereo, right-only input folded to hard left, exact hard-pan mute, input/master
gain each applied once, and exact zero-master mute. These results validate the
comparison harness; they do not establish polyphonic spectral pitch quality.

## Reproducing the evidence

```sh
cmake -S . -B build-ci -DARDOR_UI_BACKEND=none -DCMAKE_BUILD_TYPE=Release
cmake --build build-ci -j 4 --target \
  pedal-pog3-controls pedal-pog3-quality pedal-pog3-pitch-quality pedal-pog3-attack-quality \
  pedal-pog3-voice-stages pedal-pog3-expression-quality pedal-pog3-bench \
  pedal-pitch-effect-quality pedal-harmonizer-quality \
  pedal-daisy-fx-catalog-smoke pedal-manager-effect-catalog-smoke \
  pedal-scene-plan-smoke pedal-scheduled-convolver-smoke pedal-poc
ctest --test-dir build-ci --output-on-failure \
  -R 'pedal-(pog3-controls|pog3-quality|pog3-pitch-quality|pog3-low-chord-quality|pog3-attack-quality|pog3-voice-stages|pog3-expression-quality|pitch-effect-quality|harmonizer-quality|daisy-fx-catalog-smoke|manager-effect-catalog-smoke|scene-plan-smoke|scheduled-convolver-smoke)$'
build-ci/pedal-pog3-quality --render build-ci/pog3-artifacts
build-ci/pedal-pog3-attack-quality --render build-ci/pog3-artifacts
build-ci/pedal-pog3-voice-stages --render build-ci/pog3-artifacts
build-ci/pedal-pog3-expression-quality --render build-ci/pog3-artifacts
build-ci/pedal-pog3-bench --csv build-ci/pog3-artifacts/expression-processor-benchmark.csv
build-ci/pedal-pog3-pitch-quality --resolution-stress
build-ci/pedal-pog3-pitch-quality --latency
build-ci/pedal-pog3-pitch-quality --warp-onset
```

The reference portion creates ten 48 kHz stereo float WAV files: input tone, five
isolated voices, input chord/bass onset, combined reference chord, and two 7-bit
Warp ramps with Focus off/on. It reports raw peak and RMS without normalizing or
limiting. The reference chord peak is about `0.1051`; the two ramp peaks are about
`0.1114` and `0.1356`. Audio and CSV artifacts remain under the ignored build
directory. Fourteen additional spectral WAVs cover the five isolated voices with
both Focus settings, combined chords, and both 7-bit Warp performances. The
combined renders use immediate dry and five generated voices at level 0.25,
matching the reference mix. Spectral chord peaks are about 0.1158/0.1167 for
Focus off/on; the Warp peaks are about 0.1181/0.0982. Listening review remains open.

For sanitizers, configure a separate Debug build with
`-DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'` and
`-DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'`, then build and run the
POG3 test targets. An additional `-O1` was used for the pitch sanitizer build;
the full pitch suite took about six minutes under instrumentation. Performance
measurements must use the optimized, nonsanitized build.

## Foundation timing and memory baseline (M0–M2)

The host evidence was captured on an Intel Core i3-8100T x86-64 system, GCC 14.2,
Release `-O3`, ordinary IEEE float behavior. Each workload uses four seconds of
dense stereo chord/noise input after warmup: 3000 callbacks of 64 samples or
1500 callbacks of 128 samples. CSV includes median, p95, p99, p99.9, maximum,
reset time, allocation counts, and maximum transforms per callback.

The granular workload runs all ten shifters, including muted voices. Its
preparation allocates **329,256 bytes**, including **327,680 bytes** of histories.
The spectral workload runs two long and two short analyses with six long stereo
renderers and two short stereo renderers. This represents the transform work
during a future Focus transition, including the dry/unison renderer. Preparation
allocates **595,696 bytes**; this is cumulative requested allocation size, including
objects/plans, rather than a complete future processor memory inventory.

One optimized host run produced the following timings (microseconds):

| Workload | Callback samples | Median | p99 | Maximum | Callback budget |
| --- | ---: | ---: | ---: | ---: | ---: |
| Granular reference | 64 | 20.008 | 654.267 | 670.211 | 1333.333 |
| Spectral identity / Focus transition | 64 | 7.319 | 773.602 | 821.101 | 1333.333 |
| Granular reference | 128 | 39.839 | 677.183 | 685.761 | 2666.667 |
| Spectral identity / Focus transition | 128 | 197.880 | 809.274 | 1039.015 | 2666.667 |

Measured reset cost was 11.8–16.2 microseconds on this host. These are observed
samples, not guaranteed worst-case execution times; repeat on the audio target.

All measured processing, reset, target publication, and pure expression helper
calls make **zero C++ allocations**, including aligned allocations. The spectral
workload can perform **20 transforms in one callback** when both hop boundaries
coincide. Inspect the upper percentiles and maximum, since the median often
contains no FFT work. The reference also has synchronized grain-restart bursts.

The two foundation workloads above exclude spectral pitch mapping, partial/family tracking,
envelopes, filters, space stages, and freeze/gliss. Target-device admission,
combined NAM/IR chains, xruns, and endurance have not been measured. Do not use
the host identity cost as evidence that the full effect meets audio deadlines.

## Spectral pitch implementation and measured adaptations

This section records M3. M4's attack, processed unison, and revised job schedule
are described below; the M3 timings are retained as historical evidence.

The bank returns six stereo voices before levels, pan, filter, space, or master.
Voice zero is processed unison. The future processor must use immediate input
for unprocessed dry rather than mixing this delayed unison unconditionally.
All DSP setters and diagnostics are audio-thread owned; external controls must
be published through the fixed target array.

The main analysis uses N=2048/H=256 and N=1024/H=128, with one analysis per
channel/resolution. Local peaks use phase-difference instantaneous frequencies
and bounded predictive association, retaining track generation across FFT-bin
changes. Regions retain complex relative phase and high-band residual/noise.
Each renderer owns its phase/OLA history. Fractional translation uses a centered
24-tap Lanczos kernel with 512 prepared fractional phases; coefficients scatter
into destination bins, adding colliding contributions. Below-zero analytic
support is conjugate-reflected, and upper-edge regions are tapered/rejected.

The ordinary low C-major chord initially failed badly with the original two
windows: multiple fundamentals merged and shifted components lost up to about
44 dB. A shared **N=4096/H=512 low-band analysis**, limited to peak interpretation
through 400 Hz, fixes that measured defect. Its resolved partials reconstruct
individual Hann lobes in the existing output IFFTs. A complementary 200–300 Hz
crossover preserves primary high-band regions, with phase alignment tapering
out by 400 Hz. This adds two shared forwards per low hop, rather than another
IFFT for each voice. Low unpitched content is represented by these tracked
partials; transient/noise fidelity in that band still needs listening review.
Carrier normalization rejects incoherent phase estimates outside the peak's
main lobe instead of dividing by a near-zero sidelobe. Frames and banks cannot
be copied: their frame spans refer to their own analysis storage.

Both upper variants remain warm at all times. Focus changes apply one reversible
25 ms sample-based fade to the upper pair. The short path remains at nominal
+12/+24 even during Warp; the long upper path follows Warp. Lower/fifth voices
always follow Warp. Extent slews over 10 ms; rendering integrates frequency over
actual hops without chromatic quantization or phase resets.
New tracks seed their pitch offset from the analyzed carrier, rather than
absolute elapsed time. The late-onset regression warms identical periodic input
for two different durations, then starts the same Warp gesture; all five output
voices compare exactly (maximum sample error zero).

The initial synchronous pitch workload exceeded callback budgets. Scattering
source coefficients reduced interpolation work; renderer jobs are now spread
within one additional hop per resolution. Long jobs complete by sample 234 of
256, short jobs by sample 96 of 128. Input history continues separately while
spectra/regions remain immutable until their next frame; every job completes
before any frame it uses can change. Every window starts at `t+1+H`, regardless
of its job's execution sample, using the synthesis stream's explicit start offset.
There are no worker threads, queued allocations, or unbounded job lists.

This revises the bank's main identity delay to **N+H**: 2304 samples / **48 ms**
for long and 1152 samples / **24 ms** for short. The streaming foundation's
zero-offset identity remains N. Tests verify all six collapsed-unison jobs at
exactly N+H, zero logical deadline misses, and exactly equal output under
1/17/48/64/128/256 callback partitions with timestamped control changes.

The low-band analysis has a further latency tradeoff. An isolated +1 octave
tone burst's energy-centroid delay measures:

| Input | Focus off | Focus on |
| --- | ---: | ---: |
| 82.4 Hz | 60.022 ms | 72.006 ms |
| 659.3 Hz | 24.009 ms | 48.001 ms |

These are measured envelope delays for the stated synthetic burst, not one
universal latency number. Unprocessed dry is still intended to remain immediate.
Low-note latency must be included in the eventual listening/product decision.

The spectral quality suite covers 100 nominal tone cases: five intervals, both
Focus settings, and inputs 65.4/82.4/110/146.8/196/329.6/659.3 Hz plus detuned and
half-bin cases. The measured worst tuning error is **0.0054 cent**, worst unwanted
tonal spur **−63.23 dBc**, and worst original-frequency leakage **−108.74 dBc**.
Half-Warp pitch/quality tests run with both Focus settings, including the fixed
Focus-off uppers. Centered anti-phase input and a silent
independent channel survive; Focus reversals preserve both warm upper histories.

All shifted fundamentals survive a resolved chord/inharmonic fixture and the
ordinary low C-major chord with/without additional harmonics. The latter's
largest level deviation is **0.17 dB** across 60 component checks, inside the
3 dB gate, including down-two
collisions. Three +24 out-of-band tones (7.0013, 10.0037, and 17.0032 kHz) are
rejected in both Focus modes. A moving
input partial retains its identity across several FFT bins. Overload at ±12,
rapid Focus/Warp changes, silence drain, and repeated reset pass.

An explicitly separate resolution diagnostic remains an honest limitation:
82.4069 and 87.3071 Hz together lose **8.77 and 9.23 dB** at their expected −24
partials. The current low analysis does not separate that 4.9 Hz pair. This
diagnostic is not reported as a passed chord gate or hidden by a loose estimator.

## Bank performance and memory before attack (M3)

The same optimized host and four-second dense stereo/noise workload, with rapid
Warp and Focus updates, now includes the complete three-resolution pitch bank:

| Callback samples | Median µs | p99 µs | Maximum µs | Budget µs | Maximum transforms in callback |
| --- | ---: | ---: | ---: | ---: | ---: |
| 64 | 522.291 | 1167.312 | 1320.079 | 1333.333 | 11 |
| 128 | 1230.343 | 1721.769 | 2575.939 | 2666.667 | 16 |

Requested preparation allocation is **1,913,744 bytes** (about 1.83 MiB), including
all plans, phase histories, analyses, and both warm upper variants. Every measured
prepared process/control/reset call allocates **zero** C++ objects. Reset takes
about 47–48 µs on this host. All logical job-deadline counters remain zero.

The extra low analysis adds two forwards at a coincident 512-sample event, for
22 transforms in that frame's work rather than the original 20; staging spreads
the render jobs across callbacks. Maximum observed host durations fit the stated
budgets, but the 64-sample worst observation leaves only about **13 µs**. Attack,
filter, space, freeze, multiple banks, and NAM/IR processing are excluded. There
is no full-block/combined-chain/device performance certificate. Preserve the
CSV distributions, include those later stages in the benchmark, and obtain
target measurements before public admission. The 2 MiB full-block memory goal
also remains open; the current bank alone consumes most of it.

## Freeze implementation caution for M7

The current live renderer advances a phase offset relative to the incoming
analysis phase; live input supplies the remainder of source phase motion.
Freezing a raw frame and repeatedly calling the live renderer unchanged would
produce the wrong pitch. A stationary held representation must advance the full
target synthesis phase, or explicitly advance the held source phase before
applying the live relative-offset route. Capture/hold/gliss must include the
4096 low-band representation as well as both primary resolutions; leaving its
source live would let new low notes replace the captured chord. Preserve staged
window timestamps and immutable job inputs during these mode transitions.
Store captured carrier amplitudes independently of the old analysis-bin
normalization: a gliss cannot change a captured frequency while retaining its
old bin/window-gain interpretation.

## Independent attack and dry route (M4)

The model uses up to 16 shared harmonic families and 256 residual/partial states
per resolution, with separate bindings for each channel. Noncancelling combined
peak magnitudes create shared identities; the renderer retains independent
complex stereo audio. Existing bindings are reserved before allocating births,
so a loud new observation cannot steal a quieter sustain's state.

This first family scorer accepts directly supported fundamentals in 40–2000 Hz,
with harmonic support through harmonic eight. It deliberately rejects phantom
subharmonics instead of implementing missing-fundamental inference; that
material keeps residual-partial envelopes. Bends update frequencies continuously.
The 35-cent association tolerance is not a pitch quantizer.

Each partial decomposes into established sustain and up to four new excitation
epochs. A positive increment swells with `sin²(pi*x/2)` over the mapped Attack
time; it does not reset the old component. Falling magnitudes proportionally
release the components. A 40 ms coalescing interval groups a rising pluck; the
per-hop rise threshold is 2%, with an absolute noise floor. Capacity counters
report bounded fallback merges. Rapid repeated picking at long Attack settings
can exhaust the four epochs and needs further listening review; coalescing may
shorten the newest increment's swell. Coincident/cancelling harmonics cannot be
perfectly separated from the mixed input.

Onsets use absolute input sample dates. Short-window beat fluctuations initially
caused false re-plucks on held chords; short paths now evaluate the resolved
low/long excitation history at their own frame-center timestamps. Low-band
history also controls matching long partials. This keeps Focus changes from
restarting a note. The low representation is enabled for processed unison when
Attack is active: a main-window region can otherwise merge low A/B and duck A.
At Attack=0 gain is exactly one, histories stay warm, and fresh unison retains
the full spectrum.

`DryAttackRouter` supplies a reversible 20 ms route fade, with exact immediate
stereo dry at its off endpoint. The future processor must enable it only when
Dry Attack is on and Attack is nonzero; dry freeze's stricter 10% eligibility
still applies separately. The helper does not introduce gain, pan, or master.

Attack interpretation is staged before rendering: low at its frame boundary,
long at age 1 sample, short at age 8. Long render jobs run at ages 17–251 and
short at 16–112, preserving the original `t+1+H` window start and N+H identity
delay. Cached gains and source frames remain immutable during their render jobs.

The 500 ms independence gate covers all six paths, both Focus settings, isolated
notes, shared harmonics, and the 82.4/130.8 Hz bass pair. Measurements fit held
and new components jointly: a naive 50 ms projection leaks held bass into the
new frequency. The tests require old level changes below 1 dB, at least 10 dB
suppression of B's first 50 ms, and settled level within 1 dB. Re-plucks preserve
old sustain while suppressing the positive increment. Three-note arpeggios,
continuous bends, Focus reversals, enabling Attack over sustain, reset, and
1/17/48/64/128/256 partitioned automation also pass. Across 36 independence
cases, the worst held-level change is **0.205 dB**, the least first-50-ms
suppression is **26.87 dB**, and the largest settled-level difference is
**0.0036 dB**. Enabling Attack over an existing generated sustain changes samples
by at most **2.98e−8**. Independent broadband noise additionally exercises
2,081 capacity fallbacks, family bounds, silence recovery, and reset under
sanitizers.

Seven additional float WAVs pair a synthetic arpeggio with Attack-off references,
500 ms Attack with immediate dry, and 500 ms Attack with processed dry, in both
Focus settings. Every fader is 0.25; files are unnormalized. These illustrate the
bank/router and do not claim a finished filter/space/master implementation.

The optimized M4 benchmark includes both warm upper variants and all voices:

| Workload | Callback samples | Median µs | p99 µs | Maximum µs | Budget µs |
| --- | ---: | ---: | ---: | ---: | ---: |
| Attack off, Warp/Focus updates | 64 | 580.910 | 1157.717 | 1293.609 | 1333.333 |
| Attack and dry-route/Warp/Focus updates | 64 | 591.434 | 1120.523 | 1294.885 | 1333.333 |
| Attack off, Warp/Focus updates | 128 | 1231.292 | 1657.229 | 1742.083 | 2666.667 |
| Attack and dry-route/Warp/Focus updates | 128 | 1315.083 | 1666.134 | 1852.894 | 2666.667 |

Prepared processing/control/reset allocations remain **zero**. Preparation
requests **2,065,592 bytes** (about **1.97 MiB**); reset costs about 51–75 µs.
Maximum transforms per callback remain 11/16 and logical deadline misses zero.
The 64-sample maximum leaves only **38 µs**. This evidence does not establish
full-block, arbitrary callback-phase, combined-chain, or target-device admission.
The memory goal is especially tight before filter, space, and held spectra.

## Filter and voice stages (M5)

`Pog3SignalPath` composes input gain, the existing attack/pitch bank, Dry Attack
selection, and `Pog3VoiceStages`. It accepts audio-owned normalized sound values;
it does not resolve expression modes or parse configurations. M6 will own base
versus effective controls. Input Gain occurs once before analysis and both onset
scorers, and Master occurs once after the separate dry/generated buses.

Each voice's level and center-preserving stereo pan precede its space lines.
Hard edges fold both input channels into the selected side with equal-power
mono gain. Only upper octaves receive doubling; the fifth receives Spread only;
neither suboctave receives either stage. Dry Detune enables both dry space
stages, including Spread when Detune depth is zero. Spread is a replacement
delay, without an extra direct path: `50u` ms left / `150u` ms right. Its two
fixed read anchors share a 10 ms fade and latest-target queue, preserving the
1:3 relationship during automation. Zero settles to the current sample exactly,
including values inside the shared helper's 0.01-sample deadband. Continuous
retargeting finishes the current fade before the latest pending target.

Doubling uses an 8 ms base, up to ±1.5 ms modulation, distinct upper/dry phases,
and 0.29/0.33 Hz stereo rates. Depth increases both modulation and delayed mix
from zero to 50%. This gradual blend is an explicit Ardor voicing adaptation,
not an EHX measurement. Histories stay warm; zero depth is exact direct audio.
Read movement is bounded to about 0.005005 samples/sample (under 8.7 cents),
including depth automation. Spread adds the selected delay after pitch/Attack;
the doubling branch contributes approximately 6.5–9.5 ms alongside direct audio.

The filter detector links channel powers rather than adding signed samples.
Fast/slow power envelopes are 2/30 ms, the sensitivity gate is −42..−66 dBFS,
contrast is 0.8..0.08, and refractory time is 15 ms. A retained fast-envelope
peak prevents low-note cycle and held-chord chatter. The peak releases only
after 100 ms mean input power falls below 70% of its reference, scaling with
that decrease. A time-only 500 ms release failed the held-chord test at maximum
sensitivity and was replaced without relaxing that gate. This is a playing-event
heuristic, separate from spectral Attack; dense close beats and real-pedal
trigger calibration still need listening review.

Filter AD runs finite attack (5 ms..3 s), then decay (20 ms..3 s), then returns
exactly to zero during sustain. Re-trigger starts from the current excursion.
Cutoff is `base * 2^(depth * AD)`, clamped to 40..20,000 Hz, with ±6-octave
depth. Base log frequency, Q and depth smooth with a 10 ms time constant; safe
positive filter coefficients interpolate every sample between 16-sample
updates. Two stereo TPT filter pairs share these controls while retaining
separate dry/generated states. LP/BP/HP mode changes fade for 10 ms; BP is
damping-normalized. The neutral LP endpoint can fade to exact open audio;
BP/HP never bypass. Dry Filter independently fades its own warm filter pair.
No automatic makeup gain, clipping, or output normalization is applied.

At a 1 kHz cutoff, measured LP gain at 100 Hz / 10 kHz is approximately
−0.00044 / −42.74 dB; HP is −40.03 / −0.00023 dB. BP gain at
100 Hz / 1 kHz / 10 kHz is −17.00 / 0 / −18.36 dB. BP's center remains
0 dB at Q=8; LP's center rises to +18.06 dB, as expected for that resonance.
Tests cover exact shortest AD timing, polarity/base return, unchanged filter
timing under volume Attack, anti-phase detection, 15 held-tone cases and two
held-chord cases with exactly one trigger, soft plucks and stronger/decaying
re-plucks, every voice's routing, endpoint impulses, queued fades, exact pan
edges, master mute, and Input Gain/Master application once.

Rapid parameter/mode/dry-routing/space automation produces identical samples
with 1/64/127/512-sample partitions when control timestamps match. Repeated
reset clears active transitions, filter state and every space history. Q=8
with inputs up to ±12 remains finite and drains without recovery. Nonfinite
source/filter/output results are muted and counted; affected filter state is
cleared. Prepared callback, control updates and reset retain their capacities.

Eight additional raw 48 kHz float WAVs cover source, organ, upward/downward
filter, doubling, Spread, processed dry routing, and automation. Output peaks
are 0.0467–0.0818, source peak 0.1155; files remain in the ignored artifact
directory. These are reproducible listening evidence, not a completed listening
sign-off or hardware comparison.

All **12 selected release suites pass** (68.48 s), including the five existing
POG3 suites, the new voice-stage suite, and pitch/harmonizer/catalog/scene/
scheduled-convolver regressions. The complete new voice-stage suite also passes
ASan/UBSan with leak detection enabled. The two new translation units compile
without `-Wall -Wextra -Wpedantic` diagnostics. Earlier M3/M4 sanitizer evidence
above remains separate; this increment did not rerun those entire sanitizer suites.

The final optimized static sound-path benchmark includes all voices, both warm
upper variants, active Attack/dry routing, Q=8, and 10 ms updates to filter
frequency/envelope/mode, pan, doubling and Spread, with Focus reversals:

| Callback samples | Median µs | p95 µs | p99 µs | p99.9 µs | Maximum µs | Budget µs |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 64 | 597.872 | 1071.241 | 1124.998 | 1189.387 | 1210.838 | 1333.333 |
| 128 | 1407.216 | 1660.164 | 1718.222 | 1784.302 | 1830.221 | 2666.667 |

These are 3,000/1,500 callbacks over four seconds after warm-up. Callback/control/
reset allocations are **zero**, scheduled-render deadline misses **zero**, and
maximum transforms remain 11/16. Those logical renderer counters do not measure
host wall-clock lateness or device xruns. A preliminary run before the final
output-overflow guard reached **1359.249 µs** for the full 64-sample path, above
its period. The final run also contains a **2000.758 µs** outlier in the separate
Attack/Warp/Focus bank workload. Preserve this variability as admission evidence;
the final static-path maximum alone does not establish dependable margin.

Requested preparation allocation is **2,233,936 bytes** (about **2.13 MiB**),
including the benchmark wrapper, owned DSP objects and allocations requested
during preparation; it is not a peak-RSS measurement. Asymmetric Spread histories
use 153,728 bytes, doubling histories 12,288 bytes (166,016 bytes combined), and
the remaining increase is owned stage/control state. Reset costs approximately
59–84 µs. The current static path exceeds the 2 MiB initial goal by **136,784
bytes** before freeze snapshots, and its observed worst callback cost exceeds
the plan's 25%-of-period isolated-effect target. Both targets remain unmet.
Further memory/CPU work and target-device combined-chain endurance are required
before public admission; no callback-period or unrelated DSP quality change is
authorized by these results.

## Expression controller and prepared processor (M6 DSP)

`Pog3Processor` owns the prepared sound path behind a lifecycle-owned state
object. `configure` validates native 48 kHz and numeric parameters, compiles
immutable endpoint arrays, allocates/plans off the callback, then replaces the
previous state and target defaults. Invalid parameters/sample rate/endpoints
leave the existing targets, audio history and cadence intact. Reconfiguration
and reset require exclusive lifecycle ownership; no live JSON publication or
snapshot editing occurs on the audio thread.

Key and index setters use the same registry/lock-free scalar targets. The first
sample reads them, then every 48 samples thereafter, independently of callback
boundaries. Audio-owned `base` records consumed saved controls; `sound` records
the resolved effective values. Neither expression evaluation nor smoothing
writes into targets. Individual scalar atomics retain the existing last-writer
model and do not promise an atomic whole-scene update. Reset retains current
targets and immutable endpoints, reseeds effective controls and smoothing,
clears every sound-path history, and restarts the control cadence.

The implemented modes are:

- **Off:** use base controls, smoothly restore generated gain and nominal Warp.
- **Volume:** interpolate normalized heel/toe gains after Reverse and scale only
  the generated bus after its filter, before Master. A 10 ms gain ramp reaches
  exact zero/unity endpoints; even ringing is muted at zero. Dry remains
  unchanged with Dry Attack, Dry Filter, Detune and Spread all enabled.
- **Crossfade:** resolve only the compiled union of continuous endpoint keys in
  normalized space. A missing endpoint retains the configured base captured at
  configuration time. Editing an owned base control remains saved while its
  snapshot keeps audio ownership; leaving Crossfade restores the latest base.
  Unowned sound controls continue following their targets. No second signal
  bank, preset reference, recursive expression configuration or JSON operation
  is involved.
- **Warp:** interpolate octave extent from scalar endpoints and use the bank's
  existing 10 ms semitone-domain slew. Dry/unison never changes pitch; Focus-off
  upper voices stay nominal, and Focus-on upper voices follow extent. Exiting
  Warp restores nominal intervals without clearing spectra/history.
- **Filter:** interpolate normalized/log-frequency position, replacing only
  effective base cutoff. The independent AD still sweeps around that base;
  cutoff bounds and existing coefficient smoothing remain in force.

Scalar/snapshot heel and toe values now return the exact stored endpoint rather
than subtracting and adding near endpoint values. The formatting helper reports
Volume gains as dB/Mute, Warp as 0–12 semitones of extent, Filter as Hz, and
unused scalar endpoints explicitly. It remains an off-callback UI helper.

At the M6 checkpoint, the registry retained all seven choices but the development
processor rejected Freeze + Gliss and Freeze + Volume in configuration and both
target setters. M7 below replaces that temporary rejection with audio behavior.
Nothing has been registered in the public factory/catalog. Key/index and
scene-like sequential publication are tested at the processor boundary, but
the actual physical/MIDI assignment, runtime scene dispatch and manager preset
round-trips cannot exercise `mod/pog3` yet. Those parts of the M6 integration
gate are explicitly deferred to M8, after complete freeze support.

Across the processed-dry Volume comparison, dry sample difference is **exactly
zero** and generated gain error is at most **1.87e−9**. Zero-gain filter tails
mute exactly; exiting Volume restores the warm reference wet stream exactly
after its ramp. The largest measured automation error step against the moving
reference is **0.002686** at the stated input levels. All **30** isolated Warp
interval cases (five voices × three extents × both Focus settings) are within
**0.000074 cents** in the settled synthetic-sine measurement. These are routing
checks, not a replacement for the broader M3 tuning/spur/chord evidence.

Filter tests confirm Reverse, expression-derived base, positive/negative sweep
and base return, bounded cutoff, and identical sweep timing when volume Attack
changes. Audio is bit-identical with 1/17/48/64/128/256-sample partitions under
matching 7-bit events, mode/Reverse/Focus changes and scene-like setters. Failed
reconfiguration preserves the old stream; repeated reset leaves no stale audio.

Seven raw float WAVs add source, Off reference, Volume, Crossfade, short/focused
Warp, and Filter expression with 7-bit ramps and a Reverse change. Source peak
is 0.1374, output peaks 0.0722–0.1078, with no normalization/limiting. Artifacts
remain under the ignored build directory; listening sign-off remains open.

All **13 selected release suites pass** (79.87 s). The full new expression
suite, the changed voice-stage suite and the parameter suite pass ASan/UBSan
with leak detection enabled (174.64 s total; expression suite 167.38 s).
The processor, parameter, voice-stage, expression-test and benchmark translation
units compile without `-Wall -Wextra -Wpedantic` diagnostics. Prior pitch/Attack
sanitizer evidence remains recorded separately rather than claimed as rerun.

The optimized prepared-processor workload keeps all voices/space/filter stages
active and cycles all five implemented expression modes, 7-bit positions,
Reverse, Focus and filter-mode/envelope changes:

| Callback samples | Median µs | p95 µs | p99 µs | p99.9 µs | Maximum µs | Budget µs |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 64 | 611.127 | 1069.051 | 1136.773 | 1202.798 | 1339.538 | 1333.333 |
| 128 | 1388.624 | 1653.416 | 1725.482 | 1781.511 | 1812.592 | 2666.667 |

Prepared processing, target updates and reset allocate **zero**. Scheduled
renderer deadlines remain intact, with at most 11/16 transforms per callback.
Reset costs approximately 58 µs. The 64-sample maximum exceeds its period by
**6.205 µs**; the separate Warp/Focus bank workload also reaches 1384.813 µs.
The isolated-effect 25%-of-period target, dependable callback margin and device/
combined-chain endurance remain unmet. This is a four-second host diagnostic
(3,000/1,500 callbacks), not hardware certification or a reason to hide earlier
timing outliers.

Requested preparation allocation is **2,239,758 bytes** (about **2.14 MiB**),
including processor/wrapper ownership, compiled scalar state and temporary JSON
construction/parsing allocations in this workload. It is not a retained-memory
or peak-RSS measurement. The sound-path-only preparation now requests 2,233,960
bytes, 24 bytes more than M5 for the new generated-gain slew/target. Existing
delay-history storage is unchanged. The initial 2 MiB memory goal remains
exceeded before held/gliss spectra; freeze must add an explicit memory inventory
and further admission/optimization work before public integration.

## Both freeze behaviors (M7 DSP)

`SpectralFreeze` owns bounded audio-thread state with no callback allocation.
After the existing Attack jobs, at primary-frame age 9, it compiles normalized
analytic carriers from each channel's primary, short and low representations.
Capture retains source frequency, magnitude including the current Attack gain,
phase, frame center and live track generation. The held sound is pre-transposition;
voice levels/pan, filters, Detune, Spread and Master remain live downstream.

The representation deliberately holds resolved tonal peaks at 20 Hz–Nyquist.
It does not preserve DC or an independent broadband/noise residual. This is an
explicit first-version voicing/fidelity limit, especially for noisy pick attacks
and dense unresolved material, rather than evidence of hardware equivalence.
All three representations are captured, although Focus-on held synthesis uses
primary/low carriers; short upper synthesis remains live. There are no extra
analysis or inverse transforms. The long renderer jobs and low crossover remain
unchanged, and live phase/history continues advancing underneath every hold.

Each long voice/channel has independent held phase accumulators for primary
and low carriers. A new oscillator uses its captured source phase plus the live
transposition offset when the original track generation still exists. Subsequent
frames integrate the output frequency, including gliss motion, rather than
reusing a fixed complex spectrum. Matching stereo carriers within one part per
million (or 0.1 mHz) share frequency and the transposition reference while
retaining separate captured magnitudes/phases; this prevents indefinite drift
between identical or unequal anti-phase inputs. Distinct stereo frequencies
remain independent. Aliasing rejection and the 200–300 Hz crossover follow the
live renderer. Held spectra join the same staged IFFT/OLA path.

Reverse precedes the heel/toe detector. Heel enters at q≤0.015 and leaves at
q≥0.035; toe latch enters at q≥0.985 and leaves at q≤0.965. Leaving heel requests
the latest compiled snapshot available at that control event, then promotes it
before the next primary renderer jobs. The snapshot cadence is 256 samples;
its channel frame centers retain their own analysis ages, including the wider
4096-sample low window. At startup/reset, a retained nonheel selection waits for
full history plus a second low frame and at least 4608 samples (96 ms) since
the first continuously audible partial. This also rejects incomplete onset
windows after prolonged startup silence; losing audible content restarts that
validity interval. A later deliberate heel-exit over actual silence captures a
silent hold.

Held/live mix, dry eligibility and held gain approach their targets by 256/960
per primary update: four hops (21.33 ms) for a full transition. Existing OLA and
the measured 48 ms primary wet delay shape the audible transition further.
Heel and mode exit release to warm live synthesis, then clear held/goal data.
A new heel-exit during release waits for that clearing before its queued capture.
Reset clears held phases, spectra, pending targets, identities and counters,
retains controls, and re-enters the first-valid-frame rule. Cut/re-enable through
the actual runtime remains an M8 integration test.

Freeze + Volume applies q exactly once to held contributions, including eligible
processed dry. Scalar Heel/Toe endpoints are ignored in both freeze modes.
Raw/live dry is unchanged; only Dry Attack enabled with normalized Attack
strictly above 0.10 permits the processed unison to freeze. Focus-off uppers
stay live at their nominal intervals; enabling Focus fades them into the
existing held spectrum without recapture. Switching between freeze modes
retains the capture and fades held gain; Freeze + Volume stops any active glide.

Freeze + Gliss proposes targets from linked positive partial-energy changes:
low carriers below 300 Hz and primary carriers above it avoid crossover double
counting. The fixed threshold is 15% of current partial power with a small
silence floor, 40 ms event coalescing, and a 2048-sample (42.67 ms) stabilization
wait before assigning a latest snapshot. This spectral-flux proposer is an
implementation adaptation; it is not the full harmonic-family onset scorer.
Dense beating, noisy material and real DI still need calibration/listening.

For each resolution/channel, strongest held oscillators greedily claim the
nearest available target using symmetric relative-frequency cost, with stable
tie order and one claim per target. Held slot identities never permute.
Missing carriers fade out; new ones occupy inactive/tail slots and fade in.
Capacity is 256 partials per representation/channel with bounded fallback and
a diagnostic counter. This deterministic assignment is not an optimal chord
voice-leading algorithm; complex reassignment remains a fidelity risk.
Frequencies and magnitudes interpolate linearly over `0.02 × 150^q` seconds;
changing q rescales remaining duration. Full toe pauses interpolation at its
current state and defers target assignment. Unlatching resumes toward eligible
pending live targets. Thus gliss moves carriers rather than crossfading two
fixed-pitch outputs.

### M7 verification and artifacts

- Fifteen held-sine cases (five voices × 82.4069/196/659.3 Hz) survive replacement
  live input. Worst settled pitch error is **0.005005 cents**, level error
  **0.000083 dB**, and unequal anti-phase error below **3.74e−8**.
- A −1-octave glide from 196 to 293.6648 Hz passes through **120.752 Hz**, then
  reaches **146.833 Hz**. The intermediate carrier exceeds either endpoint's
  measured component by **43.23 dB**. Additional tests latch during movement,
  reject continued pitch creep, resume after unlatching, and retain distinct
  left/right pitches and magnitudes.
- Freeze Volume produces **0.0250002/0.0750006** amplitude at q=.25/.75 for
  generated and eligible processed-dry paths (0.1-amplitude source). Scalar
  endpoint settings do not alter this gain; switching modes preserves capture.
- Startup silence, later silent capture, strict Attack .099/.100/.101 eligibility,
  Dry Attack and Focus reversals, heel/toe jitter, Reverse, repeated reset,
  and dense independent stereo input up to ±6 remain healthy. Timestamp-matched
  controls give bit-identical output across 1/17/48/64/128/256-sample partitions.
  Staged renderer deadline checks stay intact; they are separate from CPU timing.
  A stronger startup test exposed a premature transient capture after prolonged
  silence. The completed audible-history interval fixes it; the frozen −1 voice
  now retains the expected 98 Hz from the first 196 Hz note.
- Both **60-second** holds continue after input becomes silence. The 41.20345 Hz
  sine changes by **6.43e−7 dB**; chord partials at 41.20345/65.4064/98/164.8138 Hz
  change by at most **0.000202 dB**. Peaks are 0.050003 and 0.139643; unequal
  anti-phase errors stay below 3.74e−8. Both retain one capture, no retargets,
  no nonfinite output, and drain to exact silence after mode exit.

Seven raw 48 kHz stereo float WAVs are generated under the ignored
`build-ci/pog3-artifacts`: `freeze-source`, `freeze-gliss-focused`,
`freeze-gliss-short`, `freeze-volume-live-dry`, `freeze-volume-held-dry`,
`freeze-toe-latch`, and `freeze-off`. Source peak/RMS are 0.072651/0.027838;
output peaks are 0.042554–0.057604. No normalization, limiting, or compensation
is applied. These are synthetic chord performances; listening sign-off remains
open. Reproduce with:

```sh
cmake --build build-ci -j4 --target pedal-pog3-freeze-quality pedal-pog3-bench
build-ci/pedal-pog3-freeze-quality
build-ci/pedal-pog3-freeze-quality --render build-ci/pog3-artifacts
build-ci/pedal-pog3-bench --csv build-ci/pog3-artifacts/freeze-processor-benchmark.csv
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 build-pog3-sanitize/pedal-pog3-freeze-quality --quick
```

`--quick` runs all functional/dense/reset gates and omits only the two long holds;
`--render` generates artifacts without rerunning the quality suite. The complete
release CTest freeze suite includes endurance by default.

The host storage inventory added by M7 is explicit:

| Added storage | Requested / owned bytes |
| --- | ---: |
| Five stereo × three-resolution snapshot sets, 256 slots each | 430,560 |
| Freeze controller bookkeeping beyond those arrays | 360 |
| Twelve long renderer phase stores, 512 slots × 32 bytes | 196,608 |
| Renderer vector headers and bank ownership/control metadata | 408 |
| Total increment over M6 | 627,936 |

`FrozenPartial` is 56 bytes, `FrozenBand` 14,352 bytes and `SpectralFreeze`
430,920 bytes on this host. The controller is lifecycle-owned heap storage;
renderer vectors allocate during prepare, and the short renderers allocate no
held phase stores. The existing 166,016-byte space history and FFT/analysis/
OLA/live track/Attack storage remain part of the M6 baseline, without duplicate
FFT plans for freeze. This inventory concerns requested C++ storage on the host,
not allocator overhead, resident pages or target ABI sizes.

A release `-fstack-usage` compile reports 2192 bytes for the target-assignment
function itself (including its fixed index order); reset, capture and update
report 16/112/80 bytes. These exclude nested callees and do not constitute a
whole-callback stack bound. Large snapshot copies do not become automatic
snapshot-sized arrays in this optimized host build.

All **14 selected release suites pass** on the rebuilt final code (149.26 s),
including both 60-second holds. The freeze suite's complete functional/dense/
reset checks (`--quick`) and the complete expression suite pass ASan/UBSan with
leak detection enabled. Only the two long holds are omitted from the sanitizer
run; their release results are recorded above. Earlier M3/M4/M5 sanitizer
evidence remains separate. The changed controller, bank, processor, freeze test
and benchmark translation units compile without `-Wall -Wextra -Wpedantic`
diagnostics. `git diff --check` passes.

The final optimized host benchmark runs after the validation jobs finish. Its
expression workload cycles all seven modes. A separate full-path gliss workload
uses changing independent stereo chords, all voices, processed dry, maximum
space/Q, toe latch and heel release/recapture. It asserts at least two captures
and two target assignments in both warm-up and measurement. Source generation
occurs before timing. Each row covers four seconds, 3000/1500 callbacks:

| Workload | Samples | Median µs | p95 µs | p99 µs | p99.9 µs | Maximum µs | Period µs |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| All expression modes | 64 | 734.806 | 1127.360 | 1200.467 | 1394.439 | 1528.491 | 1333.333 |
| Freeze/gliss capture and assignment | 64 | 732.361 | 1145.333 | 1196.451 | 1245.589 | 1284.949 | 1333.333 |
| All expression modes | 128 | 1550.917 | 1825.830 | 1940.764 | 2606.969 | 3146.738 | 2666.667 |
| Freeze/gliss capture and assignment | 128 | 1707.161 | 1852.160 | 1900.698 | 1949.402 | 1973.224 | 2666.667 |

Processing, target updates, capture/assignment and reset allocate **zero** in
the scoped probe. Full-path scheduled renderer deadlines remain intact, with
at most 11/16 transforms per callback. Reset costs approximately 85–107 µs.
These logical schedule counters do not measure wall-clock xruns. The expression
maxima exceed the 64/128-sample periods by **195.158/480.071 µs**; the pitch-bank
64-sample workload also reaches 1520.313 µs. The gliss workload's lower maxima
do not cancel those outliers or satisfy the isolated-effect 25%-of-period goal.
Dependable callback margin and target/combined-chain admission remain unmet.

Requested preparation allocation is **2,867,694 bytes** (about **2.735 MiB**)
for the expression workload, **2,866,208** for the dedicated gliss workload,
**2,861,896** for the static sound path, and **2,693,528** for the pitch-bank
wrapper. The expression total includes temporary configuration/JSON allocations;
it is not retained memory or peak RSS. Its increase over M6 is exactly the
627,936-byte inventory above. The static path exceeds the initial 2 MiB goal
by **764,744 bytes** before allocator overhead. The CSV and renders remain
ignored build artifacts. Further memory/CPU optimization is required before
public integration; these host diagnostics do not certify hardware feasibility.

## Admission optimization after M7

This increment reduces prepared storage and callback work while preserving
the existing resolutions, 256-partial capacities, excitation epochs, 24-tap
interpolation, staged deadlines and latency. The 33-parameter contract and
all seven expression modes remain unchanged.

### Storage and ownership

| Change | Host requested storage saved, bytes |
| --- | ---: |
| Compact frozen partials and frequency/magnitude-only previous/goal sets | 221,280 |
| Bounded 16-bit region indices and compact region/candidate layout | 112,464 |
| One shared immutable interpolation table for all three pitch plans | 98,432 |
| Live phase layout with separate byte-sized age counters | 57,344 |
| Consume each renderer's existing spectrum as its inverse-FFT workspace | 229,376 |
| Renderer overlap-add rings sized to N+H rather than 2N | 100,352 |
| Additional synthesis offset metadata | −128 |
| Storage-only checkpoint net saving | **819,120** |
| Fixed frequency indexes for stereo/canonical Attack matching | −6,160 |
| Final net saving against M7 | **812,960** |

`FrozenPartial` is now 40 bytes, and the controller is 209,640 bytes on this
host. Captured phase seeds were already computed as floats; continuous held
oscillator phases remain doubles. Track generations, live phase offsets and
alignment also retain their original precision. The previous/goal sets need
only frequencies/magnitudes; latest/requested/held sets retain capture metadata.
Indices narrow only where the existing N≤32768 and 256-slot bounds permit it.

The renderer owns and consumes its mutable IFFT spectrum; analysis spectra
remain independent. The general synthesis API retains its default internal
scratch and 2N ring. External scratch and maximum staging offsets are explicit
prepare options. Compact synthesis checks the offset before work and validates
the entire inverse frame and pending sums before committing OLA. Tests compare
general and compact rings bit-for-bit across repeated wrap, variable 0/H offsets,
reset, malformed frames and inverse overflow.

The expression workload now requests **2,054,734 bytes (1.960 MiB)**, a
**28.35% reduction** from M7 and **42,418 bytes below** the initial 2 MiB goal.
Static sound requests 2,048,936 bytes, dedicated gliss 2,053,248 and the bank
wrapper 1,880,568. These scoped construction totals include temporary
configuration allocations; they do not measure retained memory, peak RSS,
allocator overhead or target ABI sizes. Existing 166,016-byte space histories
and all three analysis resolutions remain present.

### Callback work

Fractional interpolation uses a direct interior loop when every tap lands
strictly between DC and Nyquist. Edge/reflection handling retains its original
path. Each bin sees the original addition order; the host compiler vectorizes
the interior complex arithmetic without changing the kernel or support.

Attack builds fixed, sorted frequency indexes for right-channel regions and
primary/low canonical histories. Lower bounds skip candidates outside a
conservatively rounded match window. The original distance predicate, stale
history eligibility and lowest-original-slot tie rule decide matches. Histories
refresh after their own updates and reset clears their index counts. There is
no callback allocation or reduction of retained partial capacity.

A sampling profile was unavailable on this host; an instrumented `gprof` run
identified stereo/canonical Attack searches as a useful optimization target.
Its percentages are diagnostic, not callback timing. A separate packed-real-FFT
prototype passed numerical checks but regressed measured host CPU cost; it was
removed. The shared FFT implementation and transform mathematics are unchanged
by this increment.

### Verification

All **14 selected release suites pass** on the final indexed code (138.45 s),
including both 60-second freeze holds and existing pitch/harmonizer/catalog/
scene/convolver regressions. All **53 raw WAV artifacts are byte-identical**
to the M7 baseline, covering the reference/foundation, pitch/Warp, Attack,
voice stages, expression and freeze renders. No normalization or limiter was
introduced. The changed source/test/benchmark units compile without
`-Wall -Wextra -Wpedantic` diagnostics; `git diff --check` passes.

The final full foundation, pitch, Attack and expression suites also pass
ASan/UBSan with leak detection (365.81 s combined wall time). Final freeze
functional/dense/reset checks pass with the same sanitizers and leak detection;
`--quick` omits only the two long holds already exercised in release. The
storage-only checkpoint independently passed those four suites plus functional
freeze checks. Reproduce the final gates and timing with:

```sh
ctest --test-dir build-ci --output-on-failure -R '^pedal-(pog3-.*|pitch-effect-quality|harmonizer-quality|daisy-fx-catalog-smoke|manager-effect-catalog-smoke|scene-plan-smoke|scheduled-convolver-smoke)$'
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build-pog3-sanitize -j3 --output-on-failure -R '^pedal-pog3-(quality|pitch-quality|attack-quality|expression-quality)$'
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 build-pog3-sanitize/pedal-pog3-freeze-quality --quick
build-ci/pedal-pog3-bench --csv build-ci/pog3-artifacts/admission-processor-benchmark.csv
```

### Final host timing

The final release benchmark ran after all POG3 validation jobs finished, with
the same four-second sources, 3000/1500 timed callbacks and precomputed input
as M7. The ignored `admission-processor-benchmark.csv` contains every workload;
the complete expression and dedicated gliss rows are:

| Workload | Samples | Median µs | p95 µs | p99 µs | p99.9 µs | Maximum µs | Period µs |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| All expression modes | 64 | 655.996 | 1066.203 | 1127.930 | 1190.525 | 1219.011 | 1333.333 |
| Freeze/gliss capture and assignment | 64 | 641.558 | 1094.938 | 1147.990 | 1179.767 | 1217.842 | 1333.333 |
| All expression modes | 128 | 1318.653 | 1619.748 | 1685.918 | 1782.014 | 1850.053 | 2666.667 |
| Freeze/gliss capture and assignment | 128 | 1492.269 | 1675.283 | 1723.497 | 1794.069 | 1961.423 | 2666.667 |

All-mode median cost is **10.7%/15.0% lower** at 64/128 samples than the recorded
M7 host run. This is a comparison of observed runs, not a controlled target
performance guarantee. Prepared processing/control changes/reset still allocate
**zero**, and all scheduled renderer deadlines pass, with at most 11/16
transforms in full-path 64/128-sample callbacks. Full processor reset costs
63.790–89.090 µs in this run.

The 64-sample static path still peaks at **1354.506 µs**, and the Attack bank
at **1386.892 µs**, exceeding its **1333.333 µs** period. The all-mode/gliss
rows' lower maxima do not establish dependable callback margin or meet the
isolated-effect **25%-of-period** goal (333.333/666.667 µs). An earlier indexed
run also recorded all-mode maxima of **3439.399/6276.186 µs**, and gliss reached
4757.814 µs at 128 samples. Those preliminary outliers remain in the ignored
`indexed-processor-benchmark.csv`; the final run does not erase them. Interior-
only and storage-only checkpoint CSVs also remain available. Host timing
cannot establish target/device or combined NAM/IR chain admission.

## Further CPU refinement

This increment removes redundant work from the same prepared engine. It keeps
all FFT sizes/hops, renderer jobs, latency, partial capacities, capture metadata,
Attack histories, 24-tap interpolation support and double phase accumulation.
No global FFT or callback-period change is included.

### Peak-only phase evaluation

Pitch frames retain the previous Cartesian complex bins instead of separate
phase/magnitude arrays. A candidate peak computes the previous bin's `hypot`
and `atan2` only when it needs that history. Current-bin magnitudes, peak
selection, the previous-magnitude threshold, float phase subtraction and the
frequency estimator retain their original expressions. Keeping an owned copy
also ensures the next analysis FFT cannot overwrite the previous coefficients.
Invalid frames still invalidate previous history; reset clears it.

The complex history uses the same per-bin sample storage as the two old float
arrays. Removing one vector header per frame saves another **144 host bytes**:
all-mode preparation now requests **2,054,590 bytes (1.960 MiB)**, dedicated
gliss 2,053,104, static sound 2,048,792 and the bank wrapper 1,880,424. All-mode
requested allocation is **42,562 bytes below 2 MiB**, and **813,104 bytes below
M7**. These are scoped construction counters, not retained memory, peak RSS or
target ABI measurements.

### Fully held live histories

When a prepared held renderer has exactly `heldMix == 1`, the live spectrum
would be multiplied by zero. It now skips the live rotation, interpolation,
scattering and low-band lobe reconstruction at that endpoint. Live generations,
phase offsets, ratios, ages, low carriers and cross-resolution alignment still
advance with the original update order. Held oscillator phases, synthesis IFFTs
and OLA continue normally. The entire live contribution still runs during
capture/release fades and at every intermediate mix.

The new `--warm-live` quality gate compares a renderer that freezes and releases
with an independent renderer that remains live throughout. It uses both
1024/2048 renderer sizes, 4096 low analysis, unison and −24/+7/+24 intervals,
and a new note played during the hold. Before the hold and after staged OLA
drains on release, output must match **bit-for-bit**, including signed zeros;
an energy check requires audible resumed output. This verifies warm state
without exposing or comparing private phase arrays. The gate is included in
both normal and `--quick` freeze suites.

### Forward interpolation traversal

The same 24 kernel weights are stored in increasing destination order. Interior
scattering traverses contiguous destinations forward rather than backward.
Each source bin still contributes once to a given interior destination, with
the original source-bin/region addition order. The reflection/endpoint path
retains its original traversal and reverses only the weight lookup, preserving
the order when multiple reflected taps meet one bin. The host GCC optimization
report now confirms **16-byte** loop vectors, versus the previous **8-byte**
vectors. This uses portable C++ and does not require architecture intrinsics
or relaxed floating-point compiler flags.

A standalone explicit-component FFT prototype was also checked against the
existing FFT at N=32/1024/2048/4096/32768, forward and inverse. It produced
identical normal finite results but ran substantially slower in both ordinary
and forced out-of-line microbenchmarks, so it was not added to the repository.
The existing shared FFT code remains untouched.

All **14 selected release suites pass** on the final code (143.80 s), including
the new warm-release gate and both 60-second holds. All **53 raw WAVs remain
byte-identical to M7**, covering pitch/Warp, Attack, voice stages, expression
and freeze. The changed bank and freeze-test translation units compile without
`-Wall -Wextra -Wpedantic` diagnostics, and `git diff --check` passes. The final
full foundation, pitch, Attack and expression suites also pass ASan/UBSan with
leak detection (396.41 s combined wall time). The complete functional/dense/
reset/warm-release freeze suite passes under the same sanitizers; `--quick`
omits only the two long holds already exercised in release.

The benchmark CSV additionally records the maximum callback's original index,
its end timestamp modulo the 512-sample low hop and its actual transform count.
It retains every percentile and the overall maximum transform count. This
bookkeeping runs after the callback timer stops and allocates nothing. Timestamp
diagnostics help locate repeatable bursts; a maximum can still include host
descheduling and is not a self-contained attribution of CPU cost. Reproduce
the new targeted state gate and final timing with:

```sh
build-ci/pedal-pog3-freeze-quality --warm-live
build-ci/pedal-pog3-bench --csv build-ci/pog3-artifacts/refined-processor-benchmark.csv
```

The final release timing run followed completion of all validation jobs. The
ignored `refined-processor-benchmark.csv` retains all workloads and new timeline
columns; the complete expression and dedicated gliss rows are:

| Workload | Samples | Median µs | p95 µs | p99 µs | p99.9 µs | Maximum µs | Period µs |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| All expression modes | 64 | 634.931 | 1014.233 | 1075.737 | 1267.788 | 1574.489 | 1333.333 |
| Freeze/gliss capture and assignment | 64 | 574.209 | 1029.670 | 1079.794 | 1135.260 | 1600.727 | 1333.333 |
| All expression modes | 128 | 1256.470 | 1518.803 | 1603.403 | 1673.535 | 1759.000 | 2666.667 |
| Freeze/gliss capture and assignment | 128 | 1378.007 | 1557.899 | 1604.260 | 1670.554 | 1855.785 | 2666.667 |

Against the preceding admission checkpoint, observed all-mode medians are
**3.2%/4.7% lower** at 64/128 samples, and dedicated gliss medians **10.5%/7.7%
lower**. All-mode p99 is **4.6%/4.9% lower**. These compare host runs, not a
controlled hardware speedup guarantee. Some other workload medians increased
between runs, and large outliers remain. The scoped processing/control/reset
allocation count is still **zero** in all rows. Full-path scheduled renderer
deadlines remain intact, with at most 11/16 transforms per 64/128-sample callback;
full-processor reset takes 64.458–83.196 µs in this run.

The 64-sample expression/gliss maxima exceed the 1333.333 µs callback period.
The Attack bank also reaches **1369.163 µs** at 64 samples, and the Warp/Focus
bank reaches **2911.778 µs** at 128 samples, exceeding its 2666.667 µs period.
The foundation comparison harness reaches 4327.503 µs at 64 samples; this is
retained as a host diagnostic, not a production fallback. Storage goals and
lower medians do not satisfy dependable callback margin, the 25%-of-period
isolated-effect goal or target/combined-chain feasibility.

Several bank/static/expression maxima occur in callbacks ending at a 512-sample
boundary, with 11/16 completed transforms. Those callbacks include coincident
analysis events. The 64-sample gliss maximum instead ends at remainder 64 with
five transforms. This motivates profiling/scheduling the coincident analysis
work, while retaining the possibility of unrelated host descheduling. The
phase-only, held-endpoint and forward-interpolation checkpoint CSVs remain
ignored artifacts; their outliers are not replaced by this final table.

The benchmark's existing malloc/free-backed replacement allocation operators
produce GCC's `-Wmismatched-new-delete` diagnostic at `-O3 -Wall`; the same
diagnostic is reproduced from the preceding commit's benchmark. After excluding
that diagnosed allocation-probe warning, the changed benchmark compiles without
other `-Wall -Wextra -Wpedantic` diagnostics. Production allocation behavior is
unchanged, and the sanitizer suites above do not use this benchmark probe.

## CodeQL and dependency checkpoint

The five `cpp/integer-multiplication-cast-to-long` findings in the Attack,
voice-stage and freeze quality tools are corrected by converting an operand to
`double` before multiplying. The product now uses the accumulator's precision;
casting a float product afterward would retain its overflow and rounding.
The owning Attack/voice-stage tests and freeze `--warm-live` gate pass. A bounded
standalone check also confirms that a finite large float squared overflows in
float while the promoted double product remains finite.

The sixteen `cpp/path-injection` findings were independently traced from the
test CLI arguments to their file sinks. `--render` deliberately accepts an
operator-selected directory with internally generated WAV names; `--csv`
deliberately accepts an operator-selected file. These are local test executables,
not network or privileged services, and promise no destination sandbox. Relative
and absolute render destinations both pass a focused legitimate-use check.
There is no violated trust boundary to repair. The findings are recorded as
false positives with that reason in GitHub, preserving these diagnostic options;
the comments in the code explain the contract rather than hide a tainted path.

The Manager CI audit separately found `source-map-js` 1.2.1 affected by
[GHSA-68fv-2mgg-jv7q](https://github.com/advisories/GHSA-68fv-2mgg-jv7q).
Only its lockfile version, resolved URL and integrity change to patched 1.2.2.
`npm ci`, `npm audit --audit-level=high` (zero vulnerabilities), type checking,
all 739 tests in 60 files and the production Manager build pass. The existing
bundle-size advisory remains; no device UI bundle is regenerated.
The patched consumer also passes a bounded check of oversized/malformed indexed
offsets and nested-offset overflow. A five-million-line offset beyond a tiny
generated source converts within a five-second process limit, and ordinary
mapping lookup/source reconstruction retains the original source text.

All CI checks pass on CI-correction commit `9987453b`. The first C++ CodeQL
attempt failed before compilation because GitLab rejected an Eigen dependency
clone under load; retrying only the failed job completed successfully.
[The C++ analysis](https://github.com/balazsbencs/ardor/actions/runs/37475930333)
marks alerts 116/117/120/121/128 **fixed**, and the PR alert readback contains
**zero open alerts**. Alerts 108–115, 118–119 and 122–127 are separately recorded
as reviewed false positives. Subsequent DSP commits still require their own
fresh CI checks; this result identifies the exact validated CI correction.

## One-sample stereo analysis staging

The bank now captures both channel windows at their original 128/256/512-hop
boundaries, completes the left analysis immediately and transforms/interprets
the right window on the following sample. The optional `SpectralAnalysis`
preparation mode reuses its existing complex workspace: no second window buffer
or FFT mathematics change. Pending work is not exposed by `spectrum()`, and a
reset discards it. Deferred hops smaller than two are rejected before changing
the prepared state; the ordinary one-sample-hop stream remains supported.

Only left boundaries reset job ages and capture Warp. Long/short Attack require
matching completed stereo frame counts. Low Attack waits for the right frame,
retaining its original input-end timestamp and Attack-seconds snapshot; it still
runs before long Attack on that next sample. This also preserves direct bank
automation that changes Attack between the boundary and completion. Freeze
remains at long age 9. Final long jobs run at ages
17/38/81/102/129/145/161/177/193/209/225/241 and short jobs at 65/81/97/113.
Four long inverses run in the first half-hop with the right analyses and stereo
interpretation; eight run in the second half. Short inverses follow their first
64-sample interpretation interval. Original staging offsets preserve N+H
output latency. All old renderer jobs
finish before either channel's workspace is overwritten at the next boundary.

The foundation suite compares every real/imaginary FFT bit against an independent
immediate analysis for N=32/1024/2048/4096 and H=2/N÷8/N÷2. It exercises startup,
multiple history wraps, nonfinite input, drain, pending-spectrum visibility,
pending reset and rejected preparation. A separate comparison against the
preceding compiled bank gives bit-identical output across 65,536 samples with
unequal stereo, all voices, all seven freeze selections, Focus/Warp, Attack
changes on both sides of each low boundary and resets before/after deferred
completion. All **53 raw WAV renders remain byte-identical** to M7.

All **14 selected release suites pass** on the final immutable-table build
(134.65 s), including freeze endurance and existing pitch/harmonizer/catalog/
scene/convolver regressions. Changed production and foundation-test units
compile without `-Wall -Wextra -Wpedantic` diagnostics. Full foundation, pitch,
Attack and expression suites pass ASan/UBSan with leak detection and UB halt
enabled on the final schedule (358.86 s at `-j3`). The final functional/dense/
reset/warm-release freeze suite also passes under the same sanitizers. Completed
logs contain no sanitizer diagnostics.
Freeze `--quick` omits only the two long holds already exercised in release.
This scheduling change targets bursts; it does not remove any required
transform or establish target-device CPU admission.

The benchmark additionally checks the measured stream's health and staged
deadline state **before** reset. Its existing post-reset check alone could hide
a failure because reset clears those counters. The check runs after callback
timing stops; reset timing and allocation accounting retain their existing
boundaries. A real finite, anti-phase oversized input through the pitch-bank
workload reproduces the old probe accepting a failed stream after reset; the
new probe rejects it explicitly before reset. Ordinary measured workloads still
pass their health and allocation guards.

Simply deferring right analysis while keeping the original inverse job dates
reduced completed transform maxima to 8/14 per 64/128 callback, but **regressed
tail timing**. Against a nearby old-bank run with the same corrected probe,
64-sample expression/gliss p99 rose from 1067.232/1075.858 µs to
1222.447/1269.960 µs. The first inverse redistribution recovered much of the
64-sample regression but retained the 128-sample regression; neither schedule
is the retained implementation. This is why transform count alone cannot
certify scheduling quality. Their unedited CSVs remain ignored artifacts.

The final schedule instead moves two long inverses from the first half-hop to
the second half and moves short inverses beyond age 64. Input windows, controls,
phase precision, per-frame addition order and output window starts remain
unchanged. It trades callback distribution rather than reducing total FFT work.
The due table is shared immutable static data (96 host bytes): assembly review
found the automatic table being copied to stack every sample. Its final object
is read-only and has no runtime initialization guard. Prepared allocation
increases only by 64 bytes of analysis/pending-control state; no second FFT
window is allocated.

After every validation process completed, two old/new pairs ran sequentially
with the same corrected probe and inputs. The control links the preceding bank
(`9987453b`, production DSP identical to `ed1b0414`); the final rows use the
retained static-table schedule. CSVs `admission-control-1/2.csv` and
`admission-balanced-1/2.csv` retain all fourteen workload rows and every outlier.
Times below are microseconds; periods are 1333.333/2666.667 µs at 64/128 samples.

| Run | Workload | Samples | Median | p95 | p99 | p99.9 | Maximum |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Control 1 | All modes | 64 | 585.632 | 1013.370 | 1059.846 | 1106.608 | 1146.308 |
| Control 1 | Gliss | 64 | 574.880 | 1025.333 | 1075.860 | 1107.961 | 1144.775 |
| Control 1 | All modes | 128 | 1262.539 | 1518.957 | 1593.917 | 1640.322 | 1719.617 |
| Control 1 | Gliss | 128 | 1379.394 | 1553.955 | 1590.712 | 1616.823 | 1620.353 |
| Final 1 | All modes | 64 | 643.867 | 975.068 | 1047.761 | 1099.479 | 1120.333 |
| Final 1 | Gliss | 64 | 640.496 | 1036.639 | 1088.932 | 1146.723 | 1266.427 |
| Final 1 | All modes | 128 | 1302.249 | 1470.281 | 1534.561 | 1570.699 | 1599.021 |
| Final 1 | Gliss | 128 | 1374.931 | 1506.627 | 1541.967 | 1596.084 | 1693.976 |
| Control 2 | All modes | 64 | 685.653 | 1020.685 | 1076.937 | 1252.261 | 1738.591 |
| Control 2 | Gliss | 64 | 709.987 | 1042.437 | 1107.527 | 1273.274 | 1329.195 |
| Control 2 | All modes | 128 | 1277.092 | 1548.078 | 1685.875 | 2491.482 | 4914.683 |
| Control 2 | Gliss | 128 | 1381.471 | 1563.971 | 1631.500 | 1687.056 | 1705.874 |
| Final 2 | All modes | 64 | 642.875 | 979.873 | 1049.320 | 1125.952 | 1179.392 |
| Final 2 | Gliss | 64 | 651.968 | 1038.968 | 1093.870 | 1184.396 | 1392.891 |
| Final 2 | All modes | 128 | 1309.153 | 1490.656 | 1596.879 | 1776.364 | 1788.923 |
| Final 2 | Gliss | 128 | 1385.979 | 1529.404 | 1618.696 | 1722.957 | 1769.730 |

Both pairs show lower 128-sample expression/gliss p95 and p99 with the retained
schedule; 64-sample expression p99 also falls in both pairs. The 64-sample gliss
comparison is mixed: p99 rises about 1.2% in the first pair and falls about 1.2%
in the second. Several medians rise because work is distributed differently;
these measurements do **not** establish lower average CPU demand or a guaranteed
speedup. The complete transforms still reach **11/16 per 64/128 callback**, as
before, with a different mix of FFT sizes and interpretation work. The rejected
8/14-transform schedule had worse tails despite its lower count.

Prepared all-mode allocation is **2,054,654 bytes (1.960 MiB)**, only 64 bytes
above the preceding implementation and **42,498 bytes below** the initial 2 MiB
requested-allocation goal. Static sound and dedicated gliss request
2,048,856/2,053,168 bytes; the bank requests 1,880,488. All rows retain **zero
processing/control/reset allocations**; all full DSP workloads pass the new
pre-reset health and renderer-deadline guard. These figures exclude the
96-byte read-only due table
and remain host allocation counters, not target RSS/ABI admission.

The final gliss maximum still reaches **1392.891 µs** at 64 samples, exceeding
the 1333.333 µs period. Other workloads retain host outliers, including the
control's 4914.683 µs expression maximum at 128 samples. Do not discard these
outliers or treat either pair as a target-device certificate. The 25%-of-period
CPU goal and dependable target/combined-chain margin remain unmet.

Reproduce the retained implementation's gates and timing independently:

```sh
ctest --test-dir build-ci --output-on-failure -R '^pedal-(pog3-.*|pitch-effect-quality|harmonizer-quality|daisy-fx-catalog-smoke|manager-effect-catalog-smoke|scene-plan-smoke|scheduled-convolver-smoke)$'
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build-pog3-sanitize -j3 --output-on-failure -R '^pedal-pog3-(quality|pitch-quality|attack-quality|expression-quality)$'
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 build-pog3-sanitize/pedal-pog3-freeze-quality --quick
build-ci/pedal-pog3-bench --csv build-ci/pog3-artifacts/admission-balanced.csv
```

## Average-demand profiling and Attack rounding refinement

The preceding schedule at **c06fa3ae** passes every GitHub CI check, including
all three CodeQL languages, the aggregate CodeQL check, C++ engine/UI, Manager
app/daemon, documentation and dependency review. The PR merge ref has **zero
open CodeQL alerts**. This verifies the scheduling follow-up separately from
the earlier CodeQL/dependency checkpoint.

### Profile and average-time diagnostics

Linux `perf` and Valgrind are unavailable on this host. A separate `-O3 -pg`
build instruments the POG3 sources and shared FFT without changing production
compiler flags or repository build configuration. It repeats the full
64-sample expression workload four times, including each warm-up. This is
**diagnostic profiling, not an admission benchmark**: instrumentation, inlining,
library attribution and 10 ms sample resolution affect the result.

Its leading self-time samples are FFT 31.60%, live renderer 14.73%, pitch-frame
interpretation 11.29%, Attack update 9.58%, Attack grouping 4.72%, synthesis
outside the FFT 4.36%, Attack family ownership 4.22% and held renderer 3.72%.
These point to remaining transform/render/interpretation demand rather than
another scheduling-only change. Profile inputs/output are retained locally in
`/tmp/pog3-cpu-profile/profile-bench.cpp`, `expression-profile.csv` and
`expression-gprof.txt`; instrumented callback times are not used below.

The ordinary benchmark appends **`mean_us`** after its existing CSV columns.
It averages every measured callback's wall time before sorting, after the
allocation/health checks and outside all callback timers. No outlier is dropped.
Existing fields retain their names/order. The mean helps distinguish reduced
overall wall-time demand from shifted callback percentiles; it still includes
host preemption and is not a direct CPU-cycle or target-device measurement.
All old/new comparisons use this same new probe.

### Bounded nearest-harmonic calculation

Attack family scoring/ownership previously called `std::round(float)` for
all candidate pairs, including ratios that cannot match supported harmonics.
Only rounded integers **1 through 8** can satisfy the existing predicate.
Reject ratios outside **[0.5, 8.5)** before integer conversion, then truncate
and increment exactly when the fractional part is at least 0.5. This retains
half ties away from zero and the original division, strict cents tolerance,
scoring, candidate order and family tie rules.

For whole parts 1 through 8, subtracting the whole part is exact by the
binary floating-point subtraction bound; for whole part zero it subtracts
zero. Integer conversion stays bounded. Avoid `int(ratio + .5f)`, whose
addition can round a just-below-half value up. No family capacity, timestamps,
excitation histories, FFT behavior, prepared storage or callback allocation
changes. The optimized Attack object has no `roundf` relocation, while the
control has two call sites.

A temporary comparison against the original calculation passes **2,043,230**
cases: two million deterministic frequency/fundamental pairs plus next-float
sweeps around half ties, strict harmonic tolerance boundaries, integer
harmonics and the 40 Hz eligibility boundary. A separately linked old/new bank
comparison also preserves every output bit across **65,536 samples × six
voices × stereo**, including arbitrary boundary Attack/Warp controls, all
expression selections, unequal stereo and resets around deferred analysis.
These helpers are retained under `/tmp/pog3-cpu-profile`; they do not add
production instrumentation or duplicate implementation unit tests.

All **eight POG3 release suites pass** (109.13 s), including both 60-second
freeze holds. All **21 affected Attack/expression/freeze raw WAVs** remain
byte-identical to the M7 baseline. The other 32 artifacts were not re-rendered for this increment; their preceding
schedule comparison remains recorded above. The changed Attack source compiles
cleanly with `-O3 -Wall -Wextra -Wpedantic`.

The full Attack and expression suites pass **ASan/UBSan with leak detection**
(256.93 s combined wall time at `-j2`). The freeze functional/dense/reset/
warm-release suite also passes with the same sanitizer settings; `--quick`
omits only the two long holds already exercised in the release suite. There
are no sanitizer diagnostics. This increment changes only Attack arithmetic
and the offline benchmark; shared FFT and other effects are untouched.

### Ordinary release timing and decision

After every validation job finished, run all 14 workload/callback combinations
in each invocation, sequentially in **control → candidate → candidate → control**
order. Both binaries use identical `-O3 -DNDEBUG` compilation, headers, corrected
health/allocation probe and archive order. The candidate binary is byte-identical
to the ordinary CMake benchmark. The control links a copy of c06fa3ae's library;
there is no profiling instrumentation in either timing binary.

All 56 measured rows pass the pre-reset health/deadline checks and report
**zero processing/control/reset allocations**. Requested preparation storage
and maximum transform counts match between control and candidate in every row:
all-mode **2,054,654 bytes**, and **11/16** maximum transforms at 64/128 samples.

Main full-path results, in microseconds, with **every outlier retained**:

| Pair | Version | Workload | Frames | Mean | p99 | Maximum |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | Control | Expression | 64 | 652.729 | 1056.198 | 1154.456 |
| 1 | Control | Gliss | 64 | 688.127 | 1091.631 | 1286.042 |
| 1 | Control | Expression | 128 | 1308.138 | 1545.113 | 1617.242 |
| 1 | Control | Gliss | 128 | 1386.518 | 1579.089 | 1818.241 |
| 1 | Retained | Expression | 64 | 625.649 | 966.935 | 1020.011 |
| 1 | Retained | Gliss | 64 | 663.144 | 1003.444 | 1189.976 |
| 1 | Retained | Expression | 128 | 1278.984 | 1639.713 | 4410.026 |
| 1 | Retained | Gliss | 128 | 1321.712 | 1491.424 | 1891.982 |
| 2 | Control | Expression | 64 | 651.723 | 1048.247 | 1113.183 |
| 2 | Control | Gliss | 64 | 689.892 | 1091.945 | 1255.548 |
| 2 | Control | Expression | 128 | 1303.973 | 1539.712 | 1608.960 |
| 2 | Control | Gliss | 128 | 1386.603 | 1562.737 | 1759.824 |
| 2 | Retained | Expression | 64 | 625.216 | 965.847 | 1016.703 |
| 2 | Retained | Gliss | 64 | 661.313 | 998.837 | 1260.829 |
| 2 | Retained | Expression | 128 | 1250.877 | 1486.718 | 1559.297 |
| 2 | Retained | Gliss | 128 | 1322.098 | 1492.308 | 2167.287 |

Retain this refinement: the Attack-bank mean falls **4.40–4.74%**, static sound
**4.07–4.66%**, expression **2.23–4.15%** and gliss **3.63–4.67%** across both
callback sizes and both orders. The unaffected granular/identity controls are
close in the reverse-order pair, whereas the first pair's 64-sample controls
also improve 2.67–3.11%; host drift prevents assigning every observed percentage
solely to this arithmetic change. The bank without Attack changes by −1.65%
to +0.44% rather than matching the consistently larger Attack gains.

Attack/static/gliss p99 falls in both pairs at both callback sizes. Expression
p99 falls at 64 samples, while its 128-sample result is **mixed**: +6.12% in pair
one and −3.44% in pair two. This is not evidence of consistently improved tails.
The retained candidate also has **4410.026 µs** expression, **3456.486 µs** static
and **5351.144 µs** no-Attack bank maxima at 128 samples, each above the
2666.667 µs period. These remain in the CSVs and are not rejected as inconvenient
samples. Even without those excursions, the expression mean is about **47–48%**
of its callback period and gliss about **50%**, well above the **25% goal**.
This is a modest reduction of average demand, not target/combined-chain admission.

Complete unedited CSVs, including all other workload medians, percentiles,
maxima and reset/preparation/transform diagnostics, remain under
`build-ci/pog3-artifacts/round-{control,candidate}-{1,2}.csv` (ignored build
artifacts). An initial control was interrupted before producing rows because
validation was still running; it is retained as `round-control-interrupted.csv`
and is not timing evidence. Reproduce the current benchmark with:

```sh
build-ci/pedal-pog3-bench --csv build-ci/pog3-artifacts/round-retained.csv
```

## Prepared POG3 FFT table layout

### Scope and immutable preparation

`SpectralPlan` now has a local prepared radix-2 layout for **N ≤ 4096**, covering
all active 1024/2048/4096 plans. The shared `RealtimeFft` source, its public API,
convolver, compiler flags and CMake ownership are unchanged. The shared granular
reader correction described below is separate from this FFT layout change.
Larger supported foundation plans, **8192/16384/32768**, retain the original
shared implementation. FFT scaling, butterfly operations and coefficient bits
are retained; this is a table/traversal change, not a real-FFT approximation or
an alteration of spectral resolution.

All tables are constructed off the audio thread and are immutable thereafter.
For a power-of-two N, reversing its log2(N) bits fixes
**2^ceil(log2(N)/2)** palindromic indices. Every other index belongs to exactly
one swap pair. Store only those pairs in their original ascending-source order,
using `uint16_t`; active indices are at most 4095. No per-transform reversal
calculation, index comparison or growing vector remains. Preparation validation
run before allocation/index conversion and still reject invalid N/H combinations.

Store each stage's twiddles contiguously rather than loading the largest table
at a stride. The half-stage lengths sum to **N−1**, and stage length L starts
at **L/2−1**; the last valid weight index is N−2. Each prepared angle uses the
shared FFT's original largest-table index and double arithmetic, preserving
its rounded float coefficient. Separate compile-time forward/inverse kernels
retain the original complex multiply/add/subtract and exact **1/N** inverse
scaling. Standard complex overflow behavior remains intact; no relaxed floating-
point flag, intrinsic, additional transform workspace or callback allocation
is introduced. Synthesis still validates before transactional overlap-add.

The preliminary standalone prototype preserves normal FFT output bits and
shows roughly **23–25%** lower forward and **5–8%** lower inverse times at the
three active sizes. Its **32768-point inverse regresses about 2.2–2.3%**, which
is why larger plans keep the shared path. The N=32 prototype also improves;
all smaller valid plans use the bounded layout. These microbenchmarks are
feasibility evidence, not full-processor or target admission. Source/output
are retained under `/tmp/pog3-dense-fft/dense-fft.h`, `compare-bench.cpp` and
`microbench.csv`; the production path has no prototype `noinline` attribute.

### Memory and lifetime review

On this 64-bit host, the original reversal/twiddle storage is **12N bytes**.
Packed swaps use **2(N−fixed) bytes**, and contiguous stage weights use
**8(N−1) bytes**. Two additional vector descriptors add **48 bytes per plan**;
the unused shared FFT object allocates no tables on an active dense plan.
Across all three active sizes this reduces requested preparation storage by
**14,536 bytes**, despite the denser coefficient tables.

All-mode preparation is now **2,040,118 bytes (1.946 MiB)**, **57,034 bytes below**
the initial 2 MiB goal. Static sound requests 2,034,320, dedicated gliss 2,038,632
and the bank 1,865,952 bytes. No renderer, analysis, capture, phase or excitation
capacity is reduced. Plans retain shared const ownership; values/workspaces stay
owned by their existing analysis or renderer. The private plan layout changes,
so every consumer is rebuilt; old/new comparison binaries use their respective
headers and libraries rather than mixing layouts.

These are requested host allocation counters, not retained RSS or target ABI
measurements. The table trade-off depends on `sizeof(size_t)`; a 32-bit target
has a smaller original reversal table and must be measured independently.
The image build targets 64-bit AArch64 (`scripts/build-image-in-container.sh`),
but no target binary or device measurement is produced by this work.
Target memory and combined-chain admission remain open.

### Exactness and quality verification

The foundation suite now compares **1,834,112 complex bins** against the
independent shared FFT, across every power of two from **32 through 32768**,
both directions and 14 fixtures. It includes zero/signed-zero, complex and real
noise, impulse/DC/Nyquist, Hermitian spectra, subnormals, broad exponent ranges,
finite-overflow, NaN and infinity cases. Every finite/infinite component must
match bits, including sign; NaN classification must match, while payloads are
not an audio contract. The 4096/8192 boundary and small/oversized invalid
preparation are covered. Existing foundation checks retain exact chunking,
startup/drain/delay, deferred publication/reset, and transactional invalid-frame
rejection with pending healthy OLA.

A separate automated comparison to **a3180022** preserves every output bit
across **65,536 samples × six voices × stereo** with arbitrary boundary
Attack/Warp controls, all expression selections, unequal stereo and resets
before/after deferred completion. Both raw outputs have SHA-256
`44f2a1f5b1dd9b71f70559bbbbba7274266217c11602cafc98932b4ea46e3683`.
The changed stream and foundation-test units compile without
`-O3 -Wall -Wextra -Wpedantic` diagnostics.

All **14 selected release suites pass** on the prepared layout (**114.44 s**),
including both 60-second freeze holds and existing pitch/harmonizer/catalog/
scene/convolver regressions. Full foundation and spectral-pitch ASan/UBSan tests
pass with leak detection (**327.02 s**, `-j2`), as does the freeze warm-live
release check. These tools check DSP indexing and undefined operations during
validation; they add no processing checks or allocations to the release audio
path. The changed shared reader and legacy pitch-test units also compile without
`-O3 -Wall -Wextra -Wpedantic` diagnostics.

### Granular reference mismatch and fractional ring wrapping

Comparing all 53 raw WAVs initially found one differing float: right-channel
frame **74910** of `reference-warp-focus-on.wav`. Every POG3 spectral output was
unchanged. The granular test reference exposed an existing shared
`PitchShifter::ReadInterp` defect because preparation changed heap placement.
Its original loop first normalized the upper edge, then added 8192 to a tiny
negative position. Float rounding can make that sum exactly **8192**, leaving
the subsequent read one sample beyond the declared ring. It could therefore
mix an unrelated adjacent value into one audio sample.

A public-API reproduction uses five ordinary shifters at −24/−12/+7/+12/+24,
8192-sample rings, 1024-sample grains and a 100 Hz, 7-bit heel-to-toe Warp
trajectory at 48 kHz. A harmonic chord reproduces the read at **sample 74910,
voice 4**. With a NaN sentinel after the declared ring, the new permanent
`pitch fractional wrap` test fails on the original reader while all nine
preceding legacy pitch checks pass. An independently compiled reproduction
with exactly 8192 allocated samples reports the same one-past-ring read under
ASan; the corrected reader passes ASan/UBSan and remains finite.

The correction simply normalizes negative positions **before** normalizing the
upper endpoint. The interpolation kernel, ring size, grain restart, shift
mapping and callback allocation behavior are unchanged. The plan permits a
shared shifter correction for a demonstrated defect; this is that case.

After the correction, **52/53** WAVs remain bit-identical to the preserved M7
baseline. The granular reference differs at just that one float: the original
M7 value **0.012932582758367062** becomes **0.01161237247288227**. The historical
baseline is preserved rather than overwritten. All **53/53** candidate WAVs
are bit-identical to a freshly rendered **original-FFT control with the same
corrected reader**. Thus the FFT layout itself retains every audio output bit.
Comparison artifacts are in `build-ci/pog3-artifacts/dense-renders` and
`/tmp/pog3-dense-fft/control-renders`; the original mismatch/reproduction binaries
and logs remain under `/tmp/pog3-dense-fft`.

On the final combined code, all **18 selected release suites pass (118.67 s)**:
the eight POG3 suites plus existing pitch, harmonizer, Daisy smoke/response/
automation/catalog, manager catalog, scene and convolver checks. This includes
both long freeze holds and existing Whammy/Harmonizer/Chorus Detune/Shimmer
coverage. Full legacy pitch and Daisy smoke ASan/UBSan suites pass with leak
detection (**42.61 s**, `-j2`); the exact-ring reproduction also passes. No
regression test or sanitizer is added to the real-time signal path.

### Final callback timing with the corrected reader

Four ordinary release benchmark runs execute sequentially in the order
**control → candidate → candidate → control**, after all build, render and
validation processes finish. Both controls use the original POG3 FFT layout
from **a3180022**, rebuilt against its matching private plan header, and the
**same corrected shared reader** as the candidate. Both sides use the current
probe, `-O3 -DNDEBUG`, the same 64/128-sample inputs and all seven workloads.
No timing instrumentation, relaxed math flags or discarded outliers are used.

Across both orders and callback sizes, observed mean demand falls by
**6.09–7.75%** for the Attack bank, **5.64–7.70%** for static sound,
**5.54–8.19%** for expression, and **5.13–7.31%** for gliss. Identity improves
**11.35–12.53%**; the no-Attack bank improves **6.11–8.15%**. The unchanged
granular workload varies **−1.11% to +0.69%**, exposing ordinary host drift.
These are host observations, not exact causal or target speedup claims.

All affected p99 comparisons improve, but maxima do not uniformly improve:
pair 2's 64-sample static maximum rises from **1171.034 to 1485.926 µs**, above
the **1333.333 µs** callback period. Pair 1's 128-sample expression control
maximum is **2211.164 µs**; pair 2's no-Attack control reaches **2258.158 µs**.
Every outlier remains in its percentile, mean and CSV. Full-path candidate
mean demand remains **43.44–46.99%** of the callback period, exceeding the
**25% goal**. This change improves average work but does not satisfy CPU/device
admission or make the effect selectable.

All **56 workload rows** complete with zero processing/control/reset
allocations and healthy pre-reset analysis/render counters. Maximum transform
counts remain **11/16** for 64/128-sample bank/full-path callbacks; the separate
identity comparison retains **20**. Preparation counters agree across both
orders: bank/full paths save **14,536 bytes**, the two-plan identity foundation
saves **6256 bytes**, and granular reference storage stays unchanged. Tables,
resolution, phase/capture capacity and staged deadlines are unchanged between
callback sizes.

| Pair | Path | Frames | Mean old → new (µs) | Mean change | p99 old → new (µs) | Max old → new (µs) |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 1 | Granular | 64 | 97.354 → 97.185 | -0.17% | 658.591 → 655.565 | 672.395 → 668.668 |
| 1 | Identity | 64 | 233.630 → 206.502 | -11.61% | 813.253 → 713.302 | 1003.388 → 855.587 |
| 1 | Bank | 64 | 558.536 → 524.387 | -6.11% | 828.638 → 764.671 | 907.621 → 834.889 |
| 1 | Attack bank | 64 | 568.674 → 534.023 | -6.09% | 873.626 → 815.169 | 914.648 → 890.261 |
| 1 | Static | 64 | 620.618 → 580.978 | -6.39% | 933.998 → 865.426 | 1664.623 → 896.152 |
| 1 | Expression | 64 | 624.392 → 589.826 | -5.54% | 962.098 → 903.726 | 1035.276 → 973.728 |
| 1 | Gliss | 64 | 661.455 → 626.527 | -5.28% | 997.522 → 937.933 | 1157.714 → 1095.647 |
| 1 | Granular | 128 | 193.163 → 194.498 | +0.69% | 676.340 → 678.661 | 681.357 → 687.727 |
| 1 | Identity | 128 | 464.925 → 412.133 | -11.35% | 808.905 → 717.978 | 903.008 → 726.597 |
| 1 | Bank | 128 | 1119.990 → 1048.534 | -6.38% | 1319.528 → 1211.037 | 1462.373 → 1277.780 |
| 1 | Attack bank | 128 | 1140.820 → 1069.788 | -6.23% | 1291.934 → 1200.427 | 1561.226 → 1357.889 |
| 1 | Static | 128 | 1232.028 → 1161.904 | -5.69% | 1371.339 → 1270.064 | 1402.879 → 1323.449 |
| 1 | Expression | 128 | 1252.636 → 1179.722 | -5.82% | 1486.458 → 1396.838 | 2211.164 → 1463.187 |
| 1 | Gliss | 128 | 1315.505 → 1248.027 | -5.13% | 1472.160 → 1387.606 | 1604.372 → 1601.668 |
| 2 | Granular | 64 | 97.830 → 98.074 | +0.25% | 668.923 → 659.177 | 905.632 → 771.443 |
| 2 | Identity | 64 | 235.494 → 206.631 | -12.26% | 858.110 → 714.417 | 1122.554 → 741.594 |
| 2 | Bank | 64 | 571.374 → 524.832 | -8.15% | 919.356 → 767.110 | 1102.649 → 829.321 |
| 2 | Attack bank | 64 | 577.365 → 532.592 | -7.75% | 939.108 → 810.457 | 1241.282 → 858.470 |
| 2 | Static | 64 | 629.344 → 580.869 | -7.70% | 1001.345 → 864.802 | 1171.034 → 1485.926 |
| 2 | Expression | 64 | 642.320 → 589.704 | -8.19% | 1024.995 → 904.547 | 1550.234 → 960.736 |
| 2 | Gliss | 64 | 672.043 → 622.941 | -7.31% | 1049.661 → 934.127 | 1193.125 → 1104.911 |
| 2 | Granular | 128 | 196.548 → 194.369 | -1.11% | 727.083 → 677.792 | 886.041 → 688.512 |
| 2 | Identity | 128 | 472.405 → 413.210 | -12.53% | 976.411 → 721.124 | 1021.363 → 819.107 |
| 2 | Bank | 128 | 1128.313 → 1049.453 | -6.99% | 1345.998 → 1216.273 | 2258.158 → 1267.307 |
| 2 | Attack bank | 128 | 1140.743 → 1070.756 | -6.14% | 1299.804 → 1193.858 | 1444.442 → 1280.372 |
| 2 | Static | 128 | 1227.744 → 1158.518 | -5.64% | 1356.511 → 1279.691 | 1642.557 → 1330.202 |
| 2 | Expression | 128 | 1255.780 → 1178.808 | -6.13% | 1498.619 → 1394.891 | 1691.614 → 1465.699 |
| 2 | Gliss | 128 | 1323.400 → 1248.295 | -5.68% | 1512.387 → 1387.721 | 1879.092 → 1684.278 |

The CSVs retain all columns, including median/p95/p999, reset cost and worst
callback position/transform diagnostics. Files are ignored development artifacts:
`build-ci/pog3-artifacts/dense-{control,candidate}-{1,2}.csv`. The earlier
`dense-preliminary-*` files precede the shared reader fix and are not final
comparison evidence. Reproduce current validation and measurement with:

```sh
ctest --test-dir build-ci --output-on-failure -R '^pedal-(pog3-.*|pitch-effect-quality|harmonizer-(quality|smoke)|daisy-fx-(catalog-smoke|smoke|response|automation)|manager-effect-catalog-smoke|scene-plan-smoke|scheduled-convolver-smoke)$'
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build-pog3-sanitize -j2 --output-on-failure -R '^pedal-pog3-(quality|pitch-quality)$'
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 build-pog3-sanitize/pedal-pog3-freeze-quality --warm-live
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build-pog3-sanitize -j2 --output-on-failure -R '^pedal-(pitch-effect-quality|daisy-fx-smoke)$'
build-ci/pedal-pog3-bench --csv build-ci/pog3-artifacts/dense-retained.csv
```

All GitHub CI checks pass on the preceding **a3180022** revision. The new FFT
layout and shared reader correction require their own CI run after push.

## FFTW production CPU checkpoint (2026-10-06)

The user accepts GPL-linked builds. Active 1024/2048/4096 plans now use immutable
single-precision FFTW complex plans, with new-array execution on existing
renderer/analysis vectors and exactly one 1/N inverse gain. Planning/destruction
serialize off the callback; unsupported foundation sizes and the shared
convolver FFT keep `RealtimeFft`. CMake/CI discover the platform `fftw3f` package;
the Pi package selects it with AArch64 NEON and single-thread execution.

The default FFTW buffered solver allocated C temporaries despite the old C++
allocation counter reporting zero. `FFTW_NO_BUFFERING` removes that execution
allocation. The new glibc C allocation/free CTest verifies all 14 full workloads,
reset and control helpers, with a required-preload negative check. All nine POG3
release suites and the affected sanitizer checks pass. Active transform accuracy
now has an independent -110 dB numerical contract rather than requiring the
old algorithm's float bits; impulse gain, concurrency, latency, partition,
whole-frame rejection/recovery and existing DSP quality gates pass.

Four final opposite-order runs against `59be448c` show **24.12–27.66% lower
full-path mean demand**, now **31.67–35.27%** of the period. CPU admission is still
unmet; host maxima retain period overruns. A separate diagnostic profile puts
FFT at approximately 6% of processor work, with rendering at 33–38% and both
interpretation and Attack at 20–24%. CPU effort now follows those larger costs.
The C++ requested preparation count is **2,026,198 bytes**, excluding FFTW's
opaque C storage. Fuller glibc heap observations exceed 2 MiB, so the previous
C++-only goal must not be reported as complete backend memory admission.

See [the FFT backend evaluation](pog3-fft-backend-evaluation.md) for plan/alignment
ownership, the allocation finding and fix, numerical contract, complete timing
tables and outliers, memory-accounting limits, build/license details, profile
and the detailed next CPU steps. Historical exact-WAV/scalar measurements above
remain checkpoints; they are not claims about the new FFTW output.

## Pitch-library and ERB-PS2 investigation (2026-10-06)

An optional standalone trial now compares pinned Signalsmith Stretch 1.3.2,
Rubber Band 4.0.0 and Ardor's existing Terrarium-derived filter/multirate code
against the current FFTW pitch bank. Five/eight-path stereo workloads, static
and moving Warp, 64/128 callbacks, separate C allocation/free observations and
settled tone/chord/alias diagnostics expose CPU, buffering and fidelity tradeoffs.
No dependency or renderer is added to the production build.

Signalsmith's tested cheaper/larger-hop configurations reduce CPU but fail the
tone-tuning screen; the short POG3-sized configuration also misses tuning and
does not improve pitch-bank demand. Separate Rubber Band instances exceed the
CPU goal before the remaining POG3 stages. Its variable-output adapters exhibit
counted FIFO faults; the Live adapter avoids those faults but needs fixed-block
buffering and remains too costly. These are scoped observations of the tested
options/adapters, not claims about all implementations or future shared-analysis
forks. The Terrarium reference has only three fixed octave outputs and reduced
bandwidth, so its smaller cost is not full POG3 admission.

The supplied ERB-PS2 thesis was retrieved in full and its technical/evaluation
sections reviewed. Ardor's current Poly Octave mode already ports Terrarium,
with 48 bands rather than upstream's 80; the thesis's exact constant-ERB setup
is a distinct design. The next selected prototype reproduces its octave-up
case, verifies filter coefficients and response, then evaluates shared analysis
for the full ratio range. Fifth/Warp, alias suppression, independent Attack and
freeze remain explicit extension work; a 43-band octave-up result alone would
not replace POG3. The thesis's roughly 3 ms high-band response must not be
reported as a bass/full-band latency guarantee or an embedded CPU measurement.

See [the screening report](pog3-pitch-library-evaluation.md) for pinned inputs,
measurement contracts, full results and reproduction, and
[the ERB-PS2 prototype plan](pog3-erb-ps2-evaluation-plan.md) for implementation
steps and the full-feature CPU gates. Production behavior is unchanged and M8
remains blocked on admission.

## ERB-PS2 octave-up CPU reference (2026-10-07)

The optional trial now implements the thesis's 43-band non-decimated octave-up
grid and repeated positive-frequency pole transformation. Preparation is double
precision; two float first-order sections share a stable rounded pole, with
independent stereo histories and accurate double magnitude for +12 rendering.
The analytic center-gain normalization is an explicit implementation choice;
the author's Matlab gain code was unavailable.

Two opposite-order host passes measure **3.062–3.121% of the callback period**
for **one stereo +12 voice**, with no observed overruns and zero separately
observed C callback allocation/free calls. Core storage is 3632 bytes, excluding
adapter buffers and future feature histories. This is not full POG3 admission.
The independent coefficient/complex impulse-transfer, stereo sustain/reset,
strict warning and ASan+UBSan checks pass.

The settled-tone tuning screen passes (+0.015750 cents), but the raw output
loses roughly 12.7 dB and its -42.5943 dBc spur misses the <-45 dBc gate.
Resolved-chord unwanted partials and close-low-pair gain imbalance remain.
One high-frequency alias screen passes; full upward-ratio coverage is untested.
No fixed scalar latency is claimed for the frequency-dependent filter response.

See [the reference results](pog3-erb-ps2-reference-results.md) for equations,
normalization, every retained timing row, raw audio metrics, artifact hashes and
reproduction. The next CPU experiment is the shared multi-voice design in
[the evaluation plan](pog3-erb-ps2-evaluation-plan.md), retaining this reference
as a control. Full coverage, downward continuity, fifth/Warp, independent Attack
and freeze remain extension work; production behavior and M8 are unchanged.

## Shared ERB eight-path CPU checkpoint (2026-10-07)

The optional ERB trial now shares analysis/magnitude across unison, five shifted
stereo voices and two warm upper paths. Independent double phase histories
retain downward branches, implement the true equal-tempered fifth and integrate
moving ratios. The six long-path ratios follow Warp; the two upper short-path
ratios remain fixed. The earlier library helper had that upper assignment
reversed; it is corrected for new runs and recorded beside the historical CSVs.
This matches path ratios, not the production Focus resolutions/alignment.

Both the original 43-band grid and a 69-band extended 35.4829–20019.8 Hz grid
have accurate and table-math variants. Table math approximates absolute input
angle before differencing, avoiding constant-ratio accumulation of increment
approximation error. Independent phase ramps/reversals, raw +12 equivalence,
stereo/reset/partition, strict warnings and ASan+UBSan checks pass. Sixteen
allocation invocations observe zero C callback allocation/free calls. Core
storage is 13336/21344 bytes, with a separate 24592-byte shared table object.

All 48 timing invocations finish in two opposite-order passes after heavy
verification jobs. The limited-grid table variant costs **28.333–29.648%** of
the period; the wide variant costs **48.628–51.945%**, before Attack/freeze and
the remaining effect stages. Accurate phase math costs substantially more.
No variant meets the full **25%** CPU goal. All callback outliers are retained,
including six table-wide overruns and two unchanged-Ardor host overruns.

Every exposed settled-tone interval tunes within 0.041 cents. Wide +12 tone
gain/spur improves, but +24 spurs and polyphonic gain/unwanted partials remain
inadequate. A center-plus-tail taper is only a heuristic; one alias fixture does
not validate all ratios/transients. No full latency or target-device guarantee
is claimed.

See [the shared ERB report](pog3-erb-shared-cpu-results.md) for source/control
contracts, numerical errors, all CPU ranges/tails, raw audio limitations,
reproduction and artifact hashes. The next CPU experiment is reduced-rate
baseband phase/magnitude estimation with full-rate carrier reconstruction, or
appropriate multirate tiers. Retain these accurate/table implementations as
controls; defer Attack/freeze/public integration until CPU feasibility is proved.
Production DSP and M8 are unchanged.

## ERB counted-cadence CPU checkpoint (2026-10-07)

The optional wide-bank candidate now counts analytic carrier winding at full
rate with imaginary-sign crossings and their cross-product direction. Endpoint
angles and oscillator reanchoring run every 4/8/16/32 samples. Control boundaries
flush the preceding ratio segment, and startup captures the first meaningful
phase before its first endpoint, preserving fractional voice phase origins.
The center-only alternative is kept as a failed comparison: it demonstrably
folds an off-band 5000 Hz carrier into a -1000 Hz estimate.

The 32-sample counted candidate renders all eight warm stereo paths at 48 kHz
with double recurrence state. Two opposite-order passes (**60 invocations**)
measure **20.879–21.694%** host callback-period demand, **56.49–59.25%** below the
matching full-rate wide table control. One retained 64-sample callback reaches
2910.130 µs and overruns; no callback is discarded. These are **bank-only**
costs. Required ownership, independent Attack, held/live work, additional
analysis and warm Focus/alignment behavior are unpriced, so the full 25% goal
must not be declared met.

All three optional foundation suites, strict warning checks and cadence
ASan+UBSan/leak checks pass. Twenty separate allocation workloads report zero
C callback allocation/free calls and stream faults. Core storage is 51144 bytes
at intervals 4/8/16 and 64392 at 32, plus a shared 24592-byte math table. Input
near DC/Nyquist, off-band cycles, delayed startup, ratio ramps/reversals,
stereo/reset and arbitrary partitions are covered. Constant-carrier worst output
error across counted variants is 2.50022e-6; interval-32 interior interpolation error on the independent
smooth chirp/Warp fixture is 5.55541e-4. Transient comparison against the delayed
accurate bank retains -54.80…-41.36 dB error/reference power across voices.

The candidate adds 0.667 ms reconstruction delay with extra endpoint startup;
filter group delay remains frequency-dependent. Settled-tone tuning is within
0.041 cents, +12 tone gain/spur is good at the tested frequency, and +24's
-43.26 dBc spur still misses the <-45 dBc gate. Raw chord gain/partial failures
remain; one alias fixture does not establish complete alias coverage.

See [the cadence report](pog3-erb-cadence-cpu-results.md) for equations,
failed center-only evidence, numerical/latency tradeoffs, all CPU ranges/tails,
retained audio metrics, reproduction and artifact hashes. The next CPU decision
is combined required-stage cost; production DSP and M8 remain unchanged.

## ERB Attack/freeze combined checkpoint (2026-10-07)

**ERB Attack/freeze checkpoint:** the isolated hybrid now prices all three
warm stereo spectral ownership paths, active Attack projected into the counted
ERB bank, and actual freeze/gliss with eight held stereo paths. Overall host
demand is 66.61–70.14%, rising to 82.23–87.09% during the middle held segment,
with frequent callback overruns. Resolved-note and stationary freeze fixtures
pass; close-note independence, raw pitch quality, live stereo phase and fade
coherence remain open. No production renderer is replaced. See
[the complete stage costs and DSP evidence](pog3-erb-attack-freeze-results.md).
The user has clarified that 25% is a chain-budget planning target, not a hard
usability limit. Compare the full FFTW processor and ERB hybrid on hardware now;
actual callback deadlines, intended-chain margin and xruns determine admission.

## Pi 4 CPU checkpoint (2026-10-07)

**Pi 4 CPU checkpoint:** the hardware comparison is complete. The existing
full FFTW processor consumes **101.85–115.97%** of a period across its tested
full-feature workloads at 48 kHz / 64 and 128 frames. The ERB Attack/freeze
hybrid consumes **186.92–192.55%** overall and **233.53–242.21%** during its held
segment. The bare ERB bank costs 60.84–62.65% with no measured overruns, but
is not a complete admitted block. All 72 stage rows and 28 full-suite rows are
retained. ARM numerical checks pass after correcting exact Attack-off routing.
The live service is restored; firmware, presets and live buffers are preserved.
See [hardware results, receipts and the next CPU work](pog3-pi4-cpu-results.md).
The next step is target profiling and reducing average/burst work. Full-feature
capacity, rather than the initial 25% planning target, still blocks M8.

## Pi 4 magnitude optimization (2026-10-07)

**Pi 4 optimization checkpoint:** the target frame-job profile attributes
20–23% of full-workload time to FFT, 21–32% to live rendering and 14–18% each
to interpretation/Attack; held rendering reaches 16% during freeze/gliss.
An audio-range magnitude fast path with wide arithmetic for extreme values
saves **1.04–1.47%** in a sequential, uninstrumented four-run comparison.
All nine production DSP tests and ARM numerical checks pass. Full-path demand
remains **100.57–113.53%**, and every full-path p99 still exceeds its period.
This is a small optimization, not standalone or chain admission. The next
candidate is a prepared real-input/Hermitian FFT path that preserves generic
complex behavior, allocation/latency contracts and audio quality. See
[the target profile, A/B evidence and implementation constraints](pog3-pi4-profile-results.md).

## Real FFT optimization (2026-10-07)

**Real FFT checkpoint:** prepared real-input/Hermitian FFTW plans and direct
packed-output synthesis now reduce full-path Pi mean time by **5.83–7.48%**
against the magnitude-optimized baseline. Static/expression average demand is
93.18–97.53%; freeze/gliss remains **105.67–107.34%**. All full-path p99s still
exceed their periods. All nine production DSP tests, foundation ASan+UBSan,
full ARM pitch and FFT/stream checks pass; the synthesis sample oracle matches
the public inverse. Generic complex/extreme arithmetic remains available.
A separate target profile confirms aligned real-plan execution and attributes
about 16–18% to FFT, 21–32% to live rendering and up to 18% to held rendering.
The Hann-lobe layout experiment follows below; M8 remains blocked.
See [the complete real FFT evidence and next experiment](pog3-real-fft-results.md).

**Hann-lobe layout checkpoint:** the same 8,193 prepared coefficients now use
phase-major storage with unchanged interpolation bits. All 393,258 historical
lookup comparisons, nine production DSP tests, ASan/UBSan and full ARM pitch/
foundation checks pass. A fresh Pi ABBA observes **1.18–2.90%** lower full-path
mean time; freeze/gliss remains **103.26–103.90%**, and all full-path p99s exceed
their periods. The uninvolved spectral identity control also varies, so this is
an observed build comparison, not a pure cache-locality attribution. M8 remains
blocked. Next, test skipping zero-amplitude held reconstruction after phase
advancement, then repeat the target comparison.
See [the layout evidence and constraints](pog3-lobe-layout-results.md).

**Silent held reconstruction checkpoint:** exactly-zero held amplitudes now
skip carrier/lobe reconstruction after advancing phase. Both host and Pi
before/after traces match bit for bit across 1,376,256 samples; all nine DSP tests
and the full ARM pitch suite pass. A fresh ABBA observes 0.87–1.18% lower
freeze/gliss mean time, with similarly sized variation in some uninvolved paths.
Final freeze/gliss demand remains **102.44–103.15%** and every full-path p99 is
over period; M8 stays blocked. A subsequent profile attributes about 21–31% to
live rendering, 16–18% each to FFT/interpretation/Attack and up to 16% to held
rendering. Next, improve matched-plan comparison controls and prototype batched
lobe/live accumulation work under the existing DSP contract.
See [the complete held CPU evidence and next experiments](pog3-held-zero-results.md).

**Reduced-core scope checkpoint:** freeze/gliss is now deferred by default;
`ARDOR_POG3_EXPERIMENTAL_FREEZE=ON` retains both algorithms and their audio tests.
Default Off/Volume/Crossfade/Warp/Filter preserve all existing parameter indexes
and normalized values; unavailable modes are explicitly refused. A matched Pi
comparison with shared FFTW wisdom saves **3.78–3.79%** by omitting inactive
preparation, giving **87.04–87.97%** mean period demand. Peaks remain a blocker:
64-frame over-period counts are 1152–1170 / 3000; 128-frame counts are 9–21 / 1500.
Stationary hold is substantially cheaper than moving gliss in this matched
workload, but both remain experimental. Default/opt-in builds pass eight/nine DSP
suites; host/Pi live traces match byte for byte across 384,000 samples each.
The reduced-core profile has zero freeze/held calls and attributes about 32% to
live rendering and 18–19% each to interpretation/FFT/Attack. Next, reduce callback
peaks under exact control/timestamp/ordering checks and optimize live accumulation.
See [the scope decision, matched evidence and next CPU work](pog3-freeze-scope-results.md).

**Four-source accumulation checkpoint:** eligible live regions now update each
destination once per four source bins, retaining the original contribution order.
This reduces eligible-batch destination read/write pairs from 96 to 27, without
changing preparation storage or edge paths. The first ARM trace caught a changed
imaginary multiply contraction; explicit baseline contraction fixes it. Final
host/Pi renderer and full-processor traces match byte for byte across 552,960 and
384,000 samples respectively. Default/opt-in builds pass all eight/nine DSP
suites. A fresh matched ABBA observes 1.280%/0.938% lower core mean demand at
64/128 frames; uninvolved spectral means also vary 0.691%/0.769%. Candidate demand
is 85.94–86.66%, but 64-frame over-period callbacks remain 1120–1161/3000 and
128-frame counts 4–7/1500. The worst observed 128-frame callback is higher than
baseline, so neither tail elimination nor live admission is established. Next,
measure batch eligibility and try two-source coverage before schedule changes.
See [the arithmetic, assembly and complete matched evidence](pog3-scatter-batch-results.md).

**Two-source accumulation checkpoint:** the uniform pair prototype is rejected;
four-source production code is retained. Exact host/Pi renderer/full-processor
traces and all eight/nine default/opt-in DSP suites pass, but the fresh matched
ABBA observes −0.731%/+0.516% mean reductions at 64/128 frames, with more
over-period callbacks at both sizes. Uninvolved spectral controls also vary.
Measured region geometry models 86.957%/62.648% of fractional interior sources
batched at widths two/four, yet pairs perform about 6.11% more modeled interior
destination accesses. The patch, diagnostic generator and complete receipts
remain as experiment evidence. Next, try pairs only for leftovers after
four-source batches, with exact output and matched device controls; then address
clustered callback work. Neither uniform batching experiment establishes live
admission. See [the rejected pair comparison and Luna next steps](pog3-scatter-pair-results.md).

**Hybrid accumulation checkpoint:** pairing only interior leftovers preserves
four-source batches and models 21.189% fewer interior destination accesses.
Exact host/Pi renderer/full-processor traces and all eight/nine default/opt-in
DSP suites pass. A second matched ABBA weakens the initial CPU gain: confirmation
means improve 0.130% at 64 frames and regress 0.063% at 128; combined four-run
means improve only 0.440%/0.085%. The worst candidate 64-frame callback increases.
The extra production path is rejected and four-source code is retained exactly.
Next, diagnose stage cost/counts by callback phase before adjusting renderer due
ages, preserving original controls, frame ownership, history ordering, publication
and N+H latency. See [both matched rounds and the detailed Luna next-step plan](pog3-scatter-hybrid-results.md).

## Next implementation milestone

M0's parameter/publication contract and M2's streaming identity gates are in
place. M1 supplies an audible reference, renders, and timing/allocation evidence;
its artifacts remain the comparison baseline for subsequent work.

M3–M7's software engine and core DSP quality gates are implemented. Target-device
admission, combined-chain endurance, and listening review remain open; the
effect is not release-ready merely because its host tests pass.

The **live playing-feel audition is complete** with Attack zero, direct
dry/up/down/blend footswitch selection and both Focus settings. The user reported
that everything felt okay, with a slight delay in the tone that was barely
noticeable. This is overall subjective acceptance of the current reduced-core
audition; per-mode ratings and physical input/output latency remain unmeasured.
The corrected live run completed 46,397 callbacks with zero ALSA xruns, mean
2058.66 µs, maximum 2744.54 µs and six over-period callbacks. The normal app was
restored after a clean stop. See [the live result and scope](pog3-live-audition.md).

The **bounded worker-pipeline feasibility probe is complete** for the reference
serial preset. The prior
[device budget and profiling](pog3-headroom-results.md) motivated moving POG3
to CPU3; the measured outcome is in
[the worker results and reviewed handoff](pog3-worker-pipeline-results.md).
The device currently uses 128 frames / 48 kHz, a 2.667 ms period. Two paced
POG3-only runs average 2.155–2.174 ms with no xruns but one over-period callback;
the reference vibe/NAM/EQ chain averages 0.956–0.975 ms. Both combined paced
attempts encounter a playback xrun after 11 callbacks during warmup. Unpaced
combined cost is 3.416–3.487 ms. The new one-block worker also misses strict
generation deadlines. Two blocks pass both 30-second repeats and a 180-second
run, with no stale replay, missed submissions, late outputs or ALSA xruns.
The longer run has 67500 measured callbacks, mean CPU2 1160.527 µs, mean CPU3
2324.920 µs and maximum worker submission-to-finish 2904.315 µs. Original audio
settings and DSP are retained. Repeat the playing comparison with the actual
**256-sample / 5.333 ms added delay**, then test the MMAP backend/UI and live
control snapshots before accepting public integration. CPU3 is available in the
tested serial preset; general worker-core ownership remains unresolved.

Compact Attack support-scoring read keys are paused: grouping itself accounts
for only 4.00% of fresh sampled DSP cycles, whereas the observed serial chain
needs about 22.7% less total work merely to fit on average. L1 refill counts do
not establish memory access as the dominant bottleneck. The previous bin-sort
0.912%/0.562% mean differences remain observations with mixed tails and a larger
improvement in an uninvolved spectral control, not isolated causal speedups.
Exact bounded selection, compact frame/Attack birth snapshots, refined due ages,
four-source rendering and skipped redundant sort remain. Both sparse phase
caches and the deferred gain split remain rejected. Production DSP is unchanged
by the measurement probes; public integration remains gated on real-backend,
topology and added-delay listening validation.
Matched planning controls and the freeze scope decision are complete. Both freeze
modes remain opt-in experiments; restore them only after core CPU feasibility.
The earlier full-feature hardware comparison exceeded standalone capacity at
both buffer sizes; the new reduced-core paced results above supersede the old
unpaced data for this specific 128-frame workload. The initial 25% planning target is not a hard
rejection threshold. Admission depends on target callback tails, the intended
chain and thermal/xrun endurance after standalone headroom is demonstrated. **M8 — factory, catalog, inspector, scene
and manager integration** remains blocked on feasibility. All seven DSP
expression selections have audio behavior in the opt-in build; the default exposes
five supported selections while retaining the other two normalized values. The C++ allocation counter stays
below its original goal, while FFTW internal storage, CPU margin, target
memory/endurance and M3/M4/M7 fidelity limits still require work. Successful
hold/routing tests do not satisfy target-device feasibility.
The balanced analysis schedule and bounded Attack matching have earlier
exact-output evidence; the shared granular read correction is separately
reproduced and documented. FFTW uses the new numerical/audio contract described
above. The earlier instrumented stage profile is historical; use the new
exclusive hardware-PC attribution and ordinary paced timings for the next
decision. The new probe does not qualify thermal endurance, all controls or the
production MMAP backend. No new production DSP optimization was selected.
Preserve input timestamps, control snapshots, complete-frame publication and
Attack/freeze/render deadlines.
Maintain latency, arbitrary callback partitions, allocation bounds and audible
quality gates; moving work does not solve excessive average demand.
Resolve admission before adding the public catalog entry, then exercise actual
physical/MIDI controls, scene cut/reset, preset/endpoints round-trips, tail/latency
reporting and runtime ownership. No callback-period or unrelated DSP quality
change is authorized by host results.

The granular reference must remain a test harness. It cannot satisfy independent
polyphonic attack, spectral freeze, or gliss and must not become an undocumented
production fallback.
