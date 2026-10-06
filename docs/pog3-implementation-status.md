# Poly Octave 3 implementation status

Updated: 2026-10-06. This records the implementation increments against
[the implementation plan](pog3-effect-implementation-plan.md). The parameter
contract, audible granular comparison harness, and streaming spectral foundation
are implemented, along with a spectral five-voice pitch bank, continuous Warp,
and reversible Focus switching. Independent spectral attack and a reversible
Dry Attack router, separate filter AD, LP/BP/HP buses, detune/doubling,
asymmetric Spread, voice pan, and the static sound path are now implemented.
Expression Off/Volume/Crossfade/Warp/Filter now resolve through a prepared
processor with independent base/effective controls. Freeze/gliss and public
integration remain pending. There is no selectable `mod/pog3` entry yet.

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
| `src/daisyfx/pog3/Pog3VoiceStages.{h,cpp}` | Separate linked detector/filter AD, stereo dry/generated TPT filter buses, levels/pan, upper/dry doubling, eligible 1:3 Spread, final master, static sound-path composition |
| `src/daisyfx/pog3/Pog3Processor.{h,cpp}` | Prepared lifecycle, immutable endpoint configuration, 33 key/index targets, 48-sample control cadence, separate base/effective values, Off/Volume/Crossfade/Warp/Filter resolution and diagnostics; unfinished freeze selections rejected |
| `tests/pog3_granular_reference.h` | Five independent stereo pitch shifters using the existing Whammy/Harmonizer primitive; dry, six levels/pans, input gain, and final master |
| `tests/pog3_controls.cpp` | Persisted index contract, both target setters, mappings, morph ownership, rejection/clamping, Warp and eligibility behavior |
| `tests/pog3_quality.cpp` | Identity/delay/startup/drain/chunking/reset checks, isolated reference tuning, stereo/pan/gain checks, nonfinite/overflow rejection, optional WAV renders |
| `tests/pog3_pitch_quality.cpp` | Spectral tuning/spurs/leakage, resolved and ordinary low chords, alias rejection, track continuity, Focus reversal, staged identity/deadlines, callback partitioning, Warp, overload/drain, envelope latency, close-pair diagnostic |
| `tests/pog3_attack_quality.cpp` | Held/new notes, shared harmonics, low bass, re-plucks, arpeggios, bends, Focus reversals, activation, exact-off dry, reset, partition invariance, optional attack WAV renders |
| `tests/pog3_voice_stages.cpp` | AD timing/retrigger, held-tone/chord detector, sensitivity/re-plucks, filter transfer and resonance, routing eligibility, delay endpoints/queues, pan, rapid automation/partition/reset, gain/overload/recovery, optional full-path WAV renders |
| `tests/pog3_expression_quality.cpp` | Processor key/index publication, cadence, immutable ownership, exact endpoints/units, processed-dry exclusions, 30 Warp tuning cases, Filter/envelope interaction, 7-bit mode/reverse/Focus automation, callback partitions, configuration/reset and optional expression WAV renders |
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

The six baseline tests passed before the new code. All eleven currently selected
regression tests pass:

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

The registry retains all seven choices. This development processor **rejects
Freeze + Gliss and Freeze + Volume** in configuration and both target setters
until M7 implements their audio behavior; it does not treat either as Off.
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

## Next implementation milestone

M0's parameter/publication contract and M2's streaming identity gates are in
place. M1 supplies an audible reference, renders, and timing/allocation evidence;
its artifacts remain the comparison baseline for subsequent work.

M3–M6's software engine and core DSP quality gates are implemented. Target-device
admission, combined-chain endurance, and listening review remain open; the
effect is not release-ready merely because its host tests pass.

The next software milestone is **M7 — both freeze behaviors**. Capture all
three spectral representations, preserve held relative-phase evolution,
implement genuine frequency gliss toward newly captured targets, and enforce
heel/toe hysteresis, Focus/dry eligibility, recapture/clear/reset and 60-second
hold stability. Both rejected expression selections must become complete audio
behavior before exposing the block. Memory/CPU admission and M3/M4 fidelity
limits still require work; they must not be hidden by successful routing tests.

Complete M7 before M8's factory, catalog, inspector, scene, and manager
integration. Public parameters must not expose unfinished audio behavior.

The granular reference must remain a test harness. It cannot satisfy independent
polyphonic attack, spectral freeze, or gliss and must not become an undocumented
production fallback.
