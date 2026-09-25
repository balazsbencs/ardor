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
| 4 | Delays: Digital, Tape, Dual, Filter, Lo-fi, Bucket Brigade, Duck, Pattern, Swell, Tremolo | Done | D1–D8 done | [#92](https://github.com/balazsbencs/ardor/pull/92) |
| — | Reverbs (12 modes) | Not started | — | — |

The PRs are stacked. Merge them in order: #87, #88, #89, #91, #92. After each
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

## Phase 4 — delays

All ten modes share one host path: the Daisy processor blends dry and wet
with a linear crossfade, and each mode runs a per-channel loop with the
right head 150 samples (3.1 ms) behind the left.

### Measured findings

| # | Mode | Finding | Measured |
|---|---|---|---|
| D1 | All | Engaging a delay turns the dry note down: the host mix is a linear crossfade | dry −2.5 dB at the default 25 % Mix, −6.0 dB at 50 % |
| D2 | Digital, Tape | Grit adds loop gain: the saturator's small-signal gain grows to 16×, so repeats stop decaying | Repeats 0.35, level 6 s after a burst: Digital −186 dB at Grit 0, −31 dB at 0.5, **+1.2 dB at 1.0**; Tape −190 / **−6.9** / **+0.7 dB** |
| D3 | Filter | Output soft clip on the wet signal, and no resonance make-up | first repeat H3 −35 dBc at 0.3, −24 dBc at 0.6; first repeat +4.3 dB at the default resonance |
| D4 | Digital, Dual, Duck, Pattern, Trem | The loop-safe Tone tilt also sets the first repeat's level | Tone 1: first repeat −9.4 dB on guitar |
| D5 | Tape | Tone and Grit colour only the feedback path, so they do nothing to the first repeat | Tone 0 / 1: 0.00 dB change on the first repeat |
| D6 | Dual | Modulation depth is a fraction of the delay time | 11 cents peak to peak at 0.1 s, **109 cents** at 1.0 s |
| D7 | All but Pattern, Trem | The 150-sample right-head offset combs the repeats when the output is summed to mono | mono-sum response spread 47–75 dB between 100 Hz and 4 kHz |
| D8 | Tape, Bucket Brigade | Always some saturation, even at Grit 0 | Tape H3 −56 / −44 dBc (0.3 / 0.6 input); Bucket Brigade −44 / −32 dBc |
| D9 | All | Shortest time is 60 ms (Lo-fi 2 ms): no doubling or short slapback | range 60 ms – 2.5 s |
| D10 | All | No tap tempo, tempo sync or note divisions | Ardor has no global tempo yet |

Checked and fine:

- First-repeat gain is 0 dB in Digital, Dual, Lo-fi, Duck and Trem. Pattern
  sits at −2.2 dB by its tap weights, and Swell's first repeat depends on its
  attack time, as designed.
- Grit in Bucket Brigade is level-compensated; repeats decay at every Grit.
- Delay-time changes: the clean modes crossfade two heads over 50 ms; Tape
  and Bucket Brigade glide the pitch like a varispeed.
- In-loop DC blockers: 20 repeats lose only 0.5 dB more at 60 Hz than at
  1 kHz, so the 5.35 Hz blockers are not worth changing.

### Fixes

| # | Fix | Before | After |
|---|---|---|---|
| D1 | Delay mix law: dry = min(1, 2 × (1 − Mix)), wet = Mix | dry −2.5 dB at 25 % Mix, −6.0 dB at 50 % | 0.00 dB up to 50 %; wet level unchanged |
| D2 | Digital and Tape saturators divided by their drive (unity small-signal gain), blended in by Grit | Repeats 0.35, full Grit: +1.2 dB (Digital) and +0.7 dB (Tape) 6 s later | below −190 dB |
| D3 | Filter Delay: no output clip; constant-peak band-pass, low/high-pass make-up above Q 4, soft limiter above −3 dBFS | H3 −24 dBc; first repeat +4.3 dB | H3 −141 dBc; first repeat −0.9 dB; peaks within +12 dB |
| D4 | `ToneFilter::LoudnessCorrection()` applied to the wet output only; the loop keeps the loop-safe tilt | first repeat −9.4 dB at Tone 1 | within 0.4 dB at both ends |
| D5 | Tape record path: tape EQ, normalised saturation and head loss act on everything written, Tone on playback | Tone and Grit did nothing to the first repeat | both shape it |
| D6 | Dual modulation depth is absolute (30 samples at full, as Digital) | 11 / 109 cents at 0.1 / 1 s | 13.7 cents at both |
| D7 | New **Width** control for every delay (index 7, default 100 %): it scales the right-head offset (gliding over ~20 ms) and the side of the repeats | mono sum combed by 47–75 dB | Width 0: identical channels, flat mono sum; Width 1: bit-exact with the old image |
| D8 | Tape write limiter clean below −3 dBFS; `BbdEmulator` blends into its saturator over the first 10 % of Drive | H3 −44 dBc (Tape), −32 dBc (Bucket Brigade) at 0.6 | below −150 dBc |

Notes:

- Chorus dBucket also uses `BbdEmulator`, at a fixed Drive of 0.15, where
  the blend is complete. Its sound is unchanged.
- The Tape first repeat now passes the head loss (a one-pole near 4.6 kHz)
  that only later repeats had before. That is how a tape echo sounds.
- `soft_limit_above()` in `fast_math.h` is now shared by the Filter mode, the
  Filter Delay and the Tape write.
- The Dual Mod Depth display now reads in milliseconds, like the other modes.
- Width is the delays' first control after the original seven: it uses the
  processor's P3 slot, which the delay kind now accepts by key (`width`) and
  by index. The default keeps saved presets unchanged; mono rigs set it to 0.

Test: `tests/delay_effect_quality.cpp`.

### Still open

- **D9, D10.** A shorter minimum time; tap tempo and note divisions once the
  host has a tempo.
- **Mix law for reverbs.** The reverbs use the same linear crossfade. Measure
  it in the reverb phase before changing it.

## PR review feedback

A third-party review commented on #87, #88, #89 and #91. Each comment was
checked against the code before a change.

| PR | Comment | Outcome |
|---|---|---|
| #87 | Chorus Vibrato ignored Mix | Fixed: Vibrato uses the same equal-power blend; Mix at full is the pure vibrato |
| #88 | Preset → Detune switch with a slow Glide | Partly correct. Gliding both voices from the old interval is the Glide behaviour. The real defect: the right voice's anti-alias filter was set for the mirrored pitch, so it aliased during the glide (+3.5 dB more energy than the left voice on 15 kHz). Fixed: the filter follows the voice's real pitch |
| #89 | Mix and Voice 2 Level | The behaviour is sound; the description was wrong. Mix sets both voices together (dry + Mix × (voice 1 + level × voice 2)); Voice 2 Level balances voice 2 against voice 1. Description corrected |
| #89 | A silent Voice 2 still panned Voice 1 | Fixed: the pan follows Voice 2's level, so at 0 Voice 1 stays centred |
| #91 | Carry the Filter migration into the release guidance | Done: see [Release notes](#release-notes); the commit carries a `BREAKING CHANGE:` footer for the generated changelog |

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

- Delays: the dry note is louder by 2.5 dB at 25 % Mix and up to 6 dB at
  50 %, which is the missing level coming back.
- Bank 0, preset 0 (Digital, Grit 0.57, Repeats 0.59): the repeats used to
  hold at the limiter for good; they now decay.
- Tape presets (bank 0, presets 0 and 2): the first repeat now has the tape
  head loss.

- Bank 0, preset 2 (Chorus, Digital): delay 13.5 → 18 ms; Tone now tilts.
- Bank 0, preset 3 (Vintage Trem, Photoresistor): new photocell model, no
  added distortion.
- Saved Filter presets: Tone now means frequency only (log scale). No repo
  preset uses Filter.

## Release notes

Changes that alter how saved settings sound. Carry these into the release
guidance.

- **Filter (modulation slot).** Tone now sets only the frequency, 80 Hz –
  12 kHz on a log scale, and a new Type control (LP / BP / HP / Notch)
  selects the filter. A saved Filter without Type loads as low-pass, at the
  frequency its Tone value now maps to. Before, Tone also picked the type
  (low-pass below about 0.4, band-pass around 0.5, high-pass above about 0.6).
  Re-select the type for band-pass, high-pass or notch sounds.
- **Rotary.** Speed is now the fast rotor rate (4–9 Hz); the second
  control is Rotor (Slow / Stop / Fast). Saved Slow and Fast keep their
  meaning; Tone is now a cabinet tone instead of the crossover frequency.
- **Pattern Trem.** Pattern 1 (all steps on) is now "Dotted 8ths".
- **Vibe.** The second control is now Lag (photocell recovery) instead of
  Shape.
- **Quadrature.** Depth now acts in every type (AM depth, Shift feedback);
  a saved Depth in AM or Shift now changes the sound.
- **Chorus.** Tone is a wet tone control in every type (was dBucket
  feedback), Vibrato follows Mix, and Digital and Multi use a 5–25 ms delay.
- **Delays.** The dry note stays at unity up to 50 % Mix; Grit no longer
  sustains repeats; Tape colours the first repeat.

## Open items

- **On-device check.** Listen on the pedal, and measure CPU. Rotary, Chorus,
  Whammy and Harmonizer now process each channel. On a desktop core the
  Harmonizer went from 0.44 % to 1.31 % with two voices.
- **Phase 3 quality items.** Stereo input for Filter, Formant and Quadrature.
  True formant morphing. Sample & Hold smoothing and an envelope Sensitivity
  control for Filter. Lag and tempo divisions for Ladder Sweep.
- **Delay items D9, D10.** See Phase 4.
- **Not reviewed yet.** Auto Swell, Destroyer, all reverbs.
- **Tempo sync** for the modulation effects needs a global tempo from the
  host.
- **Pre-existing test failures, not caused by this work.** On macOS, eight
  ctests fail the same way on `main` (routing, WDW, preset activation:
  "parallel stereo stage workers are unavailable on this platform").
