# ERB Attack/freeze: combined CPU checkpoint

Measured 2026-10-07. This optional prototype adds the existing spectral note
ownership and Attack model, and freeze/gliss capture/assignment, to the
32-sample counted ERB bank. It is an isolated CPU and DSP experiment;
production DSP, public integration and M8 remain unchanged. Combined freeze/gliss
costs 66.61–70.14% host demand overall and 82.23–87.09% in the held segment, with
frequent callback overruns. A hardware comparison is now the next CPU decision;
the 25% planning target is not a prerequisite for running it. The subsequent
[Pi 4 comparison](pog3-pi4-cpu-results.md) is now complete: the full FFTW
processor and ERB hybrid both exceed measured standalone capacity.

## What is implemented

`tests/pog3-library-trial/erb_attack_freeze.h` prepares the actual production
2048/256, 1024/128 and low-limited 4096/512 stereo analysis/PitchFrame paths.
The right transforms remain deferred by one sample. Low ownership updates
precede primary updates, short updates are staged eight samples after their
boundary, and freeze consumes completed pairs nine samples after a primary
boundary. All analysis and all eight live ERB paths continue during holds.
The 1024-resolution ownership path is priced even though two different live
ERB Focus resolutions and their alignment have not been implemented.

The existing `PolyphonicAttack` supplies persistent tracked-note excitation
histories and linked stereo ownership. Its gains are projected into each ERB
band by the note's predicted band power. The projection uses both positive and
negative real-input transfer terms, the prepared double coefficient designs,
and the existing 200–300 Hz low/primary crossover. Primary and short ownership
have separate gain maps for the six Warp-following and two fixed upper paths.
Gain calculation runs at frame cadence; output multiplication runs at sample
cadence. Attack=0 retains exact raw cadence output in the tested moving-control
fixture. No broadband input envelope is substituted for note ownership.

This projection still gives a band only **one gain**. It cannot separate
coincident/close carriers within that band, and has demonstrated close-low-note
failures. Frame-center envelope dates also do not align the fast ERB stream's
frequency-dependent delays. This is not the completed independent-note Attack
contract, even when the resolved-note example passes.

The existing `SpectralFreeze` supplies heel/toe hysteresis, capture validation,
held IDs, target assignment, gliss and volume/dry eligibility. The new held
renderer sums fixed-capacity tonal carriers alongside the warm live bank.
Each of eight stereo paths keeps up to 256 primary-or-short plus 256 low
partial slots; zero crossover/alias weights avoid oscillator work while their
phase histories continue. These are conservative eight-path held costs;
production currently renders held audio on its six long paths while keeping
two short live paths warm. Every 32 samples, a double real recurrence is
reanchored from bounded double phase. Phase integrates the preceding frequency
interval rather than resetting at each target or Warp change. Amplitude ramps
across the next interval. Control/frequency changes take effect at the next
32-sample endpoint; this is quantization, not continuous sample-accurate gliss.

Captured phase is extrapolated from each source center at **input** frequency
before output transposition begins. An independent two-window-center fixture
fails the earlier shifted-frequency seed at 0.3972 absolute error; the corrected
seed measures 1.95622e-6 worst error, preventing that demonstrated crossover cancellation. Matching it to the live
ERB per-band transposition offsets is unresolved, so the 20 ms live/held fade
does not prove coherent capture/release. Volume is applied once; processed dry
uses the freeze model's eligibility mix. The source-pitch crossover is shared
between primary/short and low held carriers. Upward held frequencies at/above
Nyquist are immediately muted; the 18–20 kHz taper is still a heuristic, and
moving alias/slot-removal transitions need separate validation.

## DSP evidence and remaining fidelity limits

All four optional Release foundations pass. The previous reference/shared/cadence
suites are unchanged; the final feature suite passes in 11.43 s after the seed
correction. The all-four rerun after adding the device build passed in 16.37 s;
the final ARM-discovered exact-off correction then passed the feature suite in
11.39 s. The new suite checks exact Attack-off output,
resolved-note onset behavior, actual stationary held audio under replacement
input, silence/startup/reset, heel release, gliss carrier motion and toe latch,
Volume/dry eligibility, and arbitrary partitions with absolute 48-sample Warp
controls. Strict warnings pass for both changed C++ units. The feature suite also passes
ASan+UBSan/leak checks using its explicit `--short-hold` option: only the
held-renderer duration is shortened to four seconds; other feature fixtures
are retained. The two-minute recurrence check is a Release result.

A two-minute **held-renderer-only** comparison against independently known
unwrapped phase measures 2.02735e-6 worst absolute output error at amplitude .2.
An independently integrated moving-frequency/Warp fixture, including changes
between endpoints and movement through the low/primary crossover, measures
2.03964e-6. These bounds validate the renderer's documented endpoint-quantized
trajectory, not a continuous-control or complete-processor long-hold contract.
The above-Nyquist synthetic held cases emit exact zero on the upward paths.

For 196 Hz held plus a new 493.883 Hz note and .5 s Attack, all eight paths
retain the established note within 0.094 dB, suppress the new note by
27.20–49.30 dB in the measured early window, and settle within 1e-6 dB of the
raw control in the measured late window. The early window starts 1536 samples
(32 ms) after input onset and lasts 2400 samples (50 ms); it is **not** a claim
about the first 50 ms after onset or measured complete wet latency. The held
projection window lasts 4800 samples at the same start; the late window starts
32000 samples after onset and lasts 12000 samples. Values compare with the raw
bank and do not certify its existing pitch/chord fidelity.

The 82.4069/87.3071 Hz example is retained as a failed observation: its direct
projection is not sufficiently separated to certify individual close-note
levels. It shows approximately 4.05–4.15 dB apparent held-note ducking, only
3.28–3.61 dB early attenuation, and 7.03–12.50 dB apparent late level loss.
Those figures are not production gates; this case remains unadmitted. The
previous raw chord and +24 spur failures also remain.

A shifted live ERB path already seeds phase as ratio times principal carrier
angle. It therefore does not generally preserve input interchannel phase,
including anti-phase input. This is inherited from the pitch prototype and is
not fixed by Attack. Processed-unison anti-phase deviation is 3.93e-7 in the
resolved test. Early left/right target projections differ by up to 6.98 dB in
the down-two output; such projections include the existing shifted-phase and
unwanted-partial behavior. No generated-voice stereo phase approval is claimed.

For stationary freeze of 196 Hz under replacement 493.883 Hz input, all eight
held paths retain target level within 0.000073 dB and replacement projections
below -107 dB. Held unequal anti-phase channels meet the 2e-5 absolute bound.
Gliss of the down-one path passes through 123.240 Hz before settling/latching
at 146.833 Hz, with one target assignment and one capture. Its intermediate
moving carrier exceeds the two endpoint projections by the tested 3x bound.
Freeze Volume at q=.25/.75 yields .0250002/.0750006 amplitude from .1 input;
processed dry is held only when eligible. This is synthetic tonal evidence;
real DI, capture/release cancellation, lost/new target discontinuities, noise
freeze, complete alias sweeps and target-device endurance remain open.

## CPU measurement contracts

Use the same ordinary Release configuration and host as the
[cadence checkpoint](pog3-erb-cadence-cpu-results.md): GCC 14.2, i3-8100T,
48 kHz, 64/128-sample callbacks, no fast-math/architecture flags. Warmup/reset
precede each four-second measured invocation. These retained host timings precede the ARM-discovered exact-off correction:
zero Attack now selects the raw template specialization to avoid ARM contraction
rounding differences. Active-stage benchmark controls still use .5 s Attack;
no post-correction host timing claim is made. Final timing passes run
sequentially in opposite case order, with no other build, test, allocation or
sanitizer workload running. Allocation probes run separately; their timings
are not used for CPU comparisons. Every final callback/outlier is retained. The interrupted pre-seed-correction
timing pair and its allocation data remain in
`build-pog3-library-trial/erb-attack-freeze-preseed-artifacts`; they are excluded
from the final corrected checkpoint. Preliminary standalone invocations are
also excluded.

The original dense chord/noise fixture is unchanged for `static`/`dynamic`.
Moving Warp is supplied once per callback using the existing benchmark's
smoothed control sequence; the arbitrary-partition DSP check separately uses
absolute 48-sample controls. New `events` rows feed the same input to every
backend: alternate the original low chord and 110/164.8138/220/293.6648 Hz every
half second, with a 10 ms linear rise and exponential decay on chord and noise.
The event case has fixed Warp, and explicitly exercises freeze/gliss assignment.

`erb-effects-ownership-32` runs all three analysis/PitchFrame paths and the
actual zero-second ownership updates, without a live gain map or freeze work.
`erb-effects-attack-32` adds .5 s Attack and both live gain maps.
`erb-effects-freeze-32` adds the freeze state/held renderer: heel for the first
second, capture/latch at q=1 for seconds 1–3, then heel release for the last
second. `erb-effects-gliss-32` instead captures at q=.65 without toe latch, so
new onsets can assign/move held targets. Both retain live analysis, Attack and
ERB work throughout. Source histories and freeze state are reset before the
measured/probed run.

The bare cadence control has eight paths. The unchanged Ardor bank control
also executes all six long plus two short paths, including its existing
zero-second ownership; it does not enable .5 s Attack or freeze. It is a
historical pitch-bank comparison, **not a matching full-feature control**.
Feature-stage differences within the new ERB variants provide the added-cost
comparisons. All bank outputs are used in the checksum; counter diagnostics
verify warm analysis and the intended capture, and retain capacity fallbacks.

CSV segment means cover the first second, middle two seconds and last second
without discarding transitions. `latency_frames=32` reports only the ERB's
added reconstruction delay; full filter/ownership/held latency is unmeasured.
Neither a feature prototype nor these partial stage costs includes the full
post-filter, detune/spread, output, two-resolution live Focus or alignment cost.

## Results

The corrected checkpoint contains **72 successful timing invocations** (36 per
opposite-order pass) and **24 separate allocation invocations**. All allocation
cases observe zero C allocation/free calls and zero reported processing faults.
The timings below exclude allocation instrumentation. Ranges span both callback
sizes and both passes; overruns sum twelve runs per backend, rather than one run.

| Backend | Static mean | Moving mean | Event mean | Highest p99 µs | Worst callback µs | Overruns |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| ardor | 27.331–27.888% | 27.803–28.513% | 27.286–27.705% | 1029.300 | 2904.400 | 2 |
| erb-cadence-count-32 | 23.398–23.693% | 23.915–24.256% | 23.433–23.687% | 684.950 | 2645.360 | 2 |
| erb-effects-ownership-32 | 30.781–31.293% | 31.403–32.094% | 30.971–31.416% | 1100.890 | 2481.340 | 2 |
| erb-effects-attack-32 | 49.279–49.441% | 49.838–50.435% | 49.360–51.128% | 1992.130 | 2952.040 | 27 |
| erb-effects-freeze-32 | 66.607–67.473% | 68.584–69.585% | 66.645–66.884% | 2748.240 | 4521.690 | 2395 |
| erb-effects-gliss-32 | 66.774–67.504% | 68.427–70.141% | 66.607–66.841% | 2648.220 | 5440.820 | 2363 |

| Backend | First-second mean demand | Middle-two-second mean demand | Last-second mean demand |
| --- | ---: | ---: | ---: |
| erb-effects-ownership-32 | 30.631–31.954% | 30.761–32.116% | 30.753–32.191% |
| erb-effects-attack-32 | 49.060–50.270% | 49.339–51.791% | 49.360–50.855% |
| erb-effects-freeze-32 | 50.307–51.783% | 82.234–87.092% | 51.317–52.683% |
| erb-effects-gliss-32 | 50.261–54.951% | 82.328–86.643% | 51.346–52.326% |

The middle two seconds include capture/fade transitions and fully held work.
They show why the four-second mean understates held demand: freeze/gliss reach
**82.23–87.09%** mean period demand in that segment. Frequent 64-frame overruns
remain (for example, approximately 386/3000 in individual freeze cases). These
cannot all be dismissed as isolated host scheduling outliers.

Matched-case increments are **7.159–7.983 percentage points** for ownership
above the bare bank, **18.011–19.969 points** for active Attack plus gain mapping
above zero-second ownership, and **15.756–19.480 points** for freeze above Attack.
The last difference includes freeze state, capture, fade and held rendering;
it is not an isolated oscillator cost. These hybrid stages do not demonstrate a
CPU advantage over the existing production implementation.

The current executable's bare control costs **23.398–24.256%**, compared with
20.879–21.694% in the preceding cadence checkpoint. The cause of that difference
has not been isolated. Current stage increments use the matching current control;
they do not subtract the earlier cheaper measurements.

All feature cases perform **5247 analysis transforms** in the measured four
seconds. Freeze/gliss capture exactly once; event gliss assigns four targets,
while stationary gliss controls assign none. Ownership capacity fallback events
are retained: zero-second ownership records 4110 dense / 4894 event occurrences,
and active stages record 26722 dense / 23400 event occurrences, with 16 families.
`faults=0` does not mean ownership capacity never overflowed. Maximum source held
partial slots across the six source bands are 450 for dense input, 462 for event
freeze and 477 for event gliss; these are not voice-replicated oscillator counts.

Artifacts remain under `build-pog3-library-trial/erb-attack-freeze-artifacts`.

| CSV | SHA-256 |
| --- | --- |
| `timing-1.csv` | `c94e6e1cd05ae0e59a1e138b822d70b4ddba5c34e05c72291e9a7cde25c7c95f` |
| `timing-2.csv` | `4efef9ba273af64ecc5ab79212e04da7601a6079a0d67a2ccc5a5e635d7f3580` |
| `allocation-1.csv` | `fd642a27e81e9f3526236908153daeed8acb2e9ece28321e3ae556ca7a4698c7` |

## Hardware decision and next work

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

The user clarified that **25% is an initial chain-budget planning target, not a
universal usability limit or a prerequisite for hardware testing**. A larger
block budget can be sensible for a shorter intended chain. Host percentages do
not convert directly into Pi percentages. Test hardware now rather than spending
more effort trying to satisfy that heuristic on the desktop.

Compare the existing full FFTW processor and this ERB hybrid on the actual pedal
at 48 kHz / 64 and 128 frames, with stereo dense input, moving controls, capture,
held work and target assignment. Retain means, tails, overruns, temperature,
clock, affinity, counters and service restoration receipts. The standalone
[device build and runner](../tests/pog3-library-trial/device/) use the same
benchmark sources without optional pitch-library dependencies. The production
full-suite fixture differs from the ERB stage fixture; report them separately.
Offline timing measures DSP capacity, not ALSA xruns or complete-chain usability.

If a configuration has adequate standalone margin, measure the intended
NAM/cabinet/delay/reverb chain and a ten-minute thermal/xrun soak. A supported
chain should retain observed deadline margin (initially 20%) without xruns.
Document the supported configuration; do not silently enlarge buffers or reduce
other blocks' quality. A target result above 25% alone does not reject it.

ERB fidelity work still includes close-note independence, live stereo phase,
capture/release coherence, moving aliases, lost/new targets, live Focus alignment
and realistic pluck/DI calibration. The hardware comparison guides whether to
optimize this hybrid, retain the production FFTW path or investigate another
architecture. No public backend selection follows from these host means.

## Reproduction

Configure with the paths in the [optional library report](pog3-pitch-library-evaluation.md),
then build and run the four standalone CTests. The event workload is opt-in;
the original default library-screen case count is unchanged.

```sh
cmake --build build-pog3-library-trial -j2
ctest --test-dir build-pog3-library-trial --output-on-failure
python3 tests/pog3-library-trial/run.py build-pog3-library-trial/pog3-pitch-library-trial build-pog3-library-trial/erb-attack-freeze-artifacts --phase allocation --probe build-ci/libpedal-pog3-malloc-probe.so --backends erb-effects-ownership-32 erb-effects-attack-32 erb-effects-freeze-32 erb-effects-gliss-32 --workloads static dynamic events
python3 tests/pog3-library-trial/run.py build-pog3-library-trial/pog3-pitch-library-trial build-pog3-library-trial/erb-attack-freeze-artifacts --phase timing --passes 2 --backends erb-cadence-count-32 erb-effects-ownership-32 erb-effects-attack-32 erb-effects-freeze-32 erb-effects-gliss-32 ardor --workloads static dynamic events
```

The fixed feature-bank object is **984096 bytes**, plus the shared 24592-byte
math table. This excludes all spectral/PitchFrame vector storage, shared
interpolation/window objects, FFTW allocations and trial output buffers. No
complete heap or target-device memory admission is claimed.
