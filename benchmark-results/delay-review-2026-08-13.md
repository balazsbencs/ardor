# Hosted delay algorithm review

Date: 2026-08-13  
Scope: the ten hosted delay modes and their shared 48 kHz processing path  
Device: Raspberry Pi 4 Model B Rev 1.5, Cortex-A72 at 1.5 GHz  
Product DSP changes: none

## Executive result

The delays are computationally inexpensive on the target, so quality work has
room to spend a modest number of extra cycles. The present quality ceiling is
set principally by fractional-delay interpolation and time-automation behavior,
not CPU capacity.

The isolated Raspberry Pi benchmark covered all ten modes in default, stress,
and continuously automated configurations at 32 and 64 frames. It timed
600,000 callbacks with zero deadline misses. At the more demanding 32-frame
quantum:

| Scenario | Highest mean | Mean budget | Highest p99.9 | p99.9 budget |
| --- | ---: | ---: | ---: | ---: |
| Default | Bucket Brigade, 9.51 us | 1.43% | Pattern, 20.50 us | 3.08% |
| Stress | Bucket Brigade, 10.91 us | 1.64% | Pattern, 23.28 us | 3.49% |
| Time automation | Pattern, 13.74 us | 2.06% | Tape, 21.33 us | 3.20% |

These are isolated effect costs, not complete engine costs. Existing production
measurements show that a full NAM and cabinet chain is already much tighter at
32 frames, so added delay cost should still be justified and rechecked in a
full-engine soak after changes.

## Ranked findings

### P0 — replace or specialize the fractional-delay interpolation

`DelayLineSdram` uses four-point Catmull-Rom/Hermite interpolation whenever a
tap has a fractional sample position. The new probe measured the following
worst fractional position (0.5 sample):

| Frequency | Gain error |
| ---: | ---: |
| 1 kHz | -0.001 dB |
| 5 kHz | -0.036 dB |
| 10 kHz | -0.534 dB |
| 15 kHz | -2.526 dB |
| 20 kHz | -8.415 dB |

Most normalized delay times map to fractional samples, so this loss also occurs
on static clean delays. In a feedback loop it compounds on every repeat. It is
particularly inconsistent with the intended clean Digital mode.

Recommended experiment: use a one-read integer/nearest tap for unmodulated
static delays (sample quantization is only 20.8 us), and compare 6-point or
8-tap band-limited interpolation candidates for moving/modulated taps. Gate the
choice with measured passband error and Pi cost rather than adopting one
interpolator globally.

### P0 — redesign continuous time automation

Digital, Dual, Filter, Lo-fi, Duck, Pattern, Swell, and Tremolo restart a 2,400
sample (50 ms) old/new tap crossfade whenever the control-rate target changes.
Targets are refreshed every 48 samples. Under continuous expression or MIDI
automation, each crossfade therefore advances only about 2% before its anchors
are replaced and the fade restarts. This produces a stepped pursuit of the
target rather than one coherent crossfade or varispeed motion.

It also has measurable cost: Pattern rises from 9.03 us default to 13.74 us
under 32-frame automation (+52%), while Digital rises from 5.87 to 8.63 us
(+47%). Tape and Bucket Brigade use a separate varispeed slew and do not have
this state-machine problem.

Recommended fix: introduce a shared tap-transition primitive with explicit
semantics for a discrete jump versus a continuous gesture. A clean delay can
finish or safely retarget a dual-head crossfade; analog modes should retain a
bounded varispeed slew. Add dense ramps and repeated direction changes to the
automation regression, not only isolated endpoint jumps.

### P0 — make zero-crush Lo-fi genuinely neutral

Lo-fi documents a decimation factor of one as passthrough, but it always runs
the delayed signal through a one-pole low-pass whose coefficient clamps to
0.5. At zero Crush and centered tone this is already approximately -6.0 dB at
10 kHz and -9.3 dB at 20 kHz. The neutral impulse peak measured -7.43 dBFS,
compared with -0.03 dBFS for the otherwise clean Dual, Duck, and Tremolo paths.

The bit-depth math is also one bit more permissive than its label: scaling a
bipolar signal by `1 << bits` produces roughly twice the expected signed PCM
levels.

Recommended fix: exact bypass for the anti-alias, quantizer, and sample hold at
zero Crush; use signed-bit scaling; then calibrate a proper anti-alias cutoff
against the effective held-sample rate for nonzero Crush.

### P0 — preserve stereo input at full Dual ping-pong

At Ping-Pong = 1, the right delay write contains left feedback but multiplies
the right dry input by zero. Independent right-channel source content is
therefore discarded from the wet path. Existing stereo tests exercise the
parallel setting and do not cover this endpoint.

Recommended fix: define the desired stereo injection matrix explicitly and add
left-only, right-only, anti-phase, and mono tests at Ping-Pong 0, 0.5, and 1.

### P1 — make Digital's unmodulated write path actually transparent

Digital says its anti-alias filter is transparent at zero modulation, but it
sets a 20 kHz one-pole coefficient of approximately 0.927 rather than bypassing
the filter. The measured first impulse peak is -0.68 dBFS, versus -0.03 dBFS in
the clean unfiltered modes. Its calculated loss reaches about 1.2 dB at 20 kHz
before interpolation and repeat accumulation are considered.

Recommended fix: smoothly bypass the write low-pass when modulation depth or
rate is zero, and derive any active cutoff from a measured modulation sideband
and aliasing target.

### P1 — recalibrate Bucket Brigade character and control semantics

The neutral Bucket Brigade impulse peak is -13.83 dBFS and its output is mono
for mono input. The exposed Drive control does not change saturation drive; it
mainly increases noise, clock whine, and additional darkness. The BBD helper
also calculates division and `fmodf` twice per sample to derive clock aliasing.

Recommended fix: first establish reference repeat spectra at short, medium,
and long clock times. Separate nonlinear drive from clock/noise amount, calibrate
the two-pole loss and de-emphasis as a pair, and move invariant clock-frequency
work to control rate. CPU is not currently a blocker—Bucket Brigade's worst
mean was only 1.65% of the 32-frame callback—but the saved cycles could fund a
better band-limited model.

### P1 — smooth Pattern changes and correct its usable time range

Pattern selection switches all three taps immediately at two parameter
boundaries; changing Pattern has no tap crossfade or hysteresis. Its base delay
is silently capped so the final tap fits the three-second buffer. Consequently,
large portions of the displayed 0.06–2.5 second time range collapse to the same
base time, depending on the selected pattern. The triplet ratios also use
decimal approximations rather than exact 2/3 and 4/3 ratios.

Recommended fix: crossfade pattern banks, use exact ratios, and map/display the
base-time range that is actually available for the selected pattern.

### P1 — decouple Filter resonance, feedback decay, and peak level

Filter Delay maps its resonance control to Q = 0.5–15, then multiplies feedback
by `1/Q`. High resonance can therefore shorten repeats by more than 20 dB per
loop even when the Repeats control is unchanged. In the automated probe, a
0.25-per-channel input produced a +1.10 dBFS peak, showing that the same simple
normalization does not guarantee output headroom.

The cutoff sweep interpolates the SVF's `g` coefficient between bounds derived
from a linear 800 Hz +/- 1.5 kHz range. This is neither a logarithmic musical
sweep nor a constant-energy one.

Recommended fix: measure the selected SVF output gain across Q and cutoff,
normalize the resonant path without redefining feedback time, use a logarithmic
frequency sweep, and reserve explicit headroom for resonance.

### P2 — decide which modes should create stereo width

For the generated stereo phrase, Lo-fi, Bucket Brigade, Duck, Pattern, Swell,
and Tremolo all measured L/R correlation above 0.999. Digital, Tape, Dual, and
Filter provide decorrelation through separate tap timing or modulation.

Perfect mono compatibility is not itself a defect, but six similarly centered
modes reduce differentiation. This should be a product/listening decision: keep
strict stereo preservation where it serves the mode, and add subtle independent
modulation, cross-feedback, or tap offsets only where it improves the identity.

### P2 — calibrate mode-to-mode wet level and headroom

The character renders span approximately 6.2 dB in RMS level (Pattern at
-22.19 dBFS to Duck at -28.43 dBFS), excluding their intentionally different
envelope behavior. Unlike the reverb family, delays have no per-mode output
calibration. Level-matched listening should decide whether trims are warranted;
raw loudness must not be allowed to win comparisons.

## What is already solid

- Static delay timing is consistent with the physical parameter mapping.
- Separate left/right histories preserve anti-phase material in the currently
  tested configurations.
- Reset is deterministic and same-mode instances do not share storage.
- Feedback is capped below unity and most modes include DC blocking and a soft
  feedback limiter or an intrinsically saturating loop.
- All endpoint, automation, startup, and hosted DSP regression executables pass.
- Host and aarch64 quality outputs agree apart from insignificant floating-point
  rounding and near-noise-floor DC estimates.

## Recommended implementation order

1. Shared tap/interpolation experiments and a correct continuous-automation
   state machine.
2. Clean-path corrections: Digital bypass and Dual ping-pong stereo routing.
3. Lo-fi zero-state, bit-depth, and anti-alias corrections.
4. Pattern tap transitions/range and Filter gain/frequency calibration.
5. Bucket Brigade and Tape character pass using level-matched listening.
6. Optional stereo-width and wet-level calibration across the family.
7. Repeat the isolated benchmark and run full-engine 32/64-frame soaks with the
   accepted changes.

## Artifacts and reproduction

- `delay-quality-host-2026-08-13.csv`: native objective baseline.
- `delay-quality-pi4-2026-08-13.csv`: Raspberry Pi objective baseline.
- `delay-host-2026-08-13.csv`: native timing baseline.
- `delay-pi4-2026-08-13.csv`: target timing baseline.
- `delay-pi4-2026-08-13-summary.txt`: readable target summary.
- `delay-listening-baseline/`: native float WAV character and automation
  renders (locally generated and ignored by Git).
- `tests/delay_quality.cpp`: deterministic measurement/render probe.
- `tests/delay_bench.cpp`: machine-readable timing benchmark.
- `scripts/benchmark-delay-device.sh`: isolated target runner with service and
  governor restoration.

Subjective listening has deliberately not been claimed in this engineering
pass. The generated files are level-unaltered so they preserve measured output;
comparative listening should use loudness-matched copies as a separate step.
