# Poly Octave 3 implementation status

Updated: 2026-10-06. This records the implementation increments against
[the implementation plan](pog3-effect-implementation-plan.md). The parameter
contract, audible granular comparison harness, and streaming spectral foundation
are implemented, along with a spectral five-voice pitch bank, continuous Warp,
and reversible Focus switching. Independent spectral attack and a reversible
Dry Attack router, separate filter AD, LP/BP/HP buses, detune/doubling,
asymmetric Spread, voice pan, and the static sound path are now implemented.
All seven expression modes now resolve through a prepared processor with
independent base/effective controls. Both freeze modes, moving-carrier gliss,
heel/toe hysteresis and per-voice eligibility are implemented. Prepared host
allocation now meets the initial 2 MiB goal. CPU admission, target memory and
combined-chain endurance, calibration/listening and public integration remain open.
There is no selectable `mod/pog3` entry yet.

Development continues in `/home/bbalazs/projects/ardor-pog3` on
`feat/pog3-polyphonic-octave`, based on current `main`, in
[draft PR #113](https://github.com/balazsbencs/ardor/pull/113). The original
workspace and its unrelated changes remain separate.

## Implemented code

| File / target | Responsibility |
| --- | --- |
| `src/daisyfx/pog3/Pog3Parameters.{h,cpp}` | Stable 33-index registry, defaults, normalized-to-physical mappings, display formatting, configuration validation, expression endpoints, Warp and dry-freeze eligibility helpers, lock-free control targets |
| `src/daisyfx/pog3/SpectralFrameStream.{h,cpp}` | Shared immutable FFT/window plans, causal streaming analysis, preallocated inverse FFT and overlap-add synthesis |
| `src/daisyfx/pog3/PolyphonicPitchBank.{h,cpp}` | Shared short/long/low analysis, bounded persistent partial tracks, fractional spectral translation, low-band reconstruction, independent stereo synthesis, continuous Warp, Focus fades, staged render jobs |
| `src/daisyfx/pog3/PolyphonicAttack.{h,cpp}` | Linked stereo harmonic families/residual partials, fixed old/new excitation history, input-timestamp attack ages, coherent gains across resolutions |
| `src/daisyfx/pog3/SpectralFreeze.{h,cpp}` | Fixed stereo capture/target storage, startup validity, heel/toe state machine, linked target proposals, bounded matching and partial gliss |
| `src/daisyfx/pog3/Pog3VoiceStages.{h,cpp}` | Separate linked detector/filter AD, stereo dry/generated TPT filter buses, levels/pan, upper/dry doubling, eligible 1:3 Spread, final master, static sound-path composition |
| `src/daisyfx/pog3/Pog3Processor.{h,cpp}` | Prepared lifecycle, immutable endpoint configuration, 33 key/index targets, 48-sample control cadence, separate base/effective values, all seven expression modes and freeze diagnostics |
| `tests/pog3_granular_reference.h` | Five independent stereo pitch shifters using the existing Whammy/Harmonizer primitive; dry, six levels/pans, input gain, and final master |
| `tests/pog3_controls.cpp` | Persisted index contract, both target setters, mappings, morph ownership, rejection/clamping, Warp and eligibility behavior |
| `tests/pog3_quality.cpp` | Identity/delay/startup/drain/chunking/reset checks, isolated reference tuning, stereo/pan/gain checks, nonfinite/overflow rejection, optional WAV renders |
| `tests/pog3_pitch_quality.cpp` | Spectral tuning/spurs/leakage, resolved and ordinary low chords, alias rejection, track continuity, Focus reversal, staged identity/deadlines, callback partitioning, Warp, overload/drain, envelope latency, close-pair diagnostic |
| `tests/pog3_attack_quality.cpp` | Held/new notes, shared harmonics, low bass, re-plucks, arpeggios, bends, Focus reversals, activation, exact-off dry, reset, partition invariance, optional attack WAV renders |
| `tests/pog3_voice_stages.cpp` | AD timing/retrigger, held-tone/chord detector, sensitivity/re-plucks, filter transfer and resonance, routing eligibility, delay endpoints/queues, pan, rapid automation/partition/reset, gain/overload/recovery, optional full-path WAV renders |
| `tests/pog3_expression_quality.cpp` | Processor key/index publication, cadence, immutable ownership, exact endpoints/units, processed-dry exclusions, 30 Warp tuning cases, Filter/envelope interaction, 7-bit mode/reverse/Focus automation, callback partitions, configuration/reset and optional expression WAV renders |
| `tests/pog3_freeze_quality.cpp` | Stationary pitch/level/stereo, genuine moving-carrier gliss, mid-glide latch/resume, strict dry/Focus eligibility, hysteresis/Reverse, startup/silent capture, dense input/reset, partition invariance, two 60-second holds and optional freeze WAV renders |
| `tests/pog3_artifacts.h` | Shared offline float WAV writer; preserves raw levels |
| `tests/pog3_bench.cpp` | Prepared callback timing distributions, transform burst counts, reset cost, allocation instrumentation, CSV output |
| `ardor_realtime_fft` | Sole CMake ownership of the existing `RealtimeFft.cpp`; shared with the existing DSP/convolver target |
| `ardor_pog3` | Independent parameter/spectral/pitch/attack/voice-stage/processor library; links the shared FFT without a Daisy/DSP dependency cycle |

The CMake edits retain the unrelated changes already present in the workspace.
Existing pitch modes, catalog entries, scene indices, FFT mathematics, and
tracked device binaries were not edited for this increment.

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

## Next implementation milestone

M0's parameter/publication contract and M2's streaming identity gates are in
place. M1 supplies an audible reference, renders, and timing/allocation evidence;
its artifacts remain the comparison baseline for subsequent work.

M3–M7's software engine and core DSP quality gates are implemented. Target-device
admission, combined-chain endurance, and listening review remain open; the
effect is not release-ready merely because its host tests pass.

The next work is CPU/target admission and **M8 — factory, catalog, inspector,
scene and manager integration**. All seven DSP expression selections now have
audio behavior. The initial host allocation goal is met; CPU margin, target
memory/endurance and M3/M4/M7 fidelity limits still require work. Successful
hold/routing tests do not satisfy target-device feasibility.
Use the new callback timestamps to profile coincident analysis bursts before
choosing the next optimization. A staged-analysis candidate must retain input
timestamps and the frame's control snapshot, expose only complete FFT frames,
and meet Attack/freeze/render consumer deadlines. Recheck latency, arbitrary
callback partitions, allocation bounds and audible quality before admission;
moving work does not solve excessive average demand.
Resolve admission before adding the public catalog entry, then exercise actual
physical/MIDI controls, scene cut/reset, preset/endpoints round-trips, tail/latency
reporting and runtime ownership. No callback-period or unrelated DSP quality
change is authorized by host results.

The granular reference must remain a test harness. It cannot satisfy independent
polyphonic attack, spectral freeze, or gliss and must not become an undocumented
production fallback.
