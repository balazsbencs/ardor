# Hosted delay quality fixes

Date: 2026-08-13  
Target: Raspberry Pi 4 Model B Rev 1.5, Cortex-A72 at 1.5 GHz  
Sample rate: 48 kHz

## Result

All findings in the preceding delay review were addressed. Clean, static
delays now use exact integer taps; moving taps use a 16-tap, 256-phase
Blackman-windowed sinc interpolator. The worst measured fractional-delay loss
at a half-sample position improved as follows:

| Frequency | Before: cubic | After: sinc16 |
| ---: | ---: | ---: |
| 10 kHz | -0.534 dB | +0.001 dB |
| 15 kHz | -2.526 dB | -0.003 dB |
| 20 kHz | -8.415 dB | -1.675 dB |

Host and aarch64 quality measurements agree to insignificant floating-point
rounding. Digital, Tape, Dual, Lo-fi, Duck, and Tremolo neutral first echoes
now measure exactly 0.000 dBFS where their topology is intended to be unity.

## Implemented corrections

- Added a shared queued dual-head transition. Continuous 48-sample control
  updates no longer replace active crossfade anchors or restart their 50 ms
  fade.
- Digital bypasses its write low-pass when modulation is off. Static reads are
  spectrally flat; modulated reads use the new band-limited interpolator.
- Dual preserves right-only input at full ping-pong and has enough storage for
  its complete 1.5x right-head ratio at the 2.5-second endpoint.
- Lo-fi's zero-Crush state bypasses anti-aliasing, quantization, and sample
  hold. Quantization now uses signed-bit scaling. A subtle independent right
  tap gives the mode stereo width without collapsing stereo input.
- Pattern uses exact 2/3 and 4/3 triplet ratios, a full 7.5-second tap range,
  hysteretic/crossfaded pattern selection, queued time transitions, and
  energy-normalized stereo tap weights.
- Filter uses a six-octave logarithmic cutoff table centered at 800 Hz. Its
  feedback decay comes from the raw, level-stable taps rather than resonance
  gain, and its resonant output is softly bounded for explicit headroom. The
  automated quality render peak fell from +1.10 to -1.18 dBFS.
- Bucket Brigade Drive now controls compensated nonlinear input gain instead
  of mainly adding noise and darkness. Its two-pole bandwidth is calibrated
  from roughly 9 kHz to 2.5 kHz, clock alias calculation moved to control
  rate, and independent tap timing adds stereo width. Neutral impulse peak
  improved from -13.83 to -6.91 dBFS without the character render becoming
  louder than the clean modes.
- Tape uses flat integer reads while stationary, band-limited moving reads,
  and an exact anti-alias bypass when flutter is off.
- Duck and Swell gained subtle independent right taps. Tremolo uses
  out-of-phase stereo amplitude modulation when depth is active. At zero
  modulation, Tremolo remains mono-compatible by design.
- Repeats, tone, character, modulation speed, and modulation depth now receive
  20 ms control-rate smoothing. Wet energy and peak headroom were calibrated
  by topology rather than by boosting quiet modes past unity; the broadband
  character renders are within about 3.6 dB RMS excluding the intentionally
  ducked mode.

## Raspberry Pi timing

The isolated target run timed 600,000 callbacks and recorded zero deadline
misses. The sinc path spends extra CPU only while a tap is moving or
crossfading; stationary modes are often faster than the baseline because they
use integer reads.

| Representative 32-frame case | Before mean | After mean | After p99.9 | Deadline budget |
| --- | ---: | ---: | ---: | ---: |
| Digital, default | 5.87 us | 5.59 us | 9.69 us | 1.45% |
| Digital, automation | 8.63 us | 14.51 us | 18.85 us | 2.83% |
| Bucket Brigade, default | 9.51 us | 7.56 us | 11.61 us | 1.74% |
| Pattern, default | 9.03 us | 6.03 us | 9.63 us | 1.44% |
| Pattern, automation | 13.74 us | 35.76 us | 69.52 us | 10.43% |

Pattern automation is deliberately the worst case: two stereo banks of three
taps are simultaneously crossfaded with 16-tap interpolation. Its mean remains
5.36% of a 32-frame callback, leaving substantial isolated headroom.

A production-class 64-frame run with a real NAM model measured the full NAM
tier at 448.66 us mean (33.6% of budget). The fixed delay algorithms measured
0.7-2.0% mean individually in their normal/stress cases; Pattern stress was
3.1%. Representative hosted ambient and heavy three-effect chains measured
6.2% and 10.8% mean respectively.

## Verification

- `pedal-daisy-fx-smoke`: pass
- `pedal-hosted-dsp-unit`: pass, including sinc response, queued automation,
  right-only ping-pong, zero-Crush unity, and full-range Pattern assertions
- `pedal-daisy-fx-automation`: pass
- aarch64 Buildroot release build: pass
- Raspberry Pi quality render: pass and numerically consistent with host
- Raspberry Pi isolated benchmark: 600,000 callbacks, zero misses
- Raspberry Pi production-class DSP benchmark: pass
- Pedal service and CPU governor restored after both target runs

## Artifacts

- `delay-quality-host-fixed-2026-08-13.csv`
- `delay-quality-pi4-fixed-2026-08-13.csv`
- `delay-host-fixed-2026-08-13.csv` and summary
- `delay-pi4-fixed-2026-08-13.csv`, device record, and summary
- `dsp-pi4-delay-fixed-2026-08-13.txt`
- `delay-listening-fixed/` and `delay-listening-fixed-pi4/` float WAV renders

