# ERB carrier reconstruction cadence: CPU checkpoint

Measured 2026-10-07. The optional prototype now reconstructs all eight warm
stereo paths at 48 kHz while updating oscillator endpoints every 4/8/16/32
samples. A cheap full-rate winding counter preserves carrier cycles without
calling atan2 at every sample. The 32-sample candidate reduces wide-bank CPU
demand, but full POG3 admission remains open: ownership, independent Attack,
freeze and the remaining stages are not included, and polyphonic fidelity still
fails the existing screens. Production DSP and M8 are unchanged.

## Design and comparison contracts

The new `tests/pog3-library-trial/erb_cadence_bank.h` retains the
[shared reference's](pog3-erb-shared-cpu-results.md) 69-band grid, coefficients,
48 kHz non-decimated analysis, magnitude normalization, voice ratios and
heuristic source-tail taper. It shares analysis across six Warp-following paths
and two fixed upper paths. It does not supply two Focus resolutions/alignment,
Attack, freeze/gliss, post-filter, detune, spread or output calibration.

Five optional configurations expose the tradeoff:

| Backend | Endpoint interval | Cycle accounting | Oscillator state |
| --- | ---: | --- | --- |
| `erb-cadence-center-8` | 8 samples | Infer cycles from band center | Float |
| `erb-cadence-count-4` | 4 samples | Count full-rate crossings | Float |
| `erb-cadence-count-8` | 8 samples | Count full-rate crossings | Float |
| `erb-cadence-count-16` | 16 samples | Count full-rate crossings | Float |
| `erb-cadence-count-32` | 32 samples | Count full-rate crossings | Double |

The center-only version measures the cheapest endpoint assumption; it is
explicitly rejected. If a 400 Hz band passes a 5000 Hz carrier, 8-sample
endpoints incorrectly infer -1000 Hz. An independent synthetic carrier probe
shows its down-one output matches the folded trajectory instead of the intended
2500 Hz trajectory. Accurate tuning on a few complete-bank tone fixtures does
not excuse this demonstrated failure on a filter's finite off-band tails.

For counted variants, each meaningful analytic sample stores the previous
complex carrier. When its imaginary sign changes, the cross product determines
whether the principal angle wrapped at the negative-real axis. Unlike checking
only the new real part, this preserves large increments near Nyquist. Integer
turns plus the measured endpoint angles give the full unwrapped advance:

```text
advance = currentAngle - segmentStartAngle + 2*pi*turns
outputPhase[v] += ratio[v] * advance
```

At a control change, the advance since the previous segment is weighted by
the old ratios before installing the new ones. This avoids eight phase additions
on every sample while preserving ratio-increment integration even when controls
fall between endpoints. The first meaningful carrier sets the phase origin
immediately, rather than dropping cycles before its first interpolation endpoint.
Phase remains double precision and bounded; the original -400 dB phase-floor
choice remains. Note identity across real silence/cancellation is not established.

For a completed interval, amplitude and integrated phase are interpolated
between its endpoints. A full-rate real oscillator uses
`r[n+1] = 2*cos(step)*r[n] - r[n-1]`. Each interval reanchors it from the double
phase endpoints, preventing recurrence error from accumulating indefinitely.
The recurrence coefficient uses a degree-18 cosine series instead of the
linear table: coefficient error would otherwise grow near DC/Nyquist. Phase
anchors retain the prepared cosine table. The 32-sample version uses double
recurrence state to bound the longer interval's numerical error.

The hot sample function is inlined and endpoint/acquisition work is kept out
of line with GNU compiler attributes. No fast-math or architecture-specific
flags are introduced. All storage is fixed, and the shared math table is
prepared off the callback.

Reconstruction delays the signal by one interval: 0.083/0.167/0.333/0.667 ms
for 4/8/16/32 samples. That is additional delay, not total filter-bank latency.
Startup needs two non-silent endpoints, potentially up to two intervals before
the first emitted block. Filter group delay/ringing remains frequency-dependent.
Interpolation also spreads amplitude and phase changes within an interval.
The source-tail gain taper still uses the current control state while carriers
are delayed; moving-control gain alignment and alias transitions need further
work. Production control timestamps/latency are not claimed unchanged here.

## Independent DSP evidence

All three optional foundation CTests pass. Strict warnings and the cadence
foundation's ASan+UBSan/leak check pass. The original production DSP is unchanged
and its earlier suite results are not represented as a new admission run.

The numerical tests use known unwrapped carriers and independently integrated
phase histories. They include off-band frequencies, near-DC and near-Nyquist
inputs in both frequency directions, a +24 Nyquist oscillator step, silence/reset,
delayed startup, smooth chirp/Warp ramps and reversals, and an abrupt amplitude
step. Across counted variants, the worst constant-carrier absolute output error
is **2.50022e-6** at amplitude .2; interval 32 remains **1.86858e-6**.
A 7-sample silent prefix followed by a 21173.125 Hz carrier verifies that startup
keeps its many pre-endpoint cycles. The recurrence cosine's maximum absolute
error over 100001 points is **3.52908e-9**.

| Interval | Moving-phase endpoint error | Interior error vs delayed full-rate trajectory | Abrupt .2→.4 amplitude-step deviation |
| --- | ---: | ---: | ---: |
| 4 | 9.11182e-8 | 8.84504e-6 | .0944364 |
| 8 | 9.11182e-8 | 3.58700e-5 | .141654 |
| 16 | 9.11182e-8 | 1.36364e-4 | .108945 |
| 32 | 8.81974e-8 | 5.55541e-4 | .151311 |

The interpolation errors are intentional approximation measurements, not
production quality passes. The smooth interior error grows approximately with
interval squared. Abrupt envelope steps can differ substantially within their
interpolation interval even while settled tone tuning is accurate.

A two-pluck fixture (196 and 493.883 Hz, with different onset times) compares
the full bank against the accurate wide reference with the extra delay accounted for.
No per-fixture normalization is used. Aggregate error/reference power ranges
are **-68.35 to -50.33 dB** at interval 8, and **-54.80 to -41.36 dB** at interval
32 across the eight voices. The largest interval-32 difference is processed
unison. These observations are not listening approval or a quality gate for
real guitar DI. The complete per-voice values remain in the foundation log.

Stereo histories match separately processed channels exactly. Reset clears
delayed oscillators as well as analysis/phase state. All four counted intervals
are partition-invariant with controls applied at the same absolute 48-sample
positions; the 32-sample case exercises controls between endpoints.

## CPU and allocation evidence

Use the unchanged Release Ardor control and pinned dependency configuration
from the [library report](pog3-pitch-library-evaluation.md): i3-8100T, GCC 14.2,
ordinary -O3, no fast-math/architecture flags. Dense input is the same four-second
48 kHz stereo low-guitar chord plus deterministic noise. Callbacks are 64/128
samples, with warmup, reset and a measured run. Moving rows include control
segment flushes, ratio changes and gain-taper calculations.

No timing pass overlaps a build, foundation/sanitizer, quality or allocation
workload. The two passes run sequentially in opposite case order. Timing has
no allocation preload. Every callback/outlier is retained. Preliminary single invocations
that guided implementation are excluded; they predate the final cycle counter,
startup handling or endpoint/hot-loop split.

60 successful timing invocations retain 30 rows per pass. Ranges cover
both callbacks and both passes. All rows have eight stereo paths except the
explicitly smaller octave-up reference.

| Backend | Static mean period demand | Moving mean demand | Worst callback µs | Total overruns |
| --- | ---: | ---: | ---: | ---: |
| ardor | 27.193–27.305% | 27.749–28.673% | 2679.560 | 2 |
| erb-shared-lut-wide | 48.621–49.222% | 51.727–52.592% | 2632.200 | 1 |
| erb-cadence-center-8 | 34.937–35.389% | 36.254–36.970% | 2853.050 | 1 |
| erb-cadence-count-4 | 60.568–60.648% | 63.010–63.435% | 2156.090 | 0 |
| erb-cadence-count-8 | 37.120–37.692% | 38.642–39.037% | 2784.520 | 1 |
| erb-cadence-count-16 | 25.508–25.861% | 26.396–26.949% | 951.747 | 0 |
| erb-cadence-count-32 | 20.879–21.153% | 21.432–21.694% | 2910.130 | 1 |
| Original octave-up, **1 voice** | 3.053–3.067% | Not implemented | 140.649 | 0 |

Across matching cases, interval 32 lowers mean demand **56.49–59.25%**
compared with the full-rate wide table-math bank. This is a bank-to-bank
comparison, not a complete processor improvement.

| Interval-32 callback | Controls | Mean µs range | Highest p99 µs | Maximum µs | Overruns |
| --- | --- | ---: | ---: | ---: | ---: |
| 64 | Static | 278.382–282.036 | 306.355 | 2910.130 | 1 |
| 64 | Moving | 288.760–289.251 | 303.373 | 316.194 | 0 |
| 128 | Static | 558.874–561.155 | 746.418 | 1857.910 | 0 |
| 128 | Moving | 571.518–575.772 | 762.083 | 814.670 | 0 |

These are bank costs, not the complete effect. The unchanged Ardor control
includes its existing interpretation/ownership work; this prototype does not.
The original octave-up reference has only one stereo voice and is separately
labeled. The 25% **full-block** target cannot be declared met from a smaller
bank-only mean, and target-device/chain tails remain unproven.

Twenty independent allocation invocations cover all five cadence variants,
both callbacks and static/moving controls. All report **zero C allocation/free
calls and zero stream faults**. Preparation, warmup and reset precede the probe.
Core storage is **51144 bytes** at intervals 4/8/16, and **64392** at interval 32,
plus the one shared 24592-byte math table. Trial output buffers and future
ownership/Attack/freeze storage are excluded. Long silence/denormal tails and
AArch64 execution remain outside this dense-input checkpoint; the engine's
existing denormal handling is not modified.

## Settled audio screen and limitations

Twenty successful quality invocations retain 320 rows. All counted intervals
tune the settled 329.628 Hz sine within **0.041 cents** at every exposed voice.
The interval-32 +12 target level is approximately **-0.00062 dB**, its spur is
**-50.16 dBc**, and the +24 spur improves to **-43.26 dBc**. That +24 result still
misses the existing <-45 dBc gate. One tone is not a gain-flatness sweep, and
resolved/close-low chords still expose large losses and unwanted partials.

The interval-32 17003.2 Hz above-Nyquist fifth/+12/+24 screens give total residual
power of **-66.03/-55.06/-62.31 dB**, passing those individual <-50 dB cases.
The same heuristic center-plus-tail taper remains; complete frequency/ratio
and transient alias coverage is unvalidated. Existing metric contracts apply:
exact-frequency projection for gain, chord cents unmeasured, and total power
rather than out-of-Nyquist target projection for the high-frequency case.

The center-only method also looks tuned in the settled complete-bank screen,
but fails the independent cycle-count probe. It must remain a failed CPU
comparison, not a fallback selected by a favorable timing or single-tone result.

## Reproduction and continuation

Configure the optional project with the local paths in the library report, then
run these phases sequentially:

```sh
cmake --build build-pog3-library-trial -j2
ctest --test-dir build-pog3-library-trial --output-on-failure
python3 tests/pog3-library-trial/run.py build-pog3-library-trial/pog3-pitch-library-trial build-pog3-library-trial/erb-cadence-artifacts --phase quality --backends erb-cadence-center-8 erb-cadence-count-4 erb-cadence-count-8 erb-cadence-count-16 erb-cadence-count-32
python3 tests/pog3-library-trial/run.py build-pog3-library-trial/pog3-pitch-library-trial build-pog3-library-trial/erb-cadence-artifacts --phase allocation --probe build-ci/libpedal-pog3-malloc-probe.so --backends erb-cadence-center-8 erb-cadence-count-4 erb-cadence-count-8 erb-cadence-count-16 erb-cadence-count-32
python3 tests/pog3-library-trial/run.py build-pog3-library-trial/pog3-pitch-library-trial build-pog3-library-trial/erb-cadence-artifacts --phase timing --passes 2 --backends erb-cadence-center-8 erb-cadence-count-4 erb-cadence-count-8 erb-cadence-count-16 erb-cadence-count-32 erb-shared-lut-wide ardor erb-ps2
```

CSV/log/receipt artifacts remain under the ignored build directory:

| CSV | SHA-256 |
| --- | --- |
| `timing-1.csv` | `bf56e7954d50ae54798a8a6f26427a7e202743efd83c12e2d8925a7711a5a3d4` |
| `timing-2.csv` | `c2b0d7e0dc73f17e071e93bedb401c7f3b981fbc2faff64d45b598291253b26b` |
| `allocation-1.csv` | `e172df15a8d5c7c1df74c7c17c285be4a883063257a8f7232441f66ea153a33b` |
| `quality-1.csv` | `8d36b9d2f686928cca75422c00a50a18558936b2a21b7762f70d484f3d80b7a9` |

Retain the accurate shared bank and interval-8/32 counted candidates as controls.
The next CPU decision is to price the required note ownership, independent
Attack and held/live work with the candidate bank, including any extra analysis,
warm Focus path and alignment cost. Existing production analysis/Attack costs
cannot simply be omitted or assumed to fit in the remaining margin. Measure
combined demand before declaring full-block feasibility.

Before selecting a backend, compare transient/phase interpolation and alias
sweeps at intervals 16/32 or suitable band tiers, then resolve raw polyphonic
gain/spur quality with realistic plucks/DI material. Low CPU and accurate settled
tuning do not establish full POG3 behavior. No factory/catalog integration or
production fallback is added by this checkpoint.

## Subsequent Attack/freeze checkpoint

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

## Subsequent hardware comparison

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
