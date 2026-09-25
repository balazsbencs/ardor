# DSP effects review — modulation, pitch and filter effects

Date: 2026-09-25
Goal: studio-quality modulation-slot effects.
Earlier review: [`dsp-effects-review-2026-08-14.html`](dsp-effects-review-2026-08-14.html).

This document records a measured review of the hosted Daisy modulation
effects and the fixes that followed. Each finding has a number from a
measurement, not only from reading the code. Each fix has a test that failed
before the fix and passes after it.

## Status

| Phase | Effects | Review | Fixes | PR |
|---|---|---|---|---|
| 1 | Chorus, Flanger, Phaser, Vibe, Rotary, Vintage Trem, Pattern Trem | Done | Done | [#87](https://github.com/balazsbencs/ardor/pull/87) |
| 2 | Whammy, Harmonizer, Poly Octave | Done | Done | [#88](https://github.com/balazsbencs/ardor/pull/88), [#89](https://github.com/balazsbencs/ardor/pull/89) |
| 3 | Filter, Ladder Sweep, Formant, Quadrature | Done | Quick fixes done | [#91](https://github.com/balazsbencs/ardor/pull/91) |
| — | Auto Swell, Destroyer | Not started | — | — |
| — | Delays (10 modes) | Not started | — | — |
| — | Reverbs (12 modes) | Not started | — | — |

The PRs are stacked. Merge them in order: #87, #88, #89, #91. After each
merge, GitHub moves the next PR to `main`.

None of the PRs is tested on the pedal yet. See [Open items](#open-items).

## Method

- **Signals.** A DI guitar recording (`dryguitar.wav`, resampled to 48 kHz),
  a synthetic plucked phrase with a 1/n harmonic series, steady sines, and
  plucks.
- **Measures.** Engaged level against bypass (RMS and peak), harmonic
  distortion, spurious components (dBc), pitch error (cents, from
  zero crossings), latency (onset time), stereo behaviour with an anti-phase
  input, and control liveness (two settings must give different output).
- **Rule for every fix.** A test in `tests/` fails first, then passes. Old
  presets must keep their sound, unless a change is a deliberate fix.

## Changes that apply to many effects

- **`ModMode::OwnsDryMix()`.** A mode can blend its own dry signal. The host
  then gives it the smoothed Mix and applies only Level. The through-zero
  flanger needs this, because its dry path is delayed.
- **Optional controls `p3` and `p4`.** Modulation modes can add up to two
  controls after the original seven. Scenes store descriptor indexes, so the
  first seven never move. A preset without `p3`/`p4` gets their defaults, and
  the defaults give the old sound.
- **`ToneGain::Loudness`.** An opt-in gain mode for `ToneFilter` that holds a
  pivot (400 Hz) at 0 dB. The loop-safe default lost up to 9.3 dB at the
  bright end. Delay and reverb feedback loops keep the loop-safe default.
  `SetPivotHz()` lets a pitch shifter move the pivot with its ratio.
- **Stereo input.** Several effects added L and R before processing, so an
  anti-phase source went silent. Most now process each channel.
- **Lo-fi Grit display (found through CI).** At 0.23 the display value sat
  exactly on a float32 rounding tie, so it read 4.5x with fused multiply-add
  (Apple clang) and 4.4x without it (Linux CI). A fixture regenerated on macOS
  then failed the manager parity test in CI. The device and the TypeScript
  mirror now compute it in double precision, which gives 4.5x everywhere.

## Phase 1 — classic modulation (PR #87)

| Effect | Finding | Before | After |
|---|---|---|---|
| Flanger | Through-zero math correct only at 50 % Mix | 0.146 undelayed dry leak at 25 % Mix | 0.000 at every Mix |
| Flanger | No Manual control | — | Manual (`p3`), Stereo 0–180° (`p4`) |
| Rotary | A stray `0.5` factor | −5.3 dB at Depth 0 | within 0.6 dB |
| Rotary | Fast speed and Doppler | Fast horn 1.27 Hz, ±0.9 semitone | 6.8 Hz horn, 5.5 Hz drum, about ±2 % pitch |
| Rotary | Crossover let moving bands sum above the input | +4.2 dB | LR4 crossover, fixed at 800 Hz |
| Rotary | Model | — | Slow / Stop / Fast, per-rotor motor inertia, Balance, Mic Spread, audible Drive |
| Vintage Trem | Output soft clip always on | −35 dBc H3 at Depth 0 | −118 dBc |
| Vintage Trem | Photoresistor model | Symmetric squared sine | Neon lamp and CdS cell: fast drop, slow recovery |
| Chorus | Dead and duplicate controls | Tone dead in 4 types; Vibrato = Digital | Tone works everywhere; true Vibrato; Detune pre-delay; Width (`p3`) |
| Chorus | Multi shared one voice between channels | 0.49 L/R correlation | four free voices, below 0.25 |
| Chorus, Flanger | Linear mix lost level | down to −3.9 dB at 50 % | equal-power blend, within 1.4 dB |
| Vibe | Model | Invented per-stage LFO phases | One lamp with filament lag, cells with fast attack and slow release (Lag control); sweep narrows with speed |
| Phaser | Allpass corners clamped | stalled at 11.9 kHz | full range; Stereo (`p3`) and Polarity (`p4`) |
| Pattern Trem | Pattern 1 never gated; no names | — | named patterns, dotted 8ths; Smooth (`p3`), Swing (`p4`) |

Tests: `tests/mod_effect_quality.cpp`, `tests/mod_effect_controls.cpp`,
`tests/mod_effect_models.cpp`.

## Phase 2 — pitch effects (PR #88, PR #89)

### Shared pitch shifter

`PitchShifter` (Whammy, Harmonizer, Shimmer, Chorus Detune) had three
defects:

1. When grain 0 restarted, it aligned with its partner's position before the
   partner had advanced. Every second restart landed late in the same
   direction: the output ran flat and carried sidebands.
2. The restart kept the nominal fraction, not the partner's fraction: up to
   half a sample of phase error per restart.
3. The waveform search was skipped for small jumps. For small shifts, each
   restart then gave back the shift the grain had built up.

| Measure | Before | After |
|---|---|---|
| Whammy pitch error, all presets | −1.4 to −8.9 cents | at most ±0.1 cents |
| Grain sidebands, octave up | −36 dBc | −58 dBc (−52 to −77 on all presets) |

### Effects

| Effect | Finding | Before | After |
|---|---|---|---|
| Whammy | Mono input | anti-phase silent | one voice per channel |
| Whammy | No Detune modes | — | Detune (`p3`): Shallow ±8 cents, Deep ±20 cents |
| Poly Octave | No dry note at defaults | −7.3 dB | Dry (`p3`); default dry plus one octave down |
| Poly Octave | Voices unbalanced | −14.2 / −6.5 / −4.3 dB | −2.0 / −2.3 / −2.6 dB |
| Poly Octave | Tracking did nothing | 80 dB below the signal | Attack: voices swell in over up to 250 ms |
| Harmonizer | Harmony flipped under vibrato | 29 changes in 3 s | 0 (35-cent hysteresis) |
| Harmonizer | Slow note tracking | 107 ms after a note change, 80 ms first note | 40 ms and 26 ms |
| Harmonizer | One voice, mono | — | Interval 2 (`p3`), Voice 2 Level (`p4`), per-channel voices |
| All three | Tone lost level | down to −8.4 dB | within 1 dB |

Tests: `tests/pitch_effect_quality.cpp`, `tests/harmonizer_quality.cpp`.

## Phase 3 — filter effects (PR #91)

| Effect | Finding | Before | After |
|---|---|---|---|
| Quadrature | Hilbert sections had the wrong sign, and the delay was on the wrong path | 0.1–11 dB sideband rejection | 44–74 dB, 60 Hz – 15 kHz |
| Quadrature | Depth dead in 3 of 4 types | — | AM depth, Warble depth, Shift feedback |
| Filter | Tone set both type and frequency | −26 dB at defaults; band-pass locked to 4.5–7.6 kHz | Type (`p3`); Tone sets 80 Hz – 12 kHz; −0.5 dB |
| Filter | Output soft clip | −36 dBc H3 | below −80 dBc |
| Filter | Resonance peak at Q 20 | +24 dB into a clip | within +12 dB |
| Ladder Sweep | Unit-delay feedback, unstable at high cutoff | rang at −9.5 dBFS 1 s after a click | zero-delay feedback, silent below full Resonance |
| Ladder Sweep | Drive changed loudness | −6 to +14 dB | within 1.7 dB |
| Formant | Level and formant balance | −17 dB; all formants equal | +1.2 dB; natural formant levels |
| Formant, Quadrature | Tone lost level | −7.5 / −9.5 dB | +1.2 / +0.4 dB |

Test: `tests/filter_effect_quality.cpp`.

## Decisions and trade-offs

- **Rotary peaks.** The LR4 crossover shifts the phase, which can make sharp
  pick attacks peak higher on a DI signal, with no change in average level.
  A real Leslie does this too. The test checks the band sum above the input
  with steady tones, not the height of attack peaks.
- **Chorus dBucket at 50 % Mix is −1.4 dB.** This is the comb of its short
  delay near 167 Hz, which is part of the circuit's sound.
- **Photo trem at full Depth is −3.7 dB.** For bypass loudness, the
  gain curve would need about +6.8 dB peaks. The peaks stay within +3 dB.
- **Ladder resonance lowers the level on purpose.** Half of the ladder's bass
  loss is restored.
- **Whammy chords got worse** (−9.6 → −5.4 dBc on a triad). The Whammy is a
  monophonic shifter; single notes improved as shown above.
- **Whammy Detune is its own control.** Extending the 19-entry preset
  selector would change the meaning of saved values.
- **Poly Octave octave-up latency (21–48 ms) is not changed.** Wider filter
  bands trade chord quality away much faster than they reduce latency. A real
  fix needs a different engine, for example an FFT phase vocoder.

## Preset impact

- Bank 0, preset 2 (Chorus, Digital): delay 13.5 → 18 ms; Tone now tilts.
- Bank 0, preset 3 (Vintage Trem, Photoresistor): new photocell model, no
  added distortion.
- Saved Filter presets: Tone now means frequency only (log scale). No repo
  preset uses Filter.

## Open items

- **On-device check.** Listen on the pedal, and measure CPU. Rotary, Chorus,
  Whammy and Harmonizer now process each channel. On a desktop core the
  Harmonizer went from 0.44 % to 1.31 % with two voices.
- **Phase 3 quality items.** Stereo input for Filter, Formant and Quadrature.
  True formant morphing. Sample & Hold smoothing and an envelope Sensitivity
  control for Filter. Lag and tempo divisions for Ladder Sweep.
- **Not reviewed yet.** Auto Swell, Destroyer, all delays, all reverbs.
- **Tempo sync** for the modulation effects needs a global tempo from the
  host.
- **Pre-existing test failures, not caused by this work.** On macOS, eight
  ctests fail the same way on `main` (routing, WDW, preset activation:
  "parallel stereo stage workers are unavailable on this platform").
