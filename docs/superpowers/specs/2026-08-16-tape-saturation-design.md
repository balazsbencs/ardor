# Tape Saturation — Design

Date: 2026-08-16
Status: Approved, ready for implementation planning

## Goal

Add a studio-grade tape machine block, voiced after a Studer A800 at 15 and
30 ips. The block models magnetic hysteresis rather than applying a
waveshaper, and it models the transport — wow, flutter, scrape flutter and
hiss — as well as the magnetics.

Ardor already has a tape *delay*: `TapeDelay`
(`src/daisyfx/hosted/modes/tape_delay.cpp`) runs a soft-clip `Saturation` and a
pair of emphasis shelves inside a delay line. That block stays as it is. This
design adds a separate saturation block with no delay; the two do not share
code, because the delay's saturation is a memoryless curve sized for an
embedded target and this block's is a differential equation.

## Decisions

Six decisions were settled during brainstorming. Each names what the choice
cost.

1. **Full machine, including transport.** Not magnetics alone. Wow, flutter,
   scrape flutter and hiss are modelled and tested.
2. **Voiced after one machine, the Studer A800**, not a selectable machine
   type. Selectable would need two sets of head curves, two hysteresis fits and
   twice the tuning and test surface, for a second voicing nobody asked for.
3. **Transport off by default.** The code ships and is tested, but `flutter`
   and `hiss_db` sit at their inert values in the default preset. A player who
   adds "tape saturation" gets saturation, and reaches for pitch movement
   deliberately.
4. **Reuse the `distortion` block type with `mode: "tape"`**, not a new
   top-level block type. `distortion:rat` and `distortion:big_cheese` already
   take that path, so the existing `ChainPlan` branch, preset validation and
   chain-badge switch absorb this block without a fourth type.
5. **CPU budget up to roughly 10% of one Pi 4 core**, which buys 8x
   oversampling and an RK4 solver. To be measured and reported, not assumed.
6. **Jiles–Atherton hysteresis solved live**, not a precomputed table. This is
   the opposite of the choice the GCB-95 wah made, and the reason differs: the
   wah's DK solver has a *signal-dependent* iteration count, whose worst case
   is unsafe next to NAM. RK4 is a fixed four evaluations per oversampled
   sample regardless of signal, so its cost is bounded by construction.

## Constraints

From the existing platform:

- Raspberry Pi 4B, AArch64. 48 kHz, preferred block size 64 (fallback 128).
- Audio callback runs at `SCHED_FIFO/70` pinned to CPU 2, already hosting NAM.
- Round-trip latency goal under 10 ms.
- `audio-probe-pi` reports whole-core utilization and headroom.
- `RuntimeChain` calls blocks through a per-sample `process(StereoSample)`, so
  oversampling happens inside one sample call: upsample 1 to 8, run the
  magnetics 8 times, downsample back to 1.

## Module layout

New directory `src/tape/`, three focused files plus headers:

| File | Responsibility |
|---|---|
| `TapeProcessor.{h,cpp}` | Block interface: `configure`, `setParameterTarget`, `reset`, `process`. Owns the signal path, the control mapping, the oversamplers and the dry-delay compensation. |
| `TapeHysteresis.{h,cpp}` | The Jiles–Atherton core and its RK4 solver. Knows nothing about controls, sample rate or stereo. |
| `TapeTransport.{h,cpp}` | Wow, flutter, scrape flutter, the fractional delay line, and hiss. |

The split is drawn so the magnetics and the transport can each be tested
against a signal generator without constructing a block.

## Signal path

Per sample, per channel, except where noted:

```
in
  → drive trim (gain-compensated)
  → record pre-emphasis (IEC, speed-dependent)
  → 8x upsample
  →   [ Jiles-Atherton hysteresis, RK4 ]      x8
  → 8x downsample
  → playback de-emphasis (inverse of pre-emphasis)
  → head bump (two peaking biquads, speed-dependent)
  → transport: fractional delay, modulation shared across channels
  → hiss: independent generator per channel
  → output trim
  → DC blocker
  → mix against the latency-matched dry signal
```

### Reused, not rebuilt

- `HalfbandInterpolator2x` and `HalfbandDecimator2x` from
  `src/daisyfx/hosted/dsp/halfband_resampler.h`, cascaded three deep for 8x.
  Both are header-only, fixed-size and allocation-free.
- `makePeakingEq`, `makeLowPass` and `BiquadCoefficients` from
  `src/equalizer/ParametricEqMath.h` for the head bump and the emphasis
  shelves.
- `DenormalGuard` from `src/dsp/DenormalGuard.h`.
- `DcBlocker` from `src/daisyfx/hosted/dsp/dc_blocker.h`.

## Magnetics

### The model

Jiles–Atherton describes magnetisation `M` as lagging behind an anhysteretic
curve. The anhysteretic magnetisation is a Langevin function of the effective
field:

```
H_e   = H + alpha * M
L(x)  = coth(x) - 1/x
M_an  = M_s * L(H_e / a)
```

The actual magnetisation follows an ordinary differential equation in which
the coercivity `k` sets how far `M` is allowed to lag:

```
delta   = sign(dH/dt)
delta_M = 1 if (dH/dt) and (M_an - M) share a sign, else 0

dM/dH = ( (1 - c) * delta_M * (M_an - M) )
        / ( (1 - c) * delta * k - alpha * (M_an - M) )
      + c * dM_an/dH_e
```

`delta_M` is not decoration. Without it the solver takes unphysical branches
when the field reverses inside a minor loop, and the output grows without
bound on a signal that repeatedly changes direction — which a guitar signal
does thousands of times a second.

That lag is the whole point of the block. It gives tape memory, so the
harmonic content depends on where the signal has recently been and not only on
where it is now. A memoryless waveshaper cannot produce that, which is why the
existing `Saturation` helper was not extended.

### Solver

RK4 on `dM/dt = (dM/dH) * (dH/dt)`, four derivative evaluations per
oversampled sample, at 8x — so 384 kHz internally, 32 evaluations per host
sample per channel.

### Numerical hazards

Three, all of which must be handled explicitly and each of which gets a test:

1. **`L(x)` and `L'(x)` near zero.** `coth(x) - 1/x` is the difference of two
   quantities that both diverge, so it loses all precision as `x` approaches
   zero — which is exactly where a quiet passage sits. Below `|x| < 1e-4` use
   the Taylor expansions `L(x) ~ x/3 - x^3/45` and
   `L'(x) ~ 1/3 - x^2/15`.
2. **Divergence at high drive.** The known failure mode of real-time
   Jiles–Atherton. `M` is clamped to a multiple of `M_s`, and the denominator
   of `dM/dH` is floored away from zero. A test drives the block with
   full-scale square waves and asserts the output stays finite and bounded.
3. **Denormals** in the decayed solver state, covered by `DenormalGuard`.

### Starting parameters

Ballpark values for a modern studio machine, to be tuned against measurement
and ear during implementation:

| Symbol | Meaning | Start |
|---|---|---|
| `M_s` | saturation magnetisation | 3.5e5 A/m |
| `a` | anhysteretic shape | 2.2e4 A/m |
| `alpha` | interdomain coupling | 1.6e-3 |
| `k` | coercivity, loop width | 2.7e4 A/m |
| `c` | reversibility | 0.17 |

These are a starting point, not a result. The implementation plan must include
a tuning step.

## Speed, emphasis and the head bump

An honest statement of what tape speed does on this machine, because the
common assumption is wrong and would lead to a fabricated filter.

Gap loss is the usual explanation for tape's high-frequency behaviour. It does
not apply here at audio frequencies: with a playback gap around 2 µm and
15 ips (0.381 m/s), the first gap-loss null lands near 190 kHz. An A800 is
flat to 20 kHz at both speeds. **So `speed` does not switch a treble filter
in.** It changes two other things, both real:

1. **The head bump.** Finite head-core geometry puts a low-frequency ripple in
   the playback response. At 15 ips it is roughly +2.5 dB near 45 Hz; at 30 ips
   it is smaller and roughly an octave up, near 90 Hz. Modelled as two peaking
   biquads: the bump itself at `f_bump` with `Q ~ 1.2`, and the shallow dip
   below it at `f_bump / 2.2` with about half the gain, negative, `Q ~ 1.5`.
   The `head_bump` control scales the gain of both from zero to full.
2. **The emphasis time constant.** IEC is 35 µs at 15 ips and 17.5 µs at
   30 ips, so the pre-emphasis corner moves from about 4.5 kHz to about 9 kHz.

The record and playback emphasis curves are nominally inverse and cancel on a
linear signal. They are still modelled, because the pre-emphasis is what makes
high frequencies reach the magnetics harder than low ones. That asymmetry is a
real part of how tape sounds and it is the only reason the pair is worth
having.

## Bias — the one phenomenological control

Real AC bias is a supersonic oscillator, typically above 100 kHz, mixed with
the signal at the record head. At 8x oversampling the internal Nyquist is
192 kHz and the anti-imaging halfbands do not pass anything near it, so
simulating a real bias oscillator is not possible at this rate and pretending
otherwise would produce aliasing rather than realism.

`bias` therefore maps to the parameters bias actually controls, rather than
being simulated:

- Under-bias (control below centre): larger `k`, so a wider hysteresis loop and
  more distortion.
- Over-bias (control above centre): smaller `k` and a gentle high-frequency
  shelf loss, matching the trade a real machine makes.

This is the only part of the block that is phenomenological rather than
physical, and it is documented as such in the header so a later reader does not
mistake it for a derivation.

## Transport

**One transport, shared.** A stereo pair runs on one reel past one capstan, so
wow and flutter are correlated between channels. Independent modulation would
tear the stereo image apart. The modulator is computed once and both channels
read it.

Composition:

- Wow at about 0.7 Hz.
- Flutter at about 6 Hz and 11 Hz.
- Scrape flutter as noise filtered above 100 Hz.

The sum drives a fractional-delay read pointer with Lagrange interpolation.

**Full scale is deliberately unrealistic.** An A800 holds wow and flutter near
0.03% DIN weighted, which is inaudible — a control whose maximum was a real
A800 would appear broken. Full scale is roughly 0.3% peak deviation, about ten
times the real machine, so the control has usable range. A realistic setting
sits near 0.1. This is recorded here so the number is not later mistaken for a
measurement.

**Hiss is the opposite case.** Tape noise is physically uncorrelated between
channels, so each channel gets its own generator. An A800 at 15 ips runs about
68 dB below operating level, which sits inside the control range.

Both `flutter` and `hiss_db` default to inert and must be *provably* inert:
tests assert that a sine gains no sidebands at `flutter = 0`, and that silence
in gives exact silence out at `hiss_db = -120`.

## Latency and Mix

Each halfband is 31 taps and linear phase, so its group delay is 15 samples at
the rate it runs. Across three interpolation and three decimation stages:

```
15 @  96 kHz = 7.500 samples @ 48 kHz
15 @ 192 kHz = 3.750
15 @ 384 kHz = 1.875
15 @ 384 kHz = 1.875
15 @ 192 kHz = 3.750
15 @  96 kHz = 7.500
             -------
               26.25 samples @ 48 kHz  =  0.547 ms
```

The delay is fractional, so it cannot be matched by an integer buffer. The dry
path runs through a matching 26.25-sample fractional delay before the `mix`
crossfade. Without it, `mix` at intermediate settings is a comb filter rather
than a blend.

Block latency is 0.547 ms, reported in the header, and inside the 10 ms
round-trip goal.

## Controls

Nine, which sits inside the range the catalog already uses (2 to 13).

| Key | Kind | Range | Default | Effect |
|---|---|---|---|---|
| `drive` | number | -12…+24 dB | 0 | Field strength into the magnetics. Gain-compensated, so it changes character and not loudness. |
| `saturation` | number | 0…1 | 0.5 | Anhysteretic shape `a`. Turning it **up** lowers `a`, which gives an earlier and harder knee. |
| `bias` | number | 0…1 | 0.5 | Coercivity `k` and over-bias HF loss. 0.5 is correct bias; below is under-bias, above is over-bias. |
| `speed` | choice | `15` / `30` ips | `15` | Head-bump frequency and emphasis time constant. |
| `head_bump` | number | 0…1 | 0.5 | Scales both head-bump biquads. |
| `flutter` | number | 0…1 | **0** | Transport modulation depth. |
| `hiss_db` | number | -120…-60 dB | **-120** | Tape noise floor. -120 is a hard off: the generator is skipped, not run at a low level, so test 10 can demand exact silence. |
| `mix` | number | 0…1 | 1 | Wet/dry, latency-matched. |
| `output_db` | number | -24…+24 dB | 0 | Output trim. |

**Gain compensation.** `drive` scales the field into the magnetics and is then
undone by a matching output scale, so the block's level stays roughly constant
across the control and only its harmonic content changes. The compensation is a
static function of the `drive` setting alone, not a live envelope follower — a
follower would fight the compressor and transient shaper already in the chain.
A test sweeps `drive` across its range on a fixed sine and asserts the output
level moves less than 3 dB.

`speed` is a `choice` control holding a string, following
`dynamics:compressor`'s `detector`. Choice parameters are not live-settable
through `setParameterTarget`; they are applied in `configure()` and reach the
engine by rebuilding the chain. `UiModel::setSelectedBlockParamValue` holds an
explicit allow-list of which choice edits requeue a preview, and `speed` must
be added to it.

## Tests

`tests/tape_smoke.cpp`, registered as `pedal-tape-smoke`:

**Magnetics**

1. The hysteresis loop opens: sweeping the field up and back down gives
   different magnetisation on each branch.
2. The loop closes: a full cycle returns to its start within tolerance, so the
   solver does not drift.
3. Harmonics are odd-dominant, and each harmonic grows monotonically with
   `drive`.
4. Bounded under abuse: full-scale square waves, DC steps and silence all
   leave the output finite and inside a fixed ceiling.
5. Precision near zero: the Langevin path agrees with its Taylor branch across
   the switchover point.

**Filters and speed**

6. Head bump measurable at 15 ips near 45 Hz; smaller and near 90 Hz at 30 ips.
7. `head_bump = 0` leaves the low-frequency response flat.

**Aliasing**

8. A 12 kHz tone at full drive puts under -70 dBFS into non-harmonic bins.
   This is the test that proves the oversampling earns its cost.

**Transport**

9. `flutter = 0` adds no sidebands to a steady sine.
10. `hiss_db = -120` with silent input gives exact silence.
11. Flutter modulation is identical on both channels.

**Block contract**

12. `mix = 0` reproduces the input within -80 dB, which simultaneously proves
    the dry-delay compensation is correct.
13. `reset()` returns the block to its constructed state.
14. Sweeping `drive` across its full range on a fixed sine moves the output
    level less than 3 dB, which proves the gain compensation holds.

**Performance**

15. A Pi 4 benchmark under `benchmark-results/`, reported as a measured
    number against the ~10% of one core budget. If it does not fit, the
    documented fallback is 4x oversampling, and the aliasing test threshold
    moves with it and is re-reported.

Manager-side: `catalog.test.ts` gains the block, and
`tests/manager_effect_catalog_smoke.cpp` asserts the C++ and TypeScript
catalogs agree.

## Integration surface

The same set the transient shaper touched, plus the tape module:

```
CMakeLists.txt                                   library + test target
src/tape/TapeProcessor.{h,cpp}                   new
src/tape/TapeHysteresis.{h,cpp}                  new
src/tape/TapeTransport.{h,cpp}                   new
src/preset/ChainPlan.cpp                         accept mode "tape"
src/audio/EngineLoader.cpp                       build it, both lanes
src/dsp/PedalEngine.{h,cpp}                      add + parameter entry points
src/dsp/RuntimeChain.{h,cpp}                     block kind, process, reset, diagnostics
src/dsp/DualRigProcessor.{h,cpp}                 parameter forwarding
src/dsp/ClipDiagnostics.h                        SignalStageKind::Tape
src/ui/UiModel.cpp                               name, defaults, browser entry, choice allow-list
src/ui/ParameterControls.cpp                     nine controls
src/ui/LvglChainLayout.cpp                       "TAPE" badge
apps/pedal-poc/main.cpp                          preset parameter dispatch
apps/manager/src/effects/catalog.v1.json         definition
apps/manager/src/effects/catalog.test.ts         catalog test
apps/manager/src/presets/block-browser/
  BlockBrowser.test.tsx                          browser count
tests/tape_smoke.cpp                             new
tests/manager_effect_catalog_smoke.cpp           parity
tests/runtime_chain_smoke.cpp                    chain integration
tests/ui_model_smoke.cpp                         defaults and naming
```

## Out of scope

- A second machine voicing.
- Tape print-through, drop-outs and azimuth error.
- Delay. The existing `delay:tape` block covers tape echo.
- A real AC bias oscillator, for the sampling-rate reason given above.

## Measured

Recorded 2026-08-16 after implementation. Full detail in
`benchmark-results/tape-host-2026-08-16.txt`.

| Quantity | Budget | Measured |
|---|---|---|
| Worst in-band alias | under -70 dBc | **-86.6 dBc** |
| `mix = 0` error | under -80 dB | **-inf dB**, bit-exact |
| Drive level swing, full range | under 3 dB | **0.10 dB** sine, **0.57 dB** on guitar |
| Low end vs 1 kHz, head bump off | within 0.3 dB | **-0.13 dB** |
| Hiss channel cross-correlation | under 0.05 | **0.0005** |
| Pi 4 whole-core cost | about 10% | **not yet measured** |

**No hysteresis parameters moved.** The starting fit in
`TapeHysteresis::Parameters` passed every objective check, so the tuning step
the plan reserved was not needed.

**Two design values did move**, both for reasons found by measurement rather
than by argument, and both documented at their definitions:

- `kLangevinTaylorLimit` is 0.1, not the 1e-4 first written. In float,
  `coth(x) - 1/x` returns exactly zero at 1e-4 and is still 10% wrong at 1e-3.
- The wet path is padded by 1.75 samples, not trimmed by 0.75. A four-point
  Lagrange cannot realise a delay below one sample causally. Block latency is
  therefore a whole 76 frames, which is what makes `mix = 0` bit-exact.

**One bug was found that the design did not anticipate.** The emphasis pair was
first placed beside the solver at the oversampled rate, where it amplified the
halfband images by up to four times. The solver takes its branch direction from
the sign of a one-sample difference, and near a low note's turning points that
boosted ripple decided the sign. Measured, it lifted 45 Hz by 4.7 dB relative to
1 kHz. Emphasising at the host rate removed the cause and cost less; a deadband
on the direction detector took the residual from 0.6 dB to 0.13 dB.

### Outstanding

The Raspberry Pi 4 CPU figure. The pedal has no toolchain, so it needs a
Buildroot cross-build through Docker, which was not run. As a stand-in, the
block costs **2.28x the RAT** per sample on the host, at the same 8x
oversampling — the RAT already ships and runs on the Pi, so that ratio
transfers better than an absolute host number.

Most of that 2.28x is structural rather than algorithmic: the tape block runs
two independent oversampled lanes where the RAT runs one and copies it. Per
lane it is about 1.14x the RAT. If the Pi figure comes back over budget, the
first thing to reach for is collapsing the two lanes when the input is mono,
ahead of dropping to 4x oversampling and giving up aliasing headroom.

### Not yet done

Nobody has listened to it. Every check above is a measurement. The renders
under the session scratchpad (`wet-0.wav`, `wet-12.wav`, `wet-24.wav`) exist
for that pass.
