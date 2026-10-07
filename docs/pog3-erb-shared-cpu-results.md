# Shared ERB bank: eight-path CPU experiment

Measured 2026-10-07. The optional ERB prototype now shares analytic analysis and
magnitude across processed unison, five shifted stereo voices and two warm
upper variants. It implements phase-continuous downward shifts, the true
equal-tempered fifth and moving Warp. **The measured implementations do not
meet the CPU goal**, before independent Attack, freeze, filter and space work.
Production DSP is unchanged; M8 remains blocked on admission.

## What is implemented

`tests/pog3-library-trial/erb_shared_bank.h` adds two analysis ranges, each with
accurate and table-based phase math. All coefficients, histories and gains use
fixed arrays. Both stereo channels have independent filter and phase histories.

| Backend | Bands / source centers | Phase math |
| --- | --- | --- |
| `erb-shared-43` | Original 43-band grid, approximately 80 Hz–4 kHz | Standard-library atan2/cos |
| `erb-shared-wide` | Extended 69-band grid, 35.4829–20019.8 Hz | Standard-library atan2/cos |
| `erb-shared-lut-43` | Original grid | Interpolated angle/cosine tables |
| `erb-shared-lut-wide` | Extended grid | Interpolated angle/cosine tables |

The original grid is a limited-bandwidth analysis-sharing control. The wide grid
extends its ERB coordinate to `2.5 + k*4/6`, k=0…68, retaining the ratio-2
bandwidth rule and pole construction. This is an experimental coverage change,
not the thesis's ERB-PS2 specification or validated full-band tuning. See
[the untouched octave-up reference](pog3-erb-ps2-reference-results.md) for the
published design, explicit analytic-gain normalization and source attribution.

Each band computes its analytic signal and accurate magnitude once per sample.
Eight double phase histories integrate the principal input phase increment
multiplied by the current ratio. Downward roots thus retain branch continuity
through phase wraps. The fifth uses `2^(7/12)`, rather than translating each
filter center. Moving ratios scale increments, avoiding the erroneous extra
term from multiplying a growing phase by a changing ratio. Phases stay bounded
by two explicit reductions, which also allow vectorization across voices.

At an analytic magnitude below 1e-20 (-400 dB), phase is treated as unmeasurable;
the next carrier reacquires from its principal phase. This is a DSP phase-floor
choice. It does not establish coherent note identities through silence or the
more difficult near-cancellation cases in polyphonic material.

The eight paths are `[0, -24, -12, +7, +12, +24, +12, +24]` semitones. Indices
0…5 follow Warp and indices 6/7 stay fixed, matching production's six long
paths and two short upper paths. All eight phase/render histories remain warm,
even where an output contribution is masked. There is no Focus alignment/mix,
Attack, freeze, gliss, output gain correction, filter, detune, spread or pan.
Eight same-analysis paths also do not reproduce production's two resolutions.

The initial library helper had the upper assignment reversed: it kept indices
4/5 fixed and moved 6/7. This commit corrects that helper. Earlier dynamic
library CSVs are retained as historical results of their original mapping;
static rows and the current Ardor control are unaffected. The
[library report](pog3-pitch-library-evaluation.md) now records that limitation.

## Bounded table math

The accurate baseline uses an absolute atan2 phase and unwraps consecutive
differences. The table variant approximates that **absolute** angle, then takes
the same difference. For a constant ratio, input angle errors telescope instead
of accumulating a frequency bias from an approximate per-sample increment.
Moving ratios still need an independent oracle, which is included below.

An atan table has 1024 intervals on [0,1] with quadrant reconstruction, and the
cosine table has 4096 intervals per turn. Linear interpolation uses double
angle/phase arithmetic and float cosine entries; magnitude remains accurate.
Preparation computes the tables once off the callback. The immutable shared
table object occupies 24592 host bytes, separately from each bank.

Foundation results:

| Check | Worst observed absolute error |
| --- | ---: |
| Angle table, 500001 points over a turn | 7.74284e-8 radians |
| Cosine table, same grid | 3.222e-7 |
| Accurate phase output vs independent unwrapped oracle | 8.11928e-8 |
| Table phase output vs same oracle | 1.1016e-7 |
| Shared static +12 vs original reference | 0 |
| Table wide-bank output vs accurate bank during automation | 4.47035e-8 |

The carrier oracle includes downward branch crossings, an off-center fifth,
ratio steps, two Warp ramp reversals and near-Nyquist input increments. It
integrates known unwrapped input increments independently of implementation
histories. This is numerical coverage, not listening approval or a complete
dynamic polyphonic pitch-quality matrix.

Separate-channel sustained tones match stereo processing exactly. Reset clears
every history. Both grids and the table wide variant give identical output
under arbitrary callback partitions with controls applied at the same absolute
48-sample positions. All prepared wide-grid float poles remain stable.
Both optional foundation CTests, strict warning checks and the shared foundation
ASan+UBSan/leak check pass. No production CTest is changed by this standalone
project.

## CPU and allocation evidence

The host/compiler, pinned dependencies and unchanged Ardor Release control are
those in the library report: Core i3-8100T, GCC 14.2, ordinary -O3, no fast-math
or architecture flags. The trial processes the same four-second 48 kHz stereo
low-guitar chord plus deterministic noise, with warmup/reset and 64/128 callbacks.
Moving rows include callback-rate Warp updates and their gain-taper calculations;
the production bank retains its own additional smoothing and snapshots.

Two complete passes run sequentially in opposite case order, after all builds,
foundation/sanitizer, quality and allocation jobs finish. Uninstrumented timing
does not preload the allocation probe. Every callback and outlier is retained.
Single preliminary invocations used while choosing phase math are excluded from
the final tables. Their implementations predate the final bounded reductions.

48 successful timing invocations retain 24 rows per pass. The ranges below
cover both callbacks and both passes. All rows use eight warm stereo paths
except the explicitly smaller references.

| Backend | Static mean period demand | Moving mean demand | Worst callback µs | Total overruns |
| --- | ---: | ---: | ---: | ---: |
| ardor | 27.134–27.343% | 27.756–28.071% | 8949.800 | 2 |
| erb-shared-43 | 68.343–69.096% | 79.638–80.330% | 3536.860 | 6 |
| erb-shared-wide | 114.337–117.912% | 133.410–133.853% | 9600.920 | 18000 |
| erb-shared-lut-43 | 28.333–28.401% | 29.245–29.648% | 2029.050 | 0 |
| erb-shared-lut-wide | 48.628–49.892% | 51.614–51.945% | 3998.430 | 6 |
| ERB octave-up, **1 voice** | 3.067–3.078% | Not implemented | 214.547 | 0 |
| Terrarium 80, **3 voices** | 7.519–7.543% | Not implemented | 219.829 | 0 |

Per-callback tails for the table variants (ranges/maximum over both passes):

| Backend | Callback | Controls | Mean µs range | Highest p99 µs | Maximum µs | Overruns |
| --- | ---: | --- | ---: | ---: | ---: | ---: |
| erb-shared-lut-43 | 64 | Static | 377.774–377.993 | 396.460 | 518.149 | 0 |
| erb-shared-lut-43 | 64 | Moving | 391.378–395.311 | 446.541 | 880.672 | 0 |
| erb-shared-lut-43 | 128 | Static | 757.285–757.349 | 786.188 | 2029.050 | 0 |
| erb-shared-lut-43 | 128 | Moving | 779.869–780.036 | 805.565 | 935.965 | 0 |
| erb-shared-lut-wide | 64 | Static | 649.660–651.298 | 675.504 | 1425.830 | 1 |
| erb-shared-lut-wide | 64 | Moving | 692.203–692.599 | 731.027 | 1498.070 | 1 |
| erb-shared-lut-wide | 128 | Static | 1296.740–1330.460 | 1754.370 | 3998.430 | 4 |
| erb-shared-lut-wide | 128 | Moving | 1376.380–1379.530 | 1425.770 | 1637.870 | 0 |

These are pitch-bank costs. The ERB variants omit the remaining POG3 stages;
there is no basis for full-block or target-device admission. Current Ardor runs
eight internal warm paths and publishes six mixed voices. The octave-up and
Terrarium references instead run only one and three fixed stereo voices, with
different bandwidths. They are controls, not equivalent-feature speedups.

Sixteen separate allocation invocations cover both callback sizes, static and
moving controls for all four shared variants. Every row reports zero C
allocation/free calls and zero stream faults. Preparation, warmup and reset
precede this observation. Core storage is 13336 bytes for 43 bands and 21344 for
69 bands; the table variants additionally reference the one shared table object.
Trial output buffers and future Attack/freeze/mixing storage are excluded.

## Audio observations that constrain later CPU work

Sixteen quality invocations produce 256 retained rows. All variants tune the
settled 329.628 Hz sine within 0.041 cents at every exposed interval. Table
math preserves those results. The wide bank improves the single-tone +12 target
level from about -12.69 dB to -0.0052 dB, and its +12 spur from -42.59 to
-52.62 dBc. That is one frequency, not a response-flatness result.

The wide +24 spur is still about -38.17 dBc, missing the <-45 dBc tone gate.
Resolved-chord and close-low-pair results retain substantial unwanted partials
and unequal gains. For example, the wide +12 resolved chord has a strongest
unwanted peak of -7.54 dBc relative to one target's local peak, and target gains
span -19.54 to +0.50 dB. Widening the range has not solved polyphonic fidelity.
The [original metric contracts](pog3-pitch-library-evaluation.md) still apply:
exact-frequency projection measures target gain, chord cents are unmeasured,
and unwanted peaks exclude every expected chord-partial region.

At each control update, a heuristic cosine taper uses
`ratio*(center+4*bandwidth)`, retaining full gain below 18 kHz and zero above
20 kHz. It does not prove rejection of finite filter tails, within-band
modulation products or arbitrary transient aliases. The wide 17003.2 Hz +12/+24
screen gives total residual power of -56.42/-63.07 dB, respectively, passing the
<-50 dB screen for those two cases. The fifth's above-Nyquist case is -66.03 dB.
This is not a frequency/ratio sweep. High-frequency processed unison/downward
gain is also attenuated by this experimental bank.

No scalar zero-latency claim is made: the CSV's zero field indicates no added
wrapper buffering, while group delay/ringing is frequency-dependent. Transient
response, realistic plucks/DI audio, complete gain/alias sweeps, note ownership
and held/live behavior remain unvalidated. Long silence/decaying-denormal CPU
tails and AArch64 execution are also outside this dense-input checkpoint.

## Reproduction and next decision

Use the standalone configuration in the library report, then execute phases
sequentially:

```sh
cmake --build build-pog3-library-trial -j2
ctest --test-dir build-pog3-library-trial --output-on-failure
python3 tests/pog3-library-trial/run.py build-pog3-library-trial/pog3-pitch-library-trial build-pog3-library-trial/erb-shared-artifacts --phase quality --backends erb-shared-43 erb-shared-wide erb-shared-lut-43 erb-shared-lut-wide
python3 tests/pog3-library-trial/run.py build-pog3-library-trial/pog3-pitch-library-trial build-pog3-library-trial/erb-shared-artifacts --phase allocation --probe build-ci/libpedal-pog3-malloc-probe.so --backends erb-shared-43 erb-shared-wide erb-shared-lut-43 erb-shared-lut-wide
python3 tests/pog3-library-trial/run.py build-pog3-library-trial/pog3-pitch-library-trial build-pog3-library-trial/erb-shared-artifacts --phase timing --passes 2 --backends erb-shared-43 erb-shared-wide erb-shared-lut-43 erb-shared-lut-wide ardor erb-ps2 terrarium-80
```

The ignored artifact directory retains CSVs, logs and per-invocation receipts.

| CSV | SHA-256 |
| --- | --- |
| `timing-1.csv` | `ebd91be513a7bb61cccd505e4c1e44b28af1af8a89716f0481f2279c4013c4ff` |
| `timing-2.csv` | `5607f0cd8d1d50ee58f19da936cdf8899bbc65165f04225017ba5f9fee426553` |
| `allocation-1.csv` | `0d17daf08331d66337003b6fd5e984494bfafbd02163dd2416c9f5c40cf505ad` |
| `quality-1.csv` | `fa1073cb4850faa0f9dd90362f045dcb811280fddc74c56b0638a5d5dd440228` |

Adding generic full-rate phase voices does not preserve the original single
octave-up CPU advantage. Table math substantially reduces their cost with small
measured numerical error, but neither tested range reaches the full 25% goal
even before required feature work. Keep these implementations as numerical/CPU
controls; do not integrate them into production or spend the next iteration
on Attack/freeze/catalog work.

The next CPU candidate is reduced-rate **baseband** phase/magnitude estimation
with full-rate carrier reconstruction, or multirate band tiers with equivalent
output coverage. Simply decimating the real carrier loses phase-wrap information
and can alias upward voices. A new prototype must preserve center-cycle count,
ratio-increment integration and stereo/control ownership, account for all
resampling/oscillator work, and compare against this accurate baseline through
crossing tones, transients, Warp and alias sweeps. Fixed-integer carrier algebra
may also avoid generic phase work, but static-only savings cannot substitute for
the required moving-ratio workloads. Resolve CPU feasibility before broadening
production integration.
