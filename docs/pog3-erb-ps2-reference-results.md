# ERB-PS2 octave-up reference: CPU and DSP checkpoint

Measured 2026-10-07. The isolated reference costs **3.062–3.121% of the host
callback period for one stereo +12 voice**. This justifies measuring a shared
multi-voice extension. It does not establish full POG3 feasibility: raw output
gain, spurs and close-note balance still fail the audio screen. The production
FFTW processor is unchanged, and M8 public integration remains blocked.

## Reference design and its limits

The source is Etienne Thuillier's 2016 thesis,
[Real-Time Polyphonic Octave Doubling for the Guitar](https://aaltodoc.aalto.fi/items/6fd2cfc9-db61-4b0a-b11f-ea8c2925509f),
especially sections 5.1–5.2 and 6.3–6.4, table 5, figure 15, equations 37–40
and table 6. The supplied PDF was read in full for the relevant technical and
evaluation sections. The companion site supplies
[reference audio](https://research.spa.aalto.fi/publications/theses/thuillier_mst/),
but no Matlab implementation was available there. This is an implementation of
the published design with an explicit normalization choice, not a claim of
sample-identical reproduction of the author's recordings.

`tests/pog3-library-trial/erb_ps2_reference.h` implements 43 non-decimated complex
bands at 48 kHz. Coefficients are prepared in double precision. For band k=0…42:

```text
z_k       = 5 + k * (4/6)
target_k  = 228.7 * (10^(z_k/21.3) - 1)
fc_k      = target_k / 2
BW_k      = (24.7 + 0.108 * target_k) / 12
```

These are the paper's qERB=6, qC=4, leftmost target ERB=5 and width=28 setup
for ratio 2. Source centers cover approximately 80 Hz–4 kHz; target centers
cover approximately 160 Hz–8 kHz. This limited coverage must be revisited for
other ratios and a full-band effect.

The table-5 real bandpass prototype uses half the desired Q. Its two conjugate
poles are mapped to a repeated positive-imaginary pole as in figure 15:

```text
q  = fc / (2 * BW)
K  = tan(pi * fc / 48000)
D  = K*K*q + K + q
a1 = 2*q*(K*K - 1) / D
a2 = (K*K*q - K + q) / D
p  = -a1/2 + i * sqrt(a2 - a1*a1/4)
H(z) = B * (1 - z^-2) / (1 - p*z^-1)^2
```

Zeros at DC and Nyquist are retained. B is chosen so the complex response has
magnitude 2 at fc, the conventional real-to-analytic amplitude normalization.
This choice is explicit because the thesis's Matlab gain code was unavailable;
it does not normalize the final summed bank or remove its crossover cancellation.
This repeated-pole bandpass differs from Terrarium's modulated lowpass design.

Two cascaded first-order sections use the same rounded float pole. This retains
the prepared pole's stability rather than independently rounding repeated-pole
second-order coefficients. Input numerator history and both complex section
histories are independent for each stereo channel. The +12 output is summed as
`(real(z)^2 - imag(z)^2) / abs(z)`, using double intermediates and an accurate
square root. Exact silence contributes zero. There is no approximate reciprocal
root, decimation, adaptive bandwidth, fixed gain compensation or per-fixture
normalization.

The benchmark adapter has no added block buffer. Its CSV `latency_frames=0` is
a placeholder, **not zero audio latency**: filter group delay and ringing vary
with frequency. The thesis's roughly 3 ms high-band response is not a bass or
full-bank latency guarantee. This checkpoint does not measure onset/decay delay.

## CPU and allocation evidence

The optional standalone trial uses the same pinned inputs, host and Release
configuration as the [library screening report](pog3-pitch-library-evaluation.md):
Intel Core i3-8100T, GCC 14.2, ordinary `-O3 -DNDEBUG -std=c++20`, no fast-math
or architecture flags. No dependencies enter the normal Ardor build.

Each workload has four seconds of 48 kHz stereo low-guitar chord plus
deterministic noise, a warm run, reset, and a measured run. Callback periods are
1333.333/2666.667 µs at 64/128 samples. Builds, quality checks and allocation
observations finish before timing. Two passes execute sequentially with the
second case order reversed. No slow callbacks are removed.

| Pass | Callback samples | Mean µs | Mean period demand | p99 µs | Maximum µs | Overruns |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 64 | 40.9735 | 3.07301% | 43.688 | 97.990 | 0 |
| 1 | 128 | 83.1099 | 3.11662% | 113.895 | 327.912 | 0 |
| 2 | 128 | 83.2296 | 3.12111% | 97.387 | 280.639 | 0 |
| 2 | 64 | 40.8214 | 3.06161% | 43.725 | 51.352 | 0 |

All 16 timing invocations succeed. The same passes retain the unchanged Ardor
bank and the 80-band Terrarium-derived reference. Ardor runs eight warm stereo
paths, static and moving Warp, at 27.131–28.153% mean demand; one retained
64-sample callback overruns at 1716.62 µs. Terrarium runs only three fixed stereo
octaves at 7.493–7.643%, with no observed overrun. ERB-PS2 runs **one** stereo
octave-up voice. Different voice counts, bandwidth and feature coverage prevent
treating these ranges as a full-processor speedup comparison.

Separate glibc C allocation/free observations report **zero allocations and
zero frees** in both callback-size workloads. Preparation, warmup and reset
precede the observation. The core owns only fixed arrays; its measured host
`sizeof` is **3632 bytes**, including double design values. That excludes the
trial adapter/output buffers and all future multi-voice, Attack/freeze/control
storage. It is not complete processor memory admission.

## Audio screen: tuned, but not admitted

Four successful quality invocations produce eight rows. Metrics use the existing
trial's contracts: a settled 329.628 Hz sine for peak tuning, a two-second Hann
projection at the exact requested frequency for level, and a 65536-point Hann
FFT for unwanted peaks. Chord tuning remains unmeasured; its near-target peaks
are not a reliable tuning estimator. Levels are relative to each fixture's
input partial amplitude. Metrics below concern the left channel; the foundation
test separately verifies independent stereo histories.

| Fixture | Requested output Hz | Target level dB | Strongest unwanted peak dBc |
| --- | ---: | ---: | ---: |
| Single tone | 659.256 | -12.6913 | -42.5943 |
| Resolved chord | 392.000 | -13.6327 | -13.2160 |
| Resolved chord | 659.256 | -11.2423 | -15.7950 |
| Resolved chord | 987.766 | -10.5393 | -15.7843 |
| Resolved chord | 1466.620 | -10.3040 | -15.7695 |
| Close low pair | 164.814 | -0.6657 | -29.5458 |
| Close low pair | 174.614 | -20.4273 | -24.1017 |

The single tone's estimated error is **+0.015750 cents**, within the <3-cent
screen. Its interpolated peak level is -12.675667 dB, corroborating the exact
target projection's loss rather than merely indicating detuning. The spur at
-42.5943 dBc misses the existing <-45 dBc tone gate. The resolved chord has
substantial unwanted partials, and the low pair has markedly uneven gains.
A global output trim cannot correct the relative loss across these notes.
Bank crossover response and intermodulation/roughness require investigation.

The 17003.2 Hz input would double above Nyquist. Its total output power is
**-59.2367 dB** relative to the input, passing the <-50 dB alias screen for this
one frequency and ratio. Here `target_level_db` records total power, not a
projection at 34006.4 Hz; the CSV spur field is a placeholder. This result is
not an alias sweep or evidence for +24/fifth/moving Warp.

## Foundation validation and reproducibility

The new optional `pog3-erb-ps2-foundation` CTest passes. It verifies all 43 poles
before and after float rounding, the prototype's center/half-Q relations by
independent trigonometric equations, center normalization and negative-frequency
response. Actual float impulse histories for the lowest/middle/highest bands
are projected at DC, center, center±bandwidth and negative center. They agree
with the rational complex transfer within 1e-3; worst observed absolute error is
**0.000183378**. Four seconds of different left/right sustained tones match
separate channel instances exactly and remain finite. Reset gives exact silence
for the following second.

Strict `-Wall -Wextra -Wpedantic -Werror` checks pass for the benchmark and
foundation source. The foundation also passes ASan+UBSan with leak detection.
These checks do not establish the unfinished transient, crossing-tone, response
sweep, arbitrary-partition or full-feature audio gates. The production engine
is unchanged, so its earlier release-suite results remain historical evidence.

Configure the optional project using the local source paths documented in the
library report, then run these phases sequentially:

```sh
cmake --build build-pog3-library-trial -j2
ctest --test-dir build-pog3-library-trial --output-on-failure
python3 tests/pog3-library-trial/run.py build-pog3-library-trial/pog3-pitch-library-trial build-pog3-library-trial/erb-artifacts --phase quality --backends erb-ps2
python3 tests/pog3-library-trial/run.py build-pog3-library-trial/pog3-pitch-library-trial build-pog3-library-trial/erb-artifacts --phase allocation --probe build-ci/libpedal-pog3-malloc-probe.so --backends erb-ps2
python3 tests/pog3-library-trial/run.py build-pog3-library-trial/pog3-pitch-library-trial build-pog3-library-trial/erb-artifacts --phase timing --passes 2 --backends erb-ps2 ardor terrarium-80
```

The ignored artifact directory retains CSVs, per-invocation logs and receipts:

| CSV | SHA-256 |
| --- | --- |
| `timing-1.csv` | `0286126025b41fd10c1be558b2de38363bf5c8221be6aea937dab3dff50f116f` |
| `timing-2.csv` | `16c4f9c1fde6a3cdd6eee72024d7f041de1ba629f68f4e857f838e36855b50fa` |
| `allocation-1.csv` | `483d2d354219f4ccd46ebc074be35f3f39f533786dcf1cdb0063e9df25f990bf` |
| `quality-1.csv` | `7044314db6d6748b94d292a289f945efabf2d9803ce7f5473121cbe4709f8a60` |

## Next CPU experiment

**Implemented continuation:** the
[shared eight-path experiment](pog3-erb-shared-cpu-results.md) now measures both
the original grid and extended coverage with accurate/table phase math. Its
28.333–29.648% / 48.628–51.945% table-math costs miss the full CPU goal before
Attack/freeze. The raw reference remains a control; lower phase-processing cost
is the next gate rather than production integration.

Follow the [multi-voice evaluation plan](pog3-erb-ps2-evaluation-plan.md): retain
this raw reference as a control, share analysis/magnitude across the required
voices, and measure the resulting full warm-path demand before production
integration. Downward branch continuity, true fifth phase increments and moving
Warp need explicit histories; the ratio-2 source coverage cannot simply be
reused unchanged. Include alias prevention, phase/control ownership, independent
Attack, live input during freeze and transitions in the eventual full-cost gate.

After that CPU checkpoint, investigate gain/crossover cancellation and roughness
with response sweeps and realistic plucks/DI chords. One accurately tuned, cheap
octave-up voice does not satisfy the complete POG3 behavior or its 25% full-block
CPU goal. No selectable block or production fallback is added here.
