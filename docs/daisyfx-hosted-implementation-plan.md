# DaisyFX Hosted Effects Implementation Plan

Date: 2026-07-16  
Scope: `src/daisyfx/hosted/` and its Ardor integration  
Primary goals: best practical sound quality, numerical reliability, and safe Raspberry Pi 4 CPU usage

The safest implementation is a sequence of small, measurable changes. Fix numerical correctness and clocking first; only then tune tone, stereo width, interpolation, and nonlinear character.

```text
Regression harness
      ↓
Numerical + reset correctness
      ↓
Parameter semantics
      ↓
True 24 kHz reverb processing
      ↓
Automation + delay-time handling
      ↓
Stereo preservation
      ↓
DSP polish and calibration
      ↓
Raspberry Pi qualification
```

## 1. Build the regression and measurement harness

Do this before changing DSP behavior so each subsequent change has an objective exit gate.

Primary files:

- [`tests/daisy_fx_smoke.cpp`](../tests/daisy_fx_smoke.cpp)
- [`tests/dsp_bench.cpp`](../tests/dsp_bench.cpp)
- [`CMakeLists.txt`](../CMakeLists.txt)

Add dedicated tests rather than continuing to grow the smoke test:

- `tests/hosted_dsp_unit.cpp`
  - ToneFilter response and stability.
  - DelayLine reset/configuration behavior.
  - Resampler response once implemented.
  - `fast_sin` accuracy.
- `tests/daisy_fx_response.cpp`
  - Impulse, sine, noise, DC, and anti-phase stereo inputs.
  - Every mode at defaults.
  - Each parameter individually at 0, 0.5, and 1.
  - All-minimum and all-maximum combinations.
  - At least three seconds of output per extreme configuration.
- `tests/daisy_fx_automation.cpp`
  - Parameter jumps and continuous sweeps.
  - Delay-time changes.
  - Categorical parameter changes.
- Extend `pedal-dsp-bench` with:
  - Default and maximum-modulation scenarios.
  - Parameter automation scenarios.
  - Representative multi-effect chains.
  - Per-effect and full-chain p50/p99/p99.9 timing.

Do not make exact sample-for-sample output golden tests for creative effects. Test measurable properties: finiteness, timing, decay slope, frequency response, channel behavior, reset determinism, and output bounds.

### Exit gate

- Every effect can be rendered and measured automatically.
- Current failures are captured as expected failing tests or diagnostics.
- Benchmark results can be exported before and after every major change.

## 2. Fix ToneFilter and numerical fault containment

This is the first production fix because the current filter can poison several reverb states.

The problem starts in [`tone_filter.cpp`](../src/daisyfx/hosted/dsp/tone_filter.cpp): its cutoff can reach 20 kHz while reverb components are configured for 24 kHz in [`constants.h`](../src/daisyfx/hosted/config/constants.h).

Implement:

- Store the actual sample rate in `ToneFilter`.
- Clamp all cutoff frequencies to `min(20000 Hz, 0.45 * sampleRate)`.
- Clamp knob input to `[0,1]`.
- Make `0.5` a real bypass/flat position.
- Use logarithmic cutoff mapping:
  - Below center: dry to progressively darker low-pass.
  - Above center: dry to progressively stronger high-pass.
- Crossfade from dry to filtered output as the knob moves away from center. This avoids a discontinuity at 0.5.
- Add `Reset()` that clears filter state but retains sample-rate and parameter configuration.
- Detect non-finite filter state, clear it, and return zero for that wet sample.

At the processor boundary:

- Validate wet output before it enters the dry/wet mix.
- On the first non-finite result, latch the effect into a safe dry-only state and record a fault.
- Do not clear large delay buffers from the audio callback.
- Expose the fault so the control thread can rebuild or reset the effect outside realtime processing.

Tests:

- Sweep 1,001 knob positions at both 24 and 48 kHz.
- Long impulse and noise renders at every extreme.
- Center position must be transparent.
- No coefficient or state may become non-finite.
- All twelve reverbs must survive the maximum-tone soak.

## 3. Correct reset behavior and delay startup

### Preserve configured delays during reset

[`DelayLineSdram::Reset()`](../src/daisyfx/hosted/dsp/delay_line_sdram.cpp) currently resets the tap to two samples. That silently destroys fixed allpass and diffuser timings, including [`Diffuser::Reset()`](../src/daisyfx/hosted/dsp/diffuser.h).

Refactor the lifecycle:

- `Init(buffer, size)`:
  - Establish buffer and default tap configuration.
  - Clear memory and write position.
- `Clear()`:
  - Clear memory and write position.
  - Preserve `delay_` and fractional configuration.
- `Reset()`:
  - Use `Clear()` semantics for compatibility, or replace call sites explicitly.
- Fixed-delay components configure taps once during `Init`.
- Dynamic delay modes re-seed their time controller after reset.

Audit every stateful DSP component:

- ToneFilter states.
- EnvelopeFollower state.
- FDN delay taps, damping states, DC blockers, and LFO phases.
- Diffusers and plate allpasses.
- Pitch shifters.
- Poly Octave decimator, interpolator, filter bank, and generator.
- Random generators, using deterministic reset seeds.

### Seed delay time before the first sample

Several delay classes initialize `delay_smooth_` to zero even though their code expects a negative sentinel. Replace the sentinel convention with an explicit `bool timeInitialized_`.

During initial `Prepare()`:

- Compute the requested time.
- Set both current and target tap positions immediately.
- Mark the controller initialized.
- Do not begin the first render by slewing from two or zero samples.

Tests:

- Configure each delay with feedback zero and mix 100%.
- Send an impulse.
- The first wet arrival must equal the requested delay, within interpolation and known processing latency.
- `Reset(); render` must match a freshly configured instance.
- Fixed diffuser/allpass delays must be unchanged after reset.

## 4. Establish one parameter-mapping contract

Parameter values are currently mapped to physical ranges in [`DaisyFxProcessor.cpp`](../src/daisyfx/DaisyFxProcessor.cpp), then some modes map them again.

Define the contract:

- Public presets and live controls remain normalized `[0,1]`.
- Mapping occurs exactly once in the hosted adapter.
- `ParamSet` contains physical values.
- Continuous mode implementations consume those physical values directly.
- Categorical parameters contain an integer enum index or explicit physical choice, not an ambiguous float threshold.

Immediate corrections:

- [`BloomReverb`](../src/daisyfx/hosted/modes/bloom_reverb.cpp)
  - `bloom_time_s = params.param1`.
  - `bloom_feedback = params.param2`.
- [`SwellReverb`](../src/daisyfx/hosted/modes/swell_reverb.cpp)
  - `rise_time_s = params.param1`.
- [`ChoraleReverb`](../src/daisyfx/hosted/modes/chorale_reverb.cpp)
  - Map normalized control once to vowel `0…6`.
  - Map resonance to explicit Q choices such as `{2,5,10}`.
- [`MagnetoReverb`](../src/daisyfx/hosted/modes/magneto_reverb.cpp)
  - Map Heads to `{3,4,6}` explicitly.
  - Map Spacing to an enum such as `Even` or `Golden`.

Add mapping tests covering normalized minimum, center, and maximum for every mode-specific range in [`reverb_param_map.h`](../src/daisyfx/hosted/params/reverb_param_map.h).

### Make the catalog mode-specific

Extend [`DaisyFxParamDescriptor`](../src/daisyfx/DaisyFxCatalog.h) with:

- Parameter type: continuous, stepped, or enum.
- Units and display precision.
- Enum labels or step count.
- Whether the parameter is active for the mode.
- Mode-specific default.

Correct known catalog mismatches in [`DaisyFxCatalog.cpp`](../src/daisyfx/DaisyFxCatalog.cpp):

- Filter: P1 is Resonance; P2 is Shape/Source.
- Formant: P1 is Resonance; P2 is Vowel.
- Destroyer:
  - Speed = Decimation.
  - Depth = Bits.
  - P1 = Filter resonance.
  - P2 = Noise.
- Hide unused Vibe P2.
- Hide unused Hall P2 and Room P1.
- Replace generic delay “Grit” labels with Saturation, Ping-Pong, Filter Type, Crush, Ducking, or Pattern as appropriate.
- Set Flanger and Phaser default mix near 50%.
- Set musical Shimmer defaults, initially +12 and +7 semitones, then validate by listening.

Keep existing JSON keys loadable. Changed defaults should affect newly created effects, not overwrite explicit values in existing presets.

## 5. Run reverbs at their intended 24 kHz

Currently the algorithms use 24 kHz constants but are called once per 48 kHz host sample in [`DaisyFxProcessor.cpp`](../src/daisyfx/DaisyFxProcessor.cpp).

Implement one shared boundary:

```text
48 kHz stereo input
        ↓
2:1 half-band decimator
        ↓
24 kHz reverb mode
        ↓
1:2 half-band interpolator
        ↓
48 kHz stereo wet signal
```

Add a reusable `HalfRateReverbAdapter` and a dedicated 2× half-band resampler. Do not reuse the current factor-six Poly Octave resampler.

Recommended filter process:

- Prototype a 31-tap symmetric polyphase half-band FIR.
- Target approximately:
  - Passband through 9–10 kHz with less than 0.15 dB ripple.
  - Stopband beginning around 14–15 kHz.
  - At least 65–70 dB stopband rejection.
- If 31 taps cannot meet the target, compare 39/47 taps on Pi before accepting weaker rejection.
- Use symmetry and half-band zero coefficients to minimize multiplies.
- No heap allocation or coefficient calculation in the audio callback.

Clocking changes:

- Process one reverb sample for every two host samples.
- Call the reverb control update every 48 internal frames, which is every 96 host frames.
- Update [`Fdn::PrepareBlock()`](../src/daisyfx/hosted/dsp/fdn.cpp) using the actual internal interval.
- Make EnvelopeFollower, LFO, DC blocker, and similar time-dependent utilities sample-rate aware.
- Reset resampler history with the reverb.

The FIR pair adds fixed latency. Delay the dry path by the same number of host frames while the reverb block is active and expose `latencyFrames()` for chain/offline accounting.

Tests:

- Resampler impulse response and gain.
- Passband/stopband frequency sweep.
- Reverb pre-delay within approximately 5% of the requested time.
- Measured RT60 within approximately 5% of the configured value.
- FDN modulation frequency correct in real seconds.
- Reverb core plus resampler must cost less than the existing full-rate reverb path on Pi.

## 6. Add parameter smoothing and correct delay-time automation

Introduce reusable audio-rate and control-rate smoothers in the processor.

Recommended smoothing:

| Parameter | Strategy |
|---|---|
| Mix, output level | 5–10 ms audio-rate linear ramp |
| Feedback/repeats | 10–20 ms control-rate smoothing |
| Tone/filter frequency | 20–40 ms, preferably logarithmic frequency interpolation |
| Decay and modulation depth | 20–50 ms |
| Pre-delay | Dual-tap crossfade |
| Categorical controls | Hysteresis plus 10–20 ms output crossfade |

Separate control responsibilities:

- `SetParameters()` updates coefficients only when effective values change.
- `AdvanceControl(frames)` advances LFOs, envelopes, and inertia even when parameters are unchanged.
- Cache mapped targets and coefficient inputs.
- Avoid repeated `expf`, `sinf`, `cosf`, and `tanf` calls after parameters settle.

Delay-time policy:

- Digital, Dual, Filter, Duck, Lo-fi, Pattern, Swell, and Trem delays:
  - Use two read positions on the existing buffer.
  - Crossfade old and new taps over 20–50 ms.
  - Do not pitch-glide through multi-second time changes.
- Tape and Bucket Brigade:
  - Retain varispeed slew because pitch movement is part of their identity.
  - Remove the current hard 0.5-sample step cap and define the glide in milliseconds/seconds instead.
- Pattern delay:
  - Crossfade the complete old three-tap pattern against the new pattern.
- Short modulation remains applied around the selected base time after the time controller.

Tests:

- Step every parameter during sine, noise, and guitar-like test signals.
- Detect impulses significantly above the surrounding local peak/RMS.
- Confirm clean delays do not produce multi-second pitch glides.
- Confirm Tape/BBD changes remain smooth and intentionally pitch-bearing.
- Rapid enum jitter around a threshold must not switch repeatedly.

## 7. Preserve stereo through delay and reverb

Change the interfaces in [`delay_mode.h`](../src/daisyfx/hosted/modes/delay_mode.h) and [`reverb_mode.h`](../src/daisyfx/hosted/modes/reverb_mode.h) to accept `StereoFrame`.

Reverb plan:

- Use L/R pre-delay paths.
- Use asymmetric input matrices rather than mono summing.
- Feed stereo input to the existing FDN stereo overload.
- Preserve current mono-input spaciousness.
- Adapt Plate, Spring, Magneto, and Reflections individually rather than blindly duplicating their entire core.

Delay plan:

- Digital, Dual, and Filter already own two long buffers: write L/R independently.
- For mono-buffer modes, add a second channel buffer and use cross-feedback where musically appropriate.
- Tape/BBD coloration should use independent filter/nonlinear state per channel so one channel does not modulate the other unintentionally.
- Pattern taps can be alternately panned while retaining stereo input history.

The memory increase is acceptable on a Pi 4: one additional 2.5-second float delay line is roughly 480 KB. CPU impact should be measured, but delay-line reads and writes are much cheaper than the nonlinear and pitch-processing modes.

Tests:

- Full-wet anti-phase input must not disappear.
- Left-only input must produce a spatial response rather than identical L/R output.
- Mono input must remain centered and compatible with current presets.
- Reset and tail behavior must remain deterministic.

## 8. Apply targeted DSP quality improvements

Only begin these after the timing and parameter fixes are complete.

### FDN modulation

In [`fdn.cpp`](../src/daisyfx/hosted/dsp/fdn.cpp):

- Store current and target modulated delays.
- Ramp tap positions between control updates using one addition per line per internal sample.
- For modulation depth zero, use the integer-delay fast path.
- For active modulation, A/B linear interpolation against cubic/Hermite interpolation.
- Prefer cubic only for modulated taps if its Pi cost is acceptable.

### Trigonometric approximation

Replace the fifth-order Taylor approximation in [`fast_math.h`](../src/daisyfx/hosted/dsp/fast_math.h) with a fitted seventh-order approximation.

Target:

- One additional multiply at most.
- No overshoot beyond `[-1,1]`.
- Maximum absolute error below roughly `1e-4` over a complete cycle.

### Wet-level calibration

After all structural changes:

- Render transient, sustained, bass-heavy, and bright inputs.
- Loudness-match mode outputs.
- Add a fixed per-mode wet trim where necessary.
- Avoid putting a common limiter across every effect; that would change tail and transient character.
- Revisit Flanger, Phaser, and Shimmer defaults after calibration.

### Tail estimation

Replace the generic reverb estimate in [`DaisyFxProcessor.cpp`](../src/daisyfx/DaisyFxProcessor.cpp) with mode-aware formulas:

- Magneto: period plus feedback repetitions to −60 dB.
- Bloom: decay plus bloom rise/feedback duration.
- Shimmer: decay plus pitch-feedback extension.
- Swell: rise time plus decay.
- Other FDN modes: pre-delay plus measured RT60 and longest-line margin.

Validate estimates by rendering until RMS stays below −60 dB for a defined window.

### Optional antialiasing experiment

Do not oversample whole effects by default.

Instead:

- Identify the modes producing the worst audible aliasing at high drive.
- Compare first-order ADAA and 2× oversampling around only the nonlinear cell.
- Merge only when blind listening shows a meaningful benefit and the representative Pi chain remains within budget.

Poly Octave should similarly expose measured 48/64/80-band quality tiers only if listening demonstrates that extra bands are worthwhile.

## 9. Raspberry Pi 4 release gate

Run Release/AArch64 builds on the actual Pi, not desktop estimates.

Representative chains should include:

- Typical: NAM + cabinet IR + EQ/compressor + modulation + delay + hall.
- Ambient: modulation + delay + shimmer/bloom.
- Heavy: Poly Octave + delay + reverb with active automation.

At 48 kHz and 64-frame blocks, the deadline is approximately 1,333 μs.

Acceptance criteria:

- Average full-chain DSP below 65% of the deadline: approximately 866 μs.
- p99 below 80%: approximately 1,067 μs.
- Zero non-finite output.
- Zero audio-thread allocation or locking.
- Zero xruns during a minimum ten-minute automated parameter soak.
- No thermal throttling during the run.
- Reset matches fresh configuration.
- Delay, pre-delay, and RT60 timing within approximately 5%.
- No audible clicks during supported automation.
- Stereo side information survives delay and reverb.
- Listening tests are loudness-matched and prefer or equal the previous version.

## Recommended change sequence

Implement this as separate reviewable changes:

1. Test and benchmark harness.
2. ToneFilter and fault safety.
3. Reset and delay startup correctness.
4. Parameter contract and catalog metadata.
5. Half-rate reverb processing.
6. Parameter smoothing and delay automation.
7. Stereo processing.
8. FDN and math quality improvements.
9. Wet-level calibration and tail estimation.
10. Raspberry Pi qualification and optional antialiasing experiments.

Do not begin subjective per-mode tuning until steps 1–7 are complete. Clocking, parameter semantics, reset behavior, and stereo topology all materially change the sound and would invalidate earlier tuning work.
