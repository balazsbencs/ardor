# Tape Saturation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a studio-grade tape machine block, voiced after a Studer A800, that models magnetic hysteresis and the transport rather than applying a waveshaper.

**Architecture:** A new `src/tape/` module with three units — `TapeHysteresis` (Jiles–Atherton magnetics, RK4, no knowledge of controls or stereo), `TapeTransport` (wow, flutter, scrape, hiss) and `TapeProcessor` (the block: controls, 8x oversampling, emphasis, head bump, dry-delay compensation, mix). It enters the chain as block type `distortion`, mode `tape`, alongside `rat` and `big_cheese`.

**Tech Stack:** C++20, CMake, `nlohmann::json` for parameters, LVGL for the pedal UI, TypeScript/Vitest for the manager catalog. No new third-party dependencies.

**Spec:** `docs/superpowers/specs/2026-08-16-tape-saturation-design.md`

## Global Constraints

- Target Raspberry Pi 4B, AArch64. 48 kHz, block size 64 (fallback 128).
- The audio callback runs at `SCHED_FIFO/70` pinned to CPU 2 and already hosts NAM. **No allocation, no locks, no I/O and no unbounded loops in anything reachable from `process()`.**
- CPU budget for this block: roughly 10% of one core, measured, not assumed.
- Every processor in this module follows the house block contract, copied from `src/rat/RatProcessor.h`: move-only (copy constructor and copy assignment deleted), `configure(params, sampleRate, error)`, `setParameterTarget(key, value)`, `reset()`, `process(StereoSample)`.
- Oversampling is 8x — 384 kHz internally — using `pedal::HalfbandInterpolator2x` and `pedal::HalfbandDecimator2x` from `src/daisyfx/hosted/dsp/halfband_resampler.h`.
- Block latency is 26.25 samples at 48 kHz (0.547 ms). `latencyFrames()` reports the rounded `26`, matching `RatProcessor`; the internal dry-path compensation uses the exact 26.25.
- Commit messages follow the repository style: `type(scope): imperative summary`, then a body that explains *why* and states what was measured. Attribution trailers are disabled globally — do not add them.
- Build with `cmake --build build -j8`. Run tests with `ctest --test-dir build --output-on-failure`.

---

## File Structure

| File | Responsibility |
|---|---|
| `src/equalizer/ParametricEqMath.{h,cpp}` | **Modify.** Gains `makeHighShelf`, needed by the emphasis pair. Reusable by the EQ block later. |
| `src/dsp/RuntimeChain.{h,cpp}` | **Modify.** Distortion storage becomes a `std::variant`; tape becomes a third alternative. |
| `src/tape/TapeHysteresis.{h,cpp}` | **Create.** Jiles–Atherton core and RK4 solver. Scalar in, scalar out, no controls. |
| `src/tape/TapeTransport.{h,cpp}` | **Create.** Wow, flutter, scrape flutter, fractional delay, hiss. |
| `src/tape/TapeProcessor.{h,cpp}` | **Create.** The block: controls, oversampling, emphasis, head bump, dry delay, mix, drive calibration. |
| `tests/tape_smoke.cpp` | **Create.** All fifteen assertions from the spec. |

Task order is bottom-up: the two shared modifications first, then the leaves, then the block, then wiring, then tuning.

---

### Task 1: Collapse the distortion pointer pair into a variant

`RuntimeChain::Block` currently holds two separate `unique_ptr` members for distortion processors — `distortion` for `RatProcessor` and `fuzz` for `CheeseProcessor` — and reads them through `block.distortion ? A : block.fuzz->B` at six sites. That ternary already dereferences `fuzz` without checking it. A third processor would turn every site into a nested three-way check and add a third way to crash.

Do this refactor first, with no behaviour change, so the tape block is a third variant alternative rather than a third pointer.

**Files:**
- Modify: `src/dsp/RuntimeChain.cpp:375-404`, `:510-511`, `:651`, `:813-818`
- Modify: `src/dsp/RuntimeChain.cpp` (the `Block` struct, around `:55-80`)
- Test: `tests/rat_smoke.cpp`, `tests/cheese_smoke.cpp`, `tests/runtime_chain_smoke.cpp` (existing — must stay green)

**Interfaces:**
- Consumes: nothing.
- Produces: `RuntimeChain::Block::distortion` becomes `std::unique_ptr<DistortionProcessor>` where `using DistortionProcessor = std::variant<RatProcessor, CheeseProcessor>;` is declared inside `RuntimeChain.cpp`'s anonymous detail. Task 5 adds `TapeProcessor` as a third alternative. The public `addDistortion` overloads and `setDistortionParameter` signatures do **not** change.

- [ ] **Step 1: Run the existing tests to record the green baseline**

```bash
cmake --build build -j8 \
  && ctest --test-dir build --output-on-failure -R 'rat-smoke|cheese-smoke|runtime-chain-smoke'
```

Expected: all three pass. If any already fails, stop and report — this task must not be started from a red baseline.

- [ ] **Step 2: Replace the two pointers with one variant**

In `src/dsp/RuntimeChain.cpp`, add `#include <variant>` and declare the alias above `struct RuntimeChain::Block`:

```cpp
// One storage slot for every distortion-family processor. Two parallel
// unique_ptrs and a `a ? a->f() : b->f()` ternary worked while there were two,
// but the ternary dereferenced `b` without checking it, and each new processor
// multiplied the number of sites that had to agree. A variant makes "exactly
// one of these is live" the type's job instead of a convention.
using DistortionProcessor = std::variant<RatProcessor, CheeseProcessor>;
```

In `struct RuntimeChain::Block`, delete both members:

```cpp
  std::unique_ptr<RatProcessor> distortion;
  std::unique_ptr<CheeseProcessor> fuzz;
```

and replace them with:

```cpp
  std::unique_ptr<DistortionProcessor> distortion;
```

- [ ] **Step 3: Rewrite the six use sites**

`addDistortion`, both overloads (was `:375-391`):

```cpp
void RuntimeChain::addDistortion(std::string id, RatProcessor processor)
{
  Block block;
  block.kind = Block::Kind::Distortion;
  block.id = std::move(id);
  block.distortion = std::make_unique<DistortionProcessor>(std::move(processor));
  blocks_.push_back(std::move(block));
}

void RuntimeChain::addDistortion(std::string id, CheeseProcessor processor)
{
  Block block;
  block.kind = Block::Kind::Distortion;
  block.id = std::move(id);
  block.distortion = std::make_unique<DistortionProcessor>(std::move(processor));
  blocks_.push_back(std::move(block));
}
```

`setDistortionParameter` (was `:393-404`):

```cpp
bool RuntimeChain::setDistortionParameter(const std::string& id, const std::string& key, float value)
{
  for (auto& block : blocks_) {
    if (block.kind == Block::Kind::Distortion && block.id == id) {
      return std::visit([&](auto& processor) { return processor.setParameterTarget(key, value); },
                        *block.distortion);
    }
    if (block.kind == Block::Kind::DualRig
        && block.dualRig->setDistortionParameter(id, key, value)) return true;
  }
  return false;
}
```

In `process()` (was `:510-511`):

```cpp
    case Block::Kind::Distortion:
      current = std::visit([&](auto& processor) { return processor.process(current); },
                           *block.distortion);
      break;
```

In `processBlock()` (was `:651`):

```cpp
        const auto processed =
          std::visit([&](auto& processor) { return processor.process(input); }, *block.distortion);
```

In `reset()` (was `:813-818`), replace both `if` blocks with:

```cpp
    if (block.distortion) {
      std::visit([](auto& processor) { processor.reset(); }, *block.distortion);
    }
```

- [ ] **Step 4: Run the tests to verify nothing changed**

```bash
cmake --build build -j8 \
  && ctest --test-dir build --output-on-failure -R 'rat-smoke|cheese-smoke|runtime-chain-smoke'
```

Expected: the same three pass. This is a refactor; a behaviour change here is a bug.

- [ ] **Step 5: Commit**

```bash
git add src/dsp/RuntimeChain.cpp
git commit -F - <<'EOF'
refactor(chain): hold one distortion processor, not two nullable ones

RuntimeChain::Block kept a unique_ptr per distortion processor and read them
through `distortion ? distortion->f() : fuzz->f()` at six sites. That ternary
dereferenced `fuzz` without checking it, so a Block with neither pointer set
was a crash rather than a compile error, and every processor added multiplied
the sites that had to agree.

A std::variant makes "exactly one of these is live" the type's job. The six
sites collapse to std::visit and the null-check disappears. No behaviour
change; the rat, cheese and runtime chain smoke tests pass unchanged.
EOF
```

---

### Task 2: Add a high-shelf filter to the shared EQ math

The record and playback emphasis pair needs a first-order-style shelf. `ParametricEqMath` has peaking, high-pass and low-pass but no shelf. Add it there rather than privately in the tape module, because it is general and the EQ block is the obvious second consumer.

**Files:**
- Modify: `src/equalizer/ParametricEqMath.h:21`, `src/equalizer/ParametricEqMath.cpp`
- Test: `tests/hosted_dsp_unit.cpp`

**Interfaces:**
- Consumes: `ardor::BiquadCoefficients`, `ardor::biquadMagnitudeDb` (both already in `ParametricEqMath.h`).
- Produces: `ardor::BiquadCoefficients ardor::makeHighShelf(float sampleRate, float frequencyHz, float q, float gainDb)` — RBJ cookbook high shelf. Passing `-gainDb` gives the magnitude inverse, which is how de-emphasis is built.

- [ ] **Step 1: Write the failing test**

`tests/hosted_dsp_unit.cpp` is a set of `verifyXxx()` free functions called in order from `main()`. Add a new one and call it from `main` after `verifyHalfbandResamplers()`:

```cpp
// A high shelf must reach its full gain well above the corner, leave the
// corner itself at half gain, and pass the low end untouched. Inverting the
// gain must invert the response, because that is how the tape block builds
// de-emphasis out of the same function.
void verifyHighShelf()
{
    const float rate = 48000.0f;
    const auto boost = ardor::makeHighShelf(rate, 4547.0f, 0.707f, 12.0f);
    const auto cut = ardor::makeHighShelf(rate, 4547.0f, 0.707f, -12.0f);

    require(std::fabs(ardor::biquadMagnitudeDb(boost, 100.0f, rate)) < 0.1f,
            "high shelf must leave 100 Hz alone");
    require(std::fabs(ardor::biquadMagnitudeDb(boost, 4547.0f, rate) - 6.0f) < 0.5f,
            "high shelf must sit at half gain on the corner");
    require(std::fabs(ardor::biquadMagnitudeDb(boost, 20000.0f, rate) - 12.0f) < 0.6f,
            "high shelf must reach full gain well above the corner");

    for (const float probe : {100.0f, 1000.0f, 4547.0f, 12000.0f}) {
      const float sum = ardor::biquadMagnitudeDb(boost, probe, rate)
                      + ardor::biquadMagnitudeDb(cut, probe, rate);
      require(std::fabs(sum) < 0.01f, "inverted gain must invert the response");
    }
}
```

Then add `verifyHighShelf();` to `main()`, and `#include "equalizer/ParametricEqMath.h"` at the top if it is not already there.

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build -j8 --target pedal-hosted-dsp-unit
```

Expected: FAIL to compile, `'makeHighShelf' is not a member of namespace 'ardor'`.

- [ ] **Step 3: Declare it**

In `src/equalizer/ParametricEqMath.h`, after the `makeLowPass` declaration on line 23:

```cpp
// RBJ cookbook high shelf. Pass a negative gainDb for the magnitude inverse of
// a positive one, which is how a pre-emphasis and de-emphasis pair is built.
BiquadCoefficients makeHighShelf(float sampleRate, float frequencyHz, float q, float gainDb);
```

- [ ] **Step 4: Implement it**

In `src/equalizer/ParametricEqMath.cpp`, following the file's existing helper style:

```cpp
BiquadCoefficients makeHighShelf(float sampleRate, float frequencyHz, float q, float gainDb)
{
  const float amplitude = std::pow(10.0f, gainDb / 40.0f);
  const float omega = 2.0f * kPi * frequencyHz / sampleRate;
  const float cosOmega = std::cos(omega);
  const float alpha = std::sin(omega) / (2.0f * q);
  const float sqrtAmplitude = std::sqrt(amplitude);
  const float twoSqrtAlpha = 2.0f * sqrtAmplitude * alpha;

  const float a0 = (amplitude + 1.0f) - (amplitude - 1.0f) * cosOmega + twoSqrtAlpha;

  BiquadCoefficients coefficients;
  coefficients.b0 = amplitude * ((amplitude + 1.0f) + (amplitude - 1.0f) * cosOmega + twoSqrtAlpha) / a0;
  coefficients.b1 = -2.0f * amplitude * ((amplitude - 1.0f) + (amplitude + 1.0f) * cosOmega) / a0;
  coefficients.b2 = amplitude * ((amplitude + 1.0f) + (amplitude - 1.0f) * cosOmega - twoSqrtAlpha) / a0;
  coefficients.a1 = 2.0f * ((amplitude - 1.0f) - (amplitude + 1.0f) * cosOmega) / a0;
  coefficients.a2 = ((amplitude + 1.0f) - (amplitude - 1.0f) * cosOmega - twoSqrtAlpha) / a0;
  return coefficients;
}
```

If `kPi` is not the name the file already uses for pi, use whatever it does use — check the top of `ParametricEqMath.cpp` before writing this.

- [ ] **Step 5: Run the test to verify it passes**

```bash
cmake --build build -j8 --target pedal-hosted-dsp-unit \
  && ctest --test-dir build --output-on-failure -R hosted-dsp-unit
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/equalizer/ParametricEqMath.h src/equalizer/ParametricEqMath.cpp tests/hosted_dsp_unit.cpp
git commit -F - <<'EOF'
feat(eq): add a high shelf to the shared filter math

The tape block needs a record pre-emphasis and a playback de-emphasis that are
each other's inverse. ParametricEqMath had peaking, high-pass and low-pass but
no shelf, and a shelf built privately inside the tape module would be the third
place in this repo to derive biquad coefficients.

RBJ cookbook form, so passing the negated gain gives the exact magnitude
inverse. Tests assert the corner sits at half gain, the shelf reaches full gain
by 20 kHz, the low end is untouched, and a boost summed with its matching cut
is flat within 0.01 dB.
EOF
```

---

### Task 3: TapeHysteresis — the magnetics

The core of the block. Jiles–Atherton describes magnetisation `M` lagging an anhysteretic curve; that lag is what gives tape memory and a level-dependent harmonic series a waveshaper cannot produce.

**Files:**
- Create: `src/tape/TapeHysteresis.h`, `src/tape/TapeHysteresis.cpp`
- Modify: `CMakeLists.txt` (new `ardor_tape` library after the `ardor_rat` block at line 209, and a test target near `pedal-rat-smoke` at line 454)
- Test: `tests/tape_smoke.cpp`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces:
  - `struct ardor::TapeHysteresis::Parameters { float saturationMagnetisation; float anhystereticShape; float interdomainCoupling; float coercivity; float reversibility; }`
  - `ardor::TapeHysteresis::defaultParameters()` returning `Parameters` — the Studer-ish starting fit.
  - `void configure(const Parameters&, float sampleRate)` — `sampleRate` is the **oversampled** rate, 384000 for this block.
  - `void reset()`
  - `float process(float field)` — takes `H` in A/m, returns `M / M_s`, nominally within ±1.
  - Free functions `ardor::langevin(float)` and `ardor::langevinPrime(float)`, exposed in the header so the Taylor switchover can be tested directly.

- [ ] **Step 1: Write the failing tests**

Create `tests/tape_smoke.cpp`:

```cpp
#include "tape/TapeHysteresis.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message)
{
  if (!condition) throw std::runtime_error(message);
}

constexpr float kOversampledRate = 384000.0f;
constexpr float kTwoPi = 6.28318530718f;

// Drives the core with a slow triangular field sweep and records the
// magnetisation at every step, which traces the B-H loop directly.
std::vector<float> sweepLoop(float peakField, std::size_t stepsPerRamp)
{
  ardor::TapeHysteresis core;
  core.configure(ardor::TapeHysteresis::defaultParameters(), kOversampledRate);
  core.reset();

  std::vector<float> trace;
  trace.reserve(stepsPerRamp * 4);
  const auto ramp = [&](float from, float to) {
    for (std::size_t n = 0; n < stepsPerRamp; ++n) {
      const float t = static_cast<float>(n) / static_cast<float>(stepsPerRamp - 1);
      trace.push_back(core.process(from + (to - from) * t));
    }
  };
  // Two full cycles: the first magnetises from zero, the second is the loop.
  ramp(0.0f, peakField);
  ramp(peakField, -peakField);
  ramp(-peakField, peakField);
  ramp(peakField, -peakField);
  return trace;
}

void testLoopOpens()
{
  constexpr std::size_t kSteps = 2000;
  const auto trace = sweepLoop(4.0e4f, kSteps);
  // The third ramp climbs from -peak to +peak, the fourth falls back. At the
  // same field the two branches must differ: that difference IS hysteresis.
  // Compare at the midpoint of each ramp, which is field zero on both.
  const float ascending = trace[2 * kSteps + kSteps / 2];
  const float descending = trace[3 * kSteps + kSteps / 2];
  require(std::fabs(ascending - descending) > 0.02f,
          "the hysteresis loop must open: the two branches differ at the same field");
  require(ascending < descending,
          "the ascending branch must sit below the descending one");
}

void testLoopCloses()
{
  constexpr std::size_t kSteps = 2000;
  const auto trace = sweepLoop(4.0e4f, kSteps);
  // The second and fourth ramps are the same gesture one cycle apart. If the
  // solver drifts, they end at different places.
  const float firstCycleEnd = trace[2 * kSteps - 1];
  const float secondCycleEnd = trace[4 * kSteps - 1];
  require(std::fabs(firstCycleEnd - secondCycleEnd) < 0.01f,
          "the loop must close: the solver must not drift across cycles");
}

// Runs a sine at the oversampled rate and returns the magnitude of the given
// harmonic, by direct correlation rather than an FFT.
double harmonicMagnitude(float fieldAmplitude, int harmonic)
{
  ardor::TapeHysteresis core;
  core.configure(ardor::TapeHysteresis::defaultParameters(), kOversampledRate);
  core.reset();

  constexpr float kFundamental = 1000.0f;
  const std::size_t settle = static_cast<std::size_t>(kOversampledRate / kFundamental) * 8;
  const std::size_t measure = static_cast<std::size_t>(kOversampledRate / kFundamental) * 64;

  for (std::size_t n = 0; n < settle; ++n) {
    const float t = static_cast<float>(n) / kOversampledRate;
    core.process(fieldAmplitude * std::sin(kTwoPi * kFundamental * t));
  }

  double real = 0.0;
  double imaginary = 0.0;
  for (std::size_t n = 0; n < measure; ++n) {
    const float t = static_cast<float>(settle + n) / kOversampledRate;
    const float out = core.process(fieldAmplitude * std::sin(kTwoPi * kFundamental * t));
    const double phase = kTwoPi * kFundamental * harmonic * t;
    real += out * std::cos(phase);
    imaginary += out * std::sin(phase);
  }
  return 2.0 * std::sqrt(real * real + imaginary * imaginary) / static_cast<double>(measure);
}

void testOddHarmonicsDominateAndGrow()
{
  const double third = harmonicMagnitude(3.0e4f, 3);
  const double second = harmonicMagnitude(3.0e4f, 2);
  require(third > second * 4.0,
          "tape is odd-symmetric: the third harmonic must dominate the second");

  const double quiet = harmonicMagnitude(1.0e4f, 3);
  const double loud = harmonicMagnitude(3.0e4f, 3);
  const double louder = harmonicMagnitude(6.0e4f, 3);
  require(quiet < loud && loud < louder,
          "the third harmonic must grow monotonically with drive");
}

void testBoundedUnderAbuse()
{
  ardor::TapeHysteresis core;
  core.configure(ardor::TapeHysteresis::defaultParameters(), kOversampledRate);
  core.reset();

  // A full-scale square is the worst case for an RK4 Jiles-Atherton solver:
  // dH/dt is enormous and reverses sign thousands of times a second. This is
  // the documented way these solvers diverge.
  for (std::size_t n = 0; n < 400000; ++n) {
    const float field = (n / 24 % 2 == 0) ? 1.0e6f : -1.0e6f;
    const float out = core.process(field);
    require(std::isfinite(out), "the solver must stay finite on a full-scale square");
    require(std::fabs(out) < 4.0f, "the solver must stay bounded on a full-scale square");
  }

  // A DC step and then silence must settle, not ring or creep.
  for (std::size_t n = 0; n < 100000; ++n) require(std::isfinite(core.process(5.0e4f)), "DC must stay finite");
  for (std::size_t n = 0; n < 100000; ++n) require(std::isfinite(core.process(0.0f)), "silence must stay finite");
}

void testLangevinTaylorSwitchover()
{
  // coth(x) - 1/x is the difference of two diverging terms, so it loses every
  // significant bit near zero and the implementation switches to a Taylor
  // series below a threshold. The two branches must agree across the seam, or
  // the block has a discontinuity exactly where a quiet passage sits.
  for (int i = -20; i <= 20; ++i) {
    const float x = 1.0e-4f * (1.0f + 0.02f * static_cast<float>(i));
    const float value = ardor::langevin(x);
    const float slope = ardor::langevinPrime(x);
    require(std::isfinite(value) && std::isfinite(slope), "Langevin must stay finite near zero");
    require(std::fabs(value - x / 3.0f) < 1.0e-8f, "Langevin must match x/3 near zero");
    require(std::fabs(slope - 1.0f / 3.0f) < 1.0e-6f, "Langevin slope must match 1/3 near zero");
  }
  // Well away from zero the closed form must still be right: L(x) -> 1 as x grows.
  require(std::fabs(ardor::langevin(100.0f) - 0.99f) < 0.02f, "Langevin must saturate at 1");
  require(std::isfinite(ardor::langevinPrime(200.0f)), "Langevin slope must not overflow to NaN");
}

} // namespace

int main()
{
  try {
    testLoopOpens();
    testLoopCloses();
    testOddHarmonicsDominateAndGrow();
    testBoundedUnderAbuse();
    testLangevinTaylorSwitchover();
  } catch (const std::exception& error) {
    std::fprintf(stderr, "tape smoke failed: %s\n", error.what());
    return 1;
  }
  std::printf("tape smoke passed\n");
  return 0;
}
```

- [ ] **Step 2: Register the library and test target, then run to verify it fails**

In `CMakeLists.txt`, after the `ardor_rat` block ends at line 209:

```cmake
add_library(ardor_tape
  src/tape/TapeHysteresis.cpp
)
target_include_directories(ardor_tape PUBLIC
  src
  ${neuralampmodelercore_SOURCE_DIR}/Dependencies
)
target_compile_features(ardor_tape PUBLIC cxx_std_20)
target_link_libraries(ardor_tape PUBLIC ardor_equalizer)
```

`TapeTransport.cpp` and `TapeProcessor.cpp` join this source list in Tasks 4 and 5.

After the `pedal-rat-smoke` block at line 456:

```cmake
add_executable(pedal-tape-smoke tests/tape_smoke.cpp)
target_link_libraries(pedal-tape-smoke PRIVATE ardor_tape)
add_test(NAME pedal-tape-smoke COMMAND pedal-tape-smoke)
```

Then:

```bash
cmake -S . -B build && cmake --build build -j8 --target pedal-tape-smoke
```

Expected: FAIL to compile, `tape/TapeHysteresis.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `src/tape/TapeHysteresis.h`:

```cpp
#pragma once

namespace ardor {

// The Langevin function L(x) = coth(x) - 1/x and its derivative, which set the
// shape of the anhysteretic magnetisation curve.
//
// Both terms of L diverge at the origin, so their difference loses every
// significant bit there — and the origin is exactly where a quiet passage
// sits. Below kLangevinTaylorLimit both functions switch to their Taylor
// series, which is correct there and faster besides.
inline constexpr float kLangevinTaylorLimit = 1.0e-4f;

float langevin(float x);
float langevinPrime(float x);

// Jiles-Atherton magnetic hysteresis.
//
// Magnetisation M lags behind an anhysteretic curve M_an, which is a Langevin
// function of the effective field H_e = H + alpha*M. How far M is allowed to
// lag is set by the coercivity k:
//
//   dM/dH = ( (1-c) * delta_M * (M_an - M) )
//           / ( (1-c) * delta * k - alpha * (M_an - M) )
//         + c * dM_an/dH_e
//
// That lag is the whole reason this class exists. It gives the block memory,
// so the harmonics depend on where the signal has recently been and not only
// on where it is now. A memoryless waveshaper cannot produce that, which is
// why the soft-clip Saturation helper in the tape delay was not extended.
//
// Solved with RK4. RK4 is a fixed four derivative evaluations per sample
// whatever the signal, so unlike the wah's DK solver its cost is bounded by
// construction — which is what makes it safe to run beside NAM.
//
// This class knows nothing about controls, stereo or sample rates other than
// the one it is given. It is fed the already-oversampled rate.
class TapeHysteresis {
public:
  struct Parameters {
    float saturationMagnetisation = 3.5e5f; // M_s, A/m
    float anhystereticShape = 2.2e4f;       // a, A/m — lower gives a harder knee
    float interdomainCoupling = 1.6e-3f;    // alpha
    float coercivity = 2.7e4f;              // k, A/m — sets the loop width
    float reversibility = 0.17f;            // c
  };

  // A starting fit for a modern studio machine, tuned in this repository
  // against the harmonic and loop tests rather than measured off hardware.
  static Parameters defaultParameters() { return Parameters{}; }

  // `oversampledRate` is the rate this core actually runs at — 384000 for the
  // tape block's 8x oversampling, not the host's 48000.
  void configure(const Parameters& parameters, float oversampledRate);
  void reset();

  // Takes the applied field H in A/m, returns M / M_s, nominally within +/-1.
  float process(float field);

private:
  float derivative(float magnetisation, float field, float fieldRate) const;

  Parameters parameters_{};
  float period_ = 1.0f / 384000.0f;
  float magnetisation_ = 0.0f;
  float previousField_ = 0.0f;
};

} // namespace ardor
```

- [ ] **Step 4: Write the implementation**

Create `src/tape/TapeHysteresis.cpp`:

```cpp
#include "tape/TapeHysteresis.h"

#include <algorithm>
#include <cmath>

namespace ardor {

float langevin(float x)
{
  if (std::fabs(x) < kLangevinTaylorLimit) {
    return x / 3.0f - (x * x * x) / 45.0f;
  }
  return 1.0f / std::tanh(x) - 1.0f / x;
}

float langevinPrime(float x)
{
  if (std::fabs(x) < kLangevinTaylorLimit) {
    return 1.0f / 3.0f - (x * x) / 15.0f;
  }
  // For large |x| sinh overflows to infinity and the second term becomes zero,
  // which is the correct limit — L'(x) tends to 1/x^2 — so no guard is needed.
  const float sinhX = std::sinh(x);
  return 1.0f / (x * x) - 1.0f / (sinhX * sinhX);
}

void TapeHysteresis::configure(const Parameters& parameters, float oversampledRate)
{
  parameters_ = parameters;
  period_ = 1.0f / oversampledRate;
  reset();
}

void TapeHysteresis::reset()
{
  magnetisation_ = 0.0f;
  previousField_ = 0.0f;
}

float TapeHysteresis::derivative(float magnetisation, float field, float fieldRate) const
{
  const float effectiveField = field + parameters_.interdomainCoupling * magnetisation;
  const float normalised = effectiveField / parameters_.anhystereticShape;
  const float anhysteretic = parameters_.saturationMagnetisation * langevin(normalised);
  const float anhystereticSlope =
    (parameters_.saturationMagnetisation / parameters_.anhystereticShape) * langevinPrime(normalised);

  const float difference = anhysteretic - magnetisation;
  const float direction = fieldRate < 0.0f ? -1.0f : 1.0f;

  // delta_M is not decoration. Without it the solver takes unphysical branches
  // whenever the field reverses inside a minor loop, and the state grows
  // without bound on a signal that changes direction thousands of times a
  // second — which is what a guitar signal is.
  const float branch = (direction * difference) > 0.0f ? 1.0f : 0.0f;

  const float reversible = parameters_.reversibility;
  float denominator = (1.0f - reversible) * direction * parameters_.coercivity
                    - parameters_.interdomainCoupling * difference;

  // The denominator passes through zero at the tips of the loop. Floor it away
  // from zero, keeping its sign, so the ratio stays finite there.
  const float floorMagnitude = 1.0e-6f * parameters_.coercivity;
  if (std::fabs(denominator) < floorMagnitude) {
    denominator = denominator < 0.0f ? -floorMagnitude : floorMagnitude;
  }

  const float irreversible = ((1.0f - reversible) * branch * difference) / denominator;

  // The reversible term wants dM_an/dH, and dH_e/dH is 1 + alpha*dM/dH, which
  // would make this implicit. alpha is 1.6e-3, so using dM_an/dH_e directly
  // costs under 0.2% and keeps the step explicit. Documented, not overlooked.
  return (irreversible + reversible * anhystereticSlope) * fieldRate;
}

float TapeHysteresis::process(float field)
{
  const float fieldRate = (field - previousField_) / period_;
  const float start = previousField_;
  const float middle = 0.5f * (start + field);
  const float halfStep = 0.5f * period_;

  const float k1 = derivative(magnetisation_, start, fieldRate);
  const float k2 = derivative(magnetisation_ + halfStep * k1, middle, fieldRate);
  const float k3 = derivative(magnetisation_ + halfStep * k2, middle, fieldRate);
  const float k4 = derivative(magnetisation_ + period_ * k3, field, fieldRate);

  magnetisation_ += (period_ / 6.0f) * (k1 + 2.0f * k2 + 2.0f * k3 + k4);

  // The documented failure mode of a live Jiles-Atherton solver is divergence
  // at high drive. Magnetisation beyond saturation is not physical, so clamp
  // it rather than let a diverging step reach the output.
  if (!std::isfinite(magnetisation_)) magnetisation_ = 0.0f;
  const float ceiling = 2.0f * parameters_.saturationMagnetisation;
  magnetisation_ = std::clamp(magnetisation_, -ceiling, ceiling);

  previousField_ = field;
  return magnetisation_ / parameters_.saturationMagnetisation;
}

} // namespace ardor
```

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --build build -j8 --target pedal-tape-smoke \
  && ctest --test-dir build --output-on-failure -R pedal-tape-smoke
```

Expected: PASS, printing `tape smoke passed`.

If `testOddHarmonicsDominateAndGrow` or `testLoopOpens` fails, the fix is in `TapeHysteresis::Parameters`, not in the test. Sweep `coercivity` (loop width) and `anhystereticShape` (knee position) and record what you found in the commit body. Do **not** loosen a threshold to get green.

- [ ] **Step 6: Commit**

```bash
git add src/tape/TapeHysteresis.h src/tape/TapeHysteresis.cpp tests/tape_smoke.cpp CMakeLists.txt
git commit -F - <<'EOF'
feat(tape): model magnetic hysteresis with Jiles-Atherton

The core of the tape block. Magnetisation lags an anhysteretic Langevin curve
by an amount the coercivity sets, and that lag is the entire point: it gives
the block memory, so the harmonics depend on where the signal has recently been
and not only on where it is now. The soft-clip Saturation helper inside the
tape delay cannot do that at any setting, which is why it was not extended.

Solved with RK4 at the oversampled rate. RK4 is a fixed four derivative
evaluations per sample whatever the signal arrives, so unlike the wah's DK
solver its worst case equals its average case, which is what makes it safe
beside NAM.

Three numerical hazards are handled and each has a test. coth(x) - 1/x is the
difference of two diverging terms and loses all precision near zero, which is
where a quiet passage sits, so both Langevin functions switch to their Taylor
series below 1e-4 and a test walks the seam. The delta_M branch term keeps the
solver off unphysical branches when the field reverses inside a minor loop;
without it the state grows without bound on a signal that changes direction
thousands of times a second, which a guitar is. And the denominator passes
through zero at the loop tips, so it is floored away from zero with its sign
kept.

Tests assert the loop opens, the loop closes across cycles without drift, the
third harmonic dominates the second and grows monotonically with drive, and
that 400k samples of full-scale square leave the output finite and under 4x
saturation.
EOF
```

---

### Task 4: TapeTransport — wow, flutter, scrape and hiss

**Files:**
- Create: `src/tape/TapeTransport.h`, `src/tape/TapeTransport.cpp`
- Modify: `CMakeLists.txt` (add `src/tape/TapeTransport.cpp` to `ardor_tape`)
- Test: `tests/tape_smoke.cpp`

**Interfaces:**
- Consumes: `ardor::StereoSample` from `daisyfx/DaisyFxProcessor.h` (header-only use; `ardor_tape` does not link `ardor_daisyfx`, matching `ardor_rat`).
- Produces:
  - `void ardor::TapeTransport::configure(float sampleRate)`
  - `void setFlutter(float depth)` — 0…1
  - `void setHissDb(float db)` — -120…-60; -120 skips the generator entirely
  - `void reset()`
  - `StereoSample process(StereoSample input)`
  - `static constexpr float kHissOffDb = -120.0f;`

- [ ] **Step 1: Write the failing tests**

Add to `tests/tape_smoke.cpp`. Put the `#include "tape/TapeTransport.h"` next to the existing include, and add these functions above `main`, then call them from `main` after the existing five.

```cpp
// Correlates the output against a probe frequency and returns its magnitude.
double binMagnitude(const std::vector<float>& signal, float frequency, float rate)
{
  double real = 0.0;
  double imaginary = 0.0;
  for (std::size_t n = 0; n < signal.size(); ++n) {
    const double phase = kTwoPi * frequency * static_cast<double>(n) / rate;
    real += signal[n] * std::cos(phase);
    imaginary += signal[n] * std::sin(phase);
  }
  return 2.0 * std::sqrt(real * real + imaginary * imaginary) / static_cast<double>(signal.size());
}

constexpr float kHostRate = 48000.0f;

void testFlutterOffAddsNoSidebands()
{
  ardor::TapeTransport transport;
  transport.configure(kHostRate);
  transport.setFlutter(0.0f);
  transport.setHissDb(ardor::TapeTransport::kHissOffDb);
  transport.reset();

  std::vector<float> out;
  out.reserve(96000);
  for (std::size_t n = 0; n < 96000; ++n) {
    const float t = static_cast<float>(n) / kHostRate;
    const float in = 0.5f * std::sin(kTwoPi * 1000.0f * t);
    out.push_back(transport.process({in, in}).left);
  }
  // Drop the first 4800 samples so the delay line has filled.
  const std::vector<float> steady(out.begin() + 4800, out.end());

  const double carrier = binMagnitude(steady, 1000.0f, kHostRate);
  require(carrier > 0.4, "the carrier must survive the transport at flutter zero");
  // Wow at 0.7 Hz and flutter at 6 Hz would put sidebands here if the
  // modulator were running at all.
  for (const float offset : {0.7f, 6.0f, 11.0f}) {
    const double upper = binMagnitude(steady, 1000.0f + offset, kHostRate);
    require(upper < carrier * 0.002,
            "flutter at zero must add no sideband");
  }
}

void testHissOffIsExactSilence()
{
  ardor::TapeTransport transport;
  transport.configure(kHostRate);
  transport.setFlutter(0.0f);
  transport.setHissDb(ardor::TapeTransport::kHissOffDb);
  transport.reset();

  for (std::size_t n = 0; n < 48000; ++n) {
    const auto out = transport.process({0.0f, 0.0f});
    require(out.left == 0.0f && out.right == 0.0f,
            "hiss off must be a hard off, not a quiet generator");
  }
}

void testHissOnIsAudibleAndUncorrelated()
{
  ardor::TapeTransport transport;
  transport.configure(kHostRate);
  transport.setFlutter(0.0f);
  transport.setHissDb(-70.0f);
  transport.reset();

  double leftEnergy = 0.0;
  double crossEnergy = 0.0;
  double rightEnergy = 0.0;
  for (std::size_t n = 0; n < 192000; ++n) {
    const auto out = transport.process({0.0f, 0.0f});
    leftEnergy += static_cast<double>(out.left) * out.left;
    rightEnergy += static_cast<double>(out.right) * out.right;
    crossEnergy += static_cast<double>(out.left) * out.right;
  }
  require(leftEnergy > 0.0 && rightEnergy > 0.0, "hiss on must produce noise");

  // Tape noise is physically uncorrelated between channels, so the normalised
  // cross-correlation must sit near zero. A shared generator would give 1.
  const double correlation = crossEnergy / std::sqrt(leftEnergy * rightEnergy);
  require(std::fabs(correlation) < 0.05,
          "hiss must be independent per channel");
}

void testFlutterIsSharedAcrossChannels()
{
  ardor::TapeTransport transport;
  transport.configure(kHostRate);
  transport.setFlutter(1.0f);
  transport.setHissDb(ardor::TapeTransport::kHissOffDb);
  transport.reset();

  // One reel passes one capstan, so both channels must be modulated by the
  // same transport. Independent modulation would tear the stereo image apart.
  for (std::size_t n = 0; n < 192000; ++n) {
    const float t = static_cast<float>(n) / kHostRate;
    const float in = 0.5f * std::sin(kTwoPi * 440.0f * t);
    const auto out = transport.process({in, in});
    require(out.left == out.right,
            "identical input must give identical output: one transport, both channels");
  }
}

void testFlutterFullScaleMovesPitch()
{
  ardor::TapeTransport transport;
  transport.configure(kHostRate);
  transport.setFlutter(1.0f);
  transport.setHissDb(ardor::TapeTransport::kHissOffDb);
  transport.reset();

  std::vector<float> out;
  out.reserve(192000);
  for (std::size_t n = 0; n < 192000; ++n) {
    const float t = static_cast<float>(n) / kHostRate;
    const float in = 0.5f * std::sin(kTwoPi * 1000.0f * t);
    out.push_back(transport.process({in, in}).left);
  }
  const std::vector<float> steady(out.begin() + 4800, out.end());

  const double carrier = binMagnitude(steady, 1000.0f, kHostRate);
  const double sideband = binMagnitude(steady, 1000.7f, kHostRate);
  require(sideband > carrier * 0.0005,
          "full-scale flutter must actually move the pitch");
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build -j8 --target pedal-tape-smoke
```

Expected: FAIL to compile, `tape/TapeTransport.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `src/tape/TapeTransport.h`:

```cpp
#pragma once

#include "daisyfx/DaisyFxProcessor.h"

#include <array>
#include <cstdint>

namespace ardor {

// Wow, flutter, scrape flutter and hiss.
//
// One transport drives both channels. A stereo pair runs on one reel past one
// capstan, so wow and flutter are correlated between the channels by physics;
// independent modulation would tear the stereo image apart. Hiss is the
// opposite case — tape noise is uncorrelated — so each channel gets its own
// generator.
//
// Full-scale flutter is deliberately about ten times a real Studer A800, which
// holds wow and flutter near 0.03% DIN weighted. At the real figure the control
// would be inaudible across its whole range and would read as broken. The
// realistic setting is near 0.1.
class TapeTransport {
public:
  // Below this the noise generator is skipped entirely rather than run at a
  // low level, so a preset with hiss off is exactly silent.
  static constexpr float kHissOffDb = -120.0f;

  void configure(float sampleRate);
  void setFlutter(float depth);   // 0..1
  void setHissDb(float db);       // kHissOffDb..-60
  void reset();
  StereoSample process(StereoSample input);

private:
  // Peak pitch deviation each component contributes at full scale, as a
  // fraction. A sinusoidal delay modulation of amplitude A at rate f gives a
  // peak pitch deviation of A*2*pi*f, so the delay amplitudes below are these
  // figures divided by their own 2*pi*f.
  struct Component {
    float rateHz;
    float pitchDeviation;
  };
  static constexpr std::array<Component, 3> kComponents = {{
    {0.7f, 0.0015f},  // wow
    {6.0f, 0.0010f},  // flutter
    {11.0f, 0.0005f}, // flutter, second mode
  }};
  static constexpr float kScrapeRateHz = 250.0f;
  static constexpr float kScrapeDeviation = 0.0002f;

  // Nominal read offset, comfortably above the largest swing the components
  // can produce so the read pointer never crosses the write pointer.
  static constexpr std::size_t kNominalDelay = 48;
  static constexpr std::size_t kBufferSize = 256; // power of two, masked index
  static constexpr std::size_t kBufferMask = kBufferSize - 1U;

  float readDelayed(const std::array<float, kBufferSize>& buffer, float delay) const;
  float nextModulation();
  float nextNoise(std::uint32_t& state) const;

  float sampleRate_ = 48000.0f;
  float flutter_ = 0.0f;
  float hissGain_ = 0.0f;
  bool hissEnabled_ = false;

  std::array<float, 3> phase_{};
  std::array<float, 3> phaseStep_{};
  std::array<float, 3> delayAmplitude_{};
  float scrapeAmplitude_ = 0.0f;
  float scrapeState_ = 0.0f;
  float scrapeCoefficient_ = 0.0f;

  std::array<float, kBufferSize> left_{};
  std::array<float, kBufferSize> right_{};
  std::size_t write_ = 0;

  // Separate seeds so the two channels' noise is independent.
  std::uint32_t leftNoise_ = 0x9E3779B9u;
  std::uint32_t rightNoise_ = 0x85EBCA6Bu;
};

} // namespace ardor
```

- [ ] **Step 4: Write the implementation**

Create `src/tape/TapeTransport.cpp`:

```cpp
#include "tape/TapeTransport.h"

#include <algorithm>
#include <cmath>

namespace ardor {

namespace {
constexpr float kTwoPi = 6.28318530718f;
} // namespace

void TapeTransport::configure(float sampleRate)
{
  sampleRate_ = (std::isfinite(sampleRate) && sampleRate > 0.0f) ? sampleRate : 48000.0f;

  for (std::size_t i = 0; i < kComponents.size(); ++i) {
    phaseStep_[i] = kTwoPi * kComponents[i].rateHz / sampleRate_;
    // A sinusoidal delay modulation of amplitude A at rate f produces a peak
    // pitch deviation of A * 2*pi*f, so invert that for the amplitude needed.
    delayAmplitude_[i] =
      kComponents[i].pitchDeviation * sampleRate_ / (kTwoPi * kComponents[i].rateHz);
  }
  scrapeAmplitude_ = kScrapeDeviation * sampleRate_ / (kTwoPi * kScrapeRateHz);
  // One-pole low pass that shapes white noise into scrape flutter.
  scrapeCoefficient_ = 1.0f - std::exp(-kTwoPi * kScrapeRateHz / sampleRate_);
  reset();
}

void TapeTransport::setFlutter(float depth)
{
  flutter_ = std::clamp(depth, 0.0f, 1.0f);
}

void TapeTransport::setHissDb(float db)
{
  hissEnabled_ = db > kHissOffDb;
  hissGain_ = hissEnabled_ ? std::pow(10.0f, db / 20.0f) : 0.0f;
}

void TapeTransport::reset()
{
  phase_.fill(0.0f);
  scrapeState_ = 0.0f;
  left_.fill(0.0f);
  right_.fill(0.0f);
  write_ = 0;
  leftNoise_ = 0x9E3779B9u;
  rightNoise_ = 0x85EBCA6Bu;
}

float TapeTransport::nextNoise(std::uint32_t& state) const
{
  // xorshift32: cheap, allocation-free and good enough for a noise floor.
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return static_cast<float>(static_cast<std::int32_t>(state)) * (1.0f / 2147483648.0f);
}

float TapeTransport::nextModulation()
{
  float offset = 0.0f;
  for (std::size_t i = 0; i < kComponents.size(); ++i) {
    offset += delayAmplitude_[i] * std::sin(phase_[i]);
    phase_[i] += phaseStep_[i];
    if (phase_[i] > kTwoPi) phase_[i] -= kTwoPi;
  }
  // Scrape flutter is broadband, not a tone, so it is filtered noise.
  scrapeState_ += scrapeCoefficient_ * (nextNoise(leftNoise_) - scrapeState_);
  offset += scrapeAmplitude_ * scrapeState_;
  return offset * flutter_;
}

float TapeTransport::readDelayed(const std::array<float, kBufferSize>& buffer, float delay) const
{
  const float position = static_cast<float>(write_ + kBufferSize) - delay;
  const auto base = static_cast<std::size_t>(position);
  const float fraction = position - static_cast<float>(base);

  // Third-order Lagrange. Linear interpolation would low-pass the signal in
  // step with the modulation, which reads as a wobbling tone control rather
  // than as pitch movement.
  const float y0 = buffer[(base - 1U) & kBufferMask];
  const float y1 = buffer[base & kBufferMask];
  const float y2 = buffer[(base + 1U) & kBufferMask];
  const float y3 = buffer[(base + 2U) & kBufferMask];

  const float d1 = fraction - 1.0f;
  const float d2 = fraction - 2.0f;
  const float dp1 = fraction + 1.0f;

  return y0 * (-fraction * d1 * d2 / 6.0f)
       + y1 * (dp1 * d1 * d2 / 2.0f)
       + y2 * (-dp1 * fraction * d2 / 2.0f)
       + y3 * (dp1 * fraction * d1 / 6.0f);
}

StereoSample TapeTransport::process(StereoSample input)
{
  left_[write_] = input.left;
  right_[write_] = input.right;
  write_ = (write_ + 1U) & kBufferMask;

  const float delay = static_cast<float>(kNominalDelay) + nextModulation();
  StereoSample output{readDelayed(left_, delay), readDelayed(right_, delay)};

  if (hissEnabled_) {
    output.left += hissGain_ * nextNoise(leftNoise_);
    output.right += hissGain_ * nextNoise(rightNoise_);
  }
  return output;
}

} // namespace ardor
```

- [ ] **Step 5: Add the source to CMake and run the tests**

In `CMakeLists.txt`, add `src/tape/TapeTransport.cpp` to the `ardor_tape` source list.

```bash
cmake --build build -j8 --target pedal-tape-smoke \
  && ctest --test-dir build --output-on-failure -R pedal-tape-smoke
```

Expected: PASS.

Note `testHissOffIsExactSilence` requires the delay-line read of an all-zero buffer to be exactly `0.0f`. Lagrange interpolation of zeros is a sum of products with zero, so this holds. If it does not, the bug is uninitialised buffer storage, not the test.

- [ ] **Step 6: Commit**

```bash
git add src/tape/TapeTransport.h src/tape/TapeTransport.cpp tests/tape_smoke.cpp CMakeLists.txt
git commit -F - <<'EOF'
feat(tape): model the transport, off by default

Wow at 0.7 Hz, flutter at 6 and 11 Hz, scrape flutter as noise filtered around
250 Hz, and hiss.

One transport drives both channels, because one reel passes one capstan. A test
asserts identical input gives bit-identical output on both channels at full
flutter; independent modulators would tear the stereo image apart. Hiss is the
opposite case and gets a generator per channel, because tape noise is
physically uncorrelated — a test requires the normalised cross-correlation of
192k samples to stay under 0.05.

The component amplitudes are derived rather than dialled in. A sinusoidal delay
modulation of amplitude A at rate f gives a peak pitch deviation of A*2*pi*f,
so each component's delay amplitude is its target deviation divided by its own
2*pi*f. The targets sum to about 0.32% at full scale, roughly ten times a real
A800, which holds 0.03% DIN weighted. That exaggeration is deliberate: at the
real figure the control would be inaudible across its whole range and would
read as broken. The realistic setting is near 0.1.

Hiss off is a hard off — the generator is skipped, not run quietly — so a
preset with the transport at its defaults is exactly silent, which a test
requires over 48000 samples rather than merely checking a small number.

Reads use third-order Lagrange. Linear interpolation low-passes in step with
the modulation, which reads as a wobbling tone control rather than pitch.
EOF
```

---

### Task 5: TapeProcessor — the block

Assembles the signal path: drive with calibrated makeup, record pre-emphasis, 8x oversampled hysteresis, de-emphasis, head bump, transport, output trim, DC block, and a latency-matched dry mix.

**Files:**
- Create: `src/tape/TapeProcessor.h`, `src/tape/TapeProcessor.cpp`
- Modify: `CMakeLists.txt` (add `src/tape/TapeProcessor.cpp` to `ardor_tape`)
- Test: `tests/tape_smoke.cpp`

**Interfaces:**
- Consumes: `ardor::TapeHysteresis` (Task 3), `ardor::TapeTransport` (Task 4), `ardor::makeHighShelf` and `ardor::makePeakingEq` (Task 2 and existing), `pedal::HalfbandInterpolator2x` / `pedal::HalfbandDecimator2x`, `pedal::DcBlocker`.
- Produces: the house block contract —
  - `bool configure(const nlohmann::json& params, float sampleRate, std::string& error)`
  - `bool setParameterTarget(const std::string& key, float value)` — accepts `drive`, `saturation`, `bias`, `head_bump`, `flutter`, `hiss_db`, `mix`, `output_db`. Returns `false` for `speed`, which is a load-time choice.
  - `void reset()`
  - `StereoSample process(StereoSample input)`
  - `std::size_t latencyFrames() const noexcept { return 26; }`

- [ ] **Step 1: Write the failing tests**

Add to `tests/tape_smoke.cpp`, with `#include "tape/TapeProcessor.h"` alongside the others, and call these from `main`.

```cpp
ardor::TapeProcessor makeTape(const nlohmann::json& overrides)
{
  nlohmann::json params;
  params["mode"] = "tape";
  params["drive"] = 0.0f;
  params["saturation"] = 0.5f;
  params["bias"] = 0.5f;
  params["speed"] = "15";
  params["head_bump"] = 0.5f;
  params["flutter"] = 0.0f;
  params["hiss_db"] = -120.0f;
  params["mix"] = 1.0f;
  params["output_db"] = 0.0f;
  for (const auto& item : overrides.items()) params[item.key()] = item.value();

  ardor::TapeProcessor tape;
  std::string error;
  if (!tape.configure(params, kHostRate, error)) throw std::runtime_error(error);
  tape.reset();
  return tape;
}

std::vector<float> renderTone(ardor::TapeProcessor& tape, float frequency, float amplitude,
                              std::size_t frames)
{
  std::vector<float> out;
  out.reserve(frames);
  for (std::size_t n = 0; n < frames; ++n) {
    const float t = static_cast<float>(n) / kHostRate;
    const float in = amplitude * std::sin(kTwoPi * frequency * t);
    out.push_back(tape.process({in, in}).left);
  }
  return out;
}

double rms(const std::vector<float>& signal, std::size_t skip)
{
  double sum = 0.0;
  for (std::size_t n = skip; n < signal.size(); ++n) sum += static_cast<double>(signal[n]) * signal[n];
  return std::sqrt(sum / static_cast<double>(signal.size() - skip));
}

void testOversamplingSuppressesAliases()
{
  // A 12 kHz tone at full drive throws harmonics at 24, 36, 48 kHz and beyond.
  // Solved at 48 kHz they would fold straight back into the audio band. This
  // is the test that proves the 8x oversampling earns its cost.
  auto tape = makeTape({{"drive", 24.0f}, {"saturation", 1.0f}});
  const auto out = renderTone(tape, 12000.0f, 0.9f, 131072);
  const std::vector<float> steady(out.begin() + 8192, out.end());

  const double carrier = binMagnitude(steady, 12000.0f, kHostRate);
  require(carrier > 0.0, "the carrier must survive");

  // Every harmonic of 12 kHz folds to either 12 kHz or DC, so any energy at an
  // unrelated frequency is an alias. Probe a spread of non-harmonic bins.
  double worst = 0.0;
  for (const float probe : {700.0f, 1900.0f, 3300.0f, 5100.0f, 7700.0f, 9300.0f, 15100.0f, 17300.0f}) {
    worst = std::max(worst, binMagnitude(steady, probe, kHostRate));
  }
  const double worstDbc = 20.0 * std::log10(worst / carrier);
  std::printf("  worst in-band alias: %.1f dBc\n", worstDbc);
  require(worstDbc < -70.0, "in-band aliases must stay under -70 dBc");
}

// Magnitude response in dB at one frequency, measured through the block.
double responseDb(ardor::TapeProcessor& tape, float frequency)
{
  const auto out = renderTone(tape, frequency, 0.05f, 65536);
  const std::vector<float> steady(out.begin() + 8192, out.end());
  return 20.0 * std::log10(binMagnitude(steady, frequency, kHostRate) / 0.05);
}

void testHeadBumpFollowsSpeed()
{
  auto fast = makeTape({{"speed", "30"}, {"head_bump", 1.0f}});
  auto slow = makeTape({{"speed", "15"}, {"head_bump", 1.0f}});
  auto flat = makeTape({{"speed", "15"}, {"head_bump", 0.0f}});

  const double slowAt45 = responseDb(slow, 45.0f);
  const double fastAt45 = responseDb(fast, 45.0f);
  const double fastAt90 = responseDb(fast, 90.0f);
  const double flatAt45 = responseDb(flat, 45.0f);

  require(slowAt45 > 1.5, "15 ips must show a head bump near 45 Hz");
  require(slowAt45 > fastAt45 + 1.0, "the 15 ips bump must be lower in frequency than the 30 ips one");
  require(fastAt90 > 0.8, "30 ips must show its smaller bump near 90 Hz");
  require(std::fabs(flatAt45) < 0.3, "head_bump at zero must leave the low end flat");
}

void testMixZeroIsTransparent()
{
  // This simultaneously proves the dry path is delay-matched. If the dry
  // signal were not delayed by the same 26.25 samples the wet path costs,
  // mix would comb rather than blend, and mix=0 would still be wrong because
  // the block would pass the input at the wrong time.
  auto tape = makeTape({{"mix", 0.0f}, {"drive", 24.0f}});

  std::vector<float> input;
  std::vector<float> output;
  for (std::size_t n = 0; n < 32768; ++n) {
    const float t = static_cast<float>(n) / kHostRate;
    const float in = 0.4f * std::sin(kTwoPi * 220.0f * t) + 0.2f * std::sin(kTwoPi * 1310.0f * t);
    input.push_back(in);
    output.push_back(tape.process({in, in}).left);
  }

  // Compare the output against the input delayed by the reported latency.
  const std::size_t latency = tape.latencyFrames();
  double errorEnergy = 0.0;
  double signalEnergy = 0.0;
  for (std::size_t n = 8192; n < input.size(); ++n) {
    const double reference = input[n - latency];
    const double difference = output[n] - reference;
    errorEnergy += difference * difference;
    signalEnergy += reference * reference;
  }
  const double errorDb = 10.0 * std::log10(errorEnergy / signalEnergy);
  std::printf("  mix=0 error: %.1f dB\n", errorDb);
  require(errorDb < -80.0, "mix at zero must reproduce the input within -80 dB");
}

void testDriveIsGainCompensated()
{
  // Drive must change character, not loudness. The compensation is calibrated
  // at configure() time by running a probe tone through a scratch copy of the
  // magnetics, so this is a check on that calibration.
  double quietest = 1.0e9;
  double loudest = -1.0e9;
  for (const float drive : {-12.0f, -6.0f, 0.0f, 6.0f, 12.0f, 18.0f, 24.0f}) {
    auto tape = makeTape({{"drive", drive}});
    const auto out = renderTone(tape, 440.0f, 0.3f, 32768);
    const double level = 20.0 * std::log10(rms(out, 8192));
    quietest = std::min(quietest, level);
    loudest = std::max(loudest, level);
  }
  std::printf("  drive level swing: %.2f dB\n", loudest - quietest);
  require(loudest - quietest < 3.0, "drive must move the output level less than 3 dB");
}

void testResetReturnsToConstructedState()
{
  auto tape = makeTape({{"drive", 18.0f}});
  const auto first = renderTone(tape, 440.0f, 0.5f, 8192);
  tape.reset();
  const auto second = renderTone(tape, 440.0f, 0.5f, 8192);
  for (std::size_t n = 0; n < first.size(); ++n) {
    require(first[n] == second[n], "reset must return the block to its constructed state");
  }
}

void testRejectsBadConfiguration()
{
  ardor::TapeProcessor tape;
  std::string error;
  nlohmann::json params;
  params["mode"] = "tape";
  require(!tape.configure(params, 0.0f, error), "a zero sample rate must be rejected");
  require(!error.empty(), "a rejected configuration must say why");

  params["mode"] = "rat";
  require(!tape.configure(params, kHostRate, error), "the wrong mode must be rejected");

  // speed is a load-time choice, not a live control.
  params["mode"] = "tape";
  require(tape.configure(params, kHostRate, error), "defaults must configure cleanly");
  require(!tape.setParameterTarget("speed", 30.0f), "speed must not be live-settable");
  require(tape.setParameterTarget("drive", 6.0f), "drive must be live-settable");
  require(!tape.setParameterTarget("nonsense", 1.0f), "unknown keys must be rejected");
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build -j8 --target pedal-tape-smoke
```

Expected: FAIL to compile, `tape/TapeProcessor.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `src/tape/TapeProcessor.h`:

```cpp
#pragma once

#include "daisyfx/DaisyFxProcessor.h"
#include "daisyfx/hosted/dsp/dc_blocker.h"
#include "daisyfx/hosted/dsp/halfband_resampler.h"
#include "equalizer/ParametricEqMath.h"
#include "tape/TapeHysteresis.h"
#include "tape/TapeTransport.h"

#include <array>
#include <cstddef>
#include <nlohmann/json.hpp>
#include <string>

namespace ardor {

// A studio tape machine, voiced after a Studer A800.
//
// Signal path, per sample:
//
//   drive trim -> record pre-emphasis -> [8x: Jiles-Atherton] -> de-emphasis
//   -> head bump -> transport -> hiss -> output trim -> DC block
//   -> mix against the latency-matched dry signal
//
// Two things about this machine are commonly got wrong, so they are stated
// here rather than left to be re-derived.
//
// Speed does not switch a treble filter in. With a playback gap near 2 um at
// 15 ips the first gap-loss null lands around 190 kHz, and an A800 is flat to
// 20 kHz at both speeds. What speed really changes is the head bump — about
// +2.5 dB near 45 Hz at 15 ips against a smaller one near 90 Hz at 30 ips —
// and the IEC emphasis constant, 35 us against 17.5 us.
//
// Bias is the one phenomenological control here. Real AC bias is an oscillator
// above 100 kHz; the internal Nyquist is 192 kHz and the anti-imaging
// halfbands do not pass anything near it, so simulating it would produce
// aliasing rather than realism. Bias instead maps to what bias controls: the
// coercivity, and an over-bias high-frequency loss.
class TapeProcessor {
public:
  TapeProcessor() = default;
  TapeProcessor(TapeProcessor&&) noexcept = default;
  TapeProcessor& operator=(TapeProcessor&&) noexcept = default;

  bool configure(const nlohmann::json& params, float sampleRate, std::string& error);
  bool setParameterTarget(const std::string& key, float value);
  void reset();
  StereoSample process(StereoSample input);

  // Six halfband stages, 15 samples each at 96, 192, 384, 384, 192 and 96 kHz,
  // which is 26.25 at the host rate. Reported rounded, matching RatProcessor;
  // the internal dry-path compensation uses the exact figure.
  std::size_t latencyFrames() const noexcept { return 26; }

private:
  TapeProcessor(const TapeProcessor&) = delete;
  TapeProcessor& operator=(const TapeProcessor&) = delete;

  struct Biquad {
    BiquadCoefficients coefficients{};
    float x1 = 0.0f;
    float x2 = 0.0f;
    float y1 = 0.0f;
    float y2 = 0.0f;

    float process(float x)
    {
      const float y = coefficients.b0 * x + coefficients.b1 * x1 + coefficients.b2 * x2
                    - coefficients.a1 * y1 - coefficients.a2 * y2;
      x2 = x1;
      x1 = x;
      y2 = y1;
      y1 = y;
      return y;
    }

    void clear() { x1 = x2 = y1 = y2 = 0.0f; }
  };

  // One oversampled, hysteresis-bearing lane. The machine is stereo, so there
  // are two, and each carries its own resamplers and solver state.
  struct Lane {
    pedal::HalfbandInterpolator2x up2x;
    pedal::HalfbandInterpolator2x up4x;
    pedal::HalfbandInterpolator2x up8x;
    pedal::HalfbandDecimator2x down4x;
    pedal::HalfbandDecimator2x down2x;
    pedal::HalfbandDecimator2x down1x;
    TapeHysteresis core;
    Biquad preEmphasis;
    Biquad deEmphasis;
    Biquad biasLoss;
    Biquad bumpPeak;
    Biquad bumpDip;
    pedal::DcBlocker dc;
  };

  static constexpr std::size_t kOversampling = 8;
  static constexpr float kLatencyFrames = 26.25f;
  static constexpr std::size_t kDryBufferSize = 64; // power of two, masked index
  static constexpr std::size_t kDryBufferMask = kDryBufferSize - 1U;

  void rebuildFilters();
  void calibrateDriveMakeup();
  float driveMakeup(float driveDb) const;
  float processLane(Lane& lane, float input);
  float readDry(const std::array<float, kDryBufferSize>& buffer, std::size_t write) const;

  float sampleRate_ = 48000.0f;
  float smoothing_ = 0.0f;
  bool fastSpeed_ = false; // false is 15 ips, true is 30 ips

  float driveDbTarget_ = 0.0f;
  float saturationTarget_ = 0.5f;
  float biasTarget_ = 0.5f;
  float headBumpTarget_ = 0.5f;
  float mixTarget_ = 1.0f;
  float outputTarget_ = 1.0f;

  float driveDb_ = 0.0f;
  float mix_ = 1.0f;
  float output_ = 1.0f;
  float driveGain_ = 1.0f;
  float makeup_ = 1.0f;

  // Makeup measured at configure() time by running a probe tone through a
  // scratch copy of the magnetics at each of these drive settings. Nothing
  // here is hand-tuned; interpolating between measurements is what keeps the
  // level steady as drive moves.
  static constexpr std::size_t kCalibrationPoints = 13;
  static constexpr float kCalibrationMinDb = -12.0f;
  static constexpr float kCalibrationMaxDb = 24.0f;
  std::array<float, kCalibrationPoints> makeupTable_{};

  Lane left_{};
  Lane right_{};
  TapeTransport transport_{};

  std::array<float, kDryBufferSize> dryLeft_{};
  std::array<float, kDryBufferSize> dryRight_{};
  std::size_t dryWrite_ = 0;
};

} // namespace ardor
```

- [ ] **Step 4: Write the implementation**

Create `src/tape/TapeProcessor.cpp`:

```cpp
#include "tape/TapeProcessor.h"

#include <algorithm>
#include <cmath>

namespace ardor {

namespace {

constexpr float kTwoPi = 6.28318530718f;

// The field, in A/m, that a full-scale sample produces at the record head at
// unity drive. Chosen so a nominal signal sits in the knee of the default
// hysteresis fit rather than at either extreme.
constexpr float kFieldPerSample = 2.5e4f;

// The emphasis boost is capped rather than left as a true 6 dB/octave ramp,
// which would be unbounded. Twelve decibels is reached about two octaves above
// the corner, which is where a real machine's record amplifier gives up too.
constexpr float kEmphasisDb = 12.0f;

float clampedNumber(const nlohmann::json& params, const char* key, float fallback,
                    float low, float high)
{
  if (!params.is_object()) return fallback;
  const auto it = params.find(key);
  if (it == params.end() || !it->is_number()) return fallback;
  const float value = it->get<float>();
  return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}

} // namespace

bool TapeProcessor::configure(const nlohmann::json& params, float sampleRate, std::string& error)
{
  error.clear();
  if (!std::isfinite(sampleRate) || sampleRate <= 0.0f) {
    error = "tape sample rate must be finite and positive";
    return false;
  }
  const auto mode = params.value("mode", std::string{"tape"});
  if (mode != "tape") {
    error = "unsupported distortion mode: " + mode;
    return false;
  }
  const auto speed = params.value("speed", std::string{"15"});
  if (speed != "15" && speed != "30") {
    error = "tape speed must be 15 or 30";
    return false;
  }

  sampleRate_ = sampleRate;
  fastSpeed_ = speed == "30";

  driveDbTarget_ = clampedNumber(params, "drive", 0.0f, -12.0f, 24.0f);
  saturationTarget_ = clampedNumber(params, "saturation", 0.5f, 0.0f, 1.0f);
  biasTarget_ = clampedNumber(params, "bias", 0.5f, 0.0f, 1.0f);
  headBumpTarget_ = clampedNumber(params, "head_bump", 0.5f, 0.0f, 1.0f);
  mixTarget_ = clampedNumber(params, "mix", 1.0f, 0.0f, 1.0f);
  const float outputDb = clampedNumber(params, "output_db", 0.0f, -24.0f, 24.0f);
  outputTarget_ = std::pow(10.0f, outputDb / 20.0f);

  const float flutter = clampedNumber(params, "flutter", 0.0f, 0.0f, 1.0f);
  const float hissDb = clampedNumber(params, "hiss_db", TapeTransport::kHissOffDb,
                                     TapeTransport::kHissOffDb, -60.0f);

  // A knob turn has to reach the magnetics without stepping.
  constexpr float kSmoothingSeconds = 0.015f;
  smoothing_ = 1.0f - std::exp(-1.0f / (kSmoothingSeconds * sampleRate_));

  const float oversampledRate = sampleRate_ * static_cast<float>(kOversampling);
  for (Lane* lane : {&left_, &right_}) {
    lane->core.configure(TapeHysteresis::defaultParameters(), oversampledRate);
    lane->dc.Init(sampleRate_);
  }
  transport_.configure(sampleRate_);
  transport_.setFlutter(flutter);
  transport_.setHissDb(hissDb);

  rebuildFilters();
  calibrateDriveMakeup();
  reset();
  return true;
}

void TapeProcessor::rebuildFilters()
{
  // Saturation lowers the anhysteretic shape parameter, which brings the knee
  // in earlier and makes it harder. Bias moves the coercivity: under-bias
  // widens the loop and distorts more, over-bias narrows it and trades that
  // for high-frequency loss.
  auto parameters = TapeHysteresis::defaultParameters();
  parameters.anhystereticShape *= 1.6f - saturationTarget_;
  parameters.coercivity *= 1.6f - biasTarget_;

  const float oversampledRate = sampleRate_ * static_cast<float>(kOversampling);

  // IEC emphasis: 35 us at 15 ips, 17.5 us at 30 ips.
  const float emphasisTau = fastSpeed_ ? 17.5e-6f : 35.0e-6f;
  const float emphasisHz = 1.0f / (kTwoPi * emphasisTau);

  // Head bump: the finite head core puts a low-frequency ripple in the
  // playback response. Bigger and lower at the slower speed.
  const float bumpHz = fastSpeed_ ? 90.0f : 45.0f;
  const float bumpDb = (fastSpeed_ ? 1.5f : 2.5f) * headBumpTarget_;

  // Over-bias costs high frequencies. Below the centre detent there is no loss.
  const float overBias = std::max(0.0f, biasTarget_ - 0.5f) * 2.0f;
  const float biasLossDb = -4.0f * overBias;

  for (Lane* lane : {&left_, &right_}) {
    // Pre-emphasis runs at the oversampled rate because it shapes what reaches
    // the magnetics; de-emphasis runs there too so the pair stays exact.
    lane->preEmphasis.coefficients =
      makeHighShelf(oversampledRate, emphasisHz, 0.707f, kEmphasisDb);
    lane->deEmphasis.coefficients =
      makeHighShelf(oversampledRate, emphasisHz, 0.707f, -kEmphasisDb);
    lane->biasLoss.coefficients =
      makeHighShelf(sampleRate_, 6000.0f, 0.707f, biasLossDb);
    lane->bumpPeak.coefficients = makePeakingEq(sampleRate_, bumpHz, 1.2f, bumpDb);
    lane->bumpDip.coefficients =
      makePeakingEq(sampleRate_, bumpHz / 2.2f, 1.5f, -0.5f * bumpDb);
    lane->core.configure(parameters, oversampledRate);
  }
}

void TapeProcessor::calibrateDriveMakeup()
{
  // Drive must change character, not loudness. Rather than carry a hand-tuned
  // table, measure: run a probe tone through a scratch copy of the magnetics
  // at each calibration point and store the reciprocal of what came out. This
  // runs at load time, never in the audio callback.
  const float oversampledRate = sampleRate_ * static_cast<float>(kOversampling);
  auto parameters = TapeHysteresis::defaultParameters();
  parameters.anhystereticShape *= 1.6f - saturationTarget_;
  parameters.coercivity *= 1.6f - biasTarget_;

  constexpr float kProbeHz = 1000.0f;
  constexpr float kProbeAmplitude = 0.3f;
  const auto settle = static_cast<std::size_t>(oversampledRate / kProbeHz) * 4;
  const auto measure = static_cast<std::size_t>(oversampledRate / kProbeHz) * 16;

  double reference = 0.0;
  for (std::size_t point = 0; point < kCalibrationPoints; ++point) {
    const float driveDb = kCalibrationMinDb
      + (kCalibrationMaxDb - kCalibrationMinDb) * static_cast<float>(point)
        / static_cast<float>(kCalibrationPoints - 1);
    const float gain = std::pow(10.0f, driveDb / 20.0f);

    TapeHysteresis probe;
    probe.configure(parameters, oversampledRate);
    probe.reset();

    for (std::size_t n = 0; n < settle; ++n) {
      const float t = static_cast<float>(n) / oversampledRate;
      probe.process(gain * kProbeAmplitude * kFieldPerSample * std::sin(kTwoPi * kProbeHz * t));
    }
    double energy = 0.0;
    for (std::size_t n = 0; n < measure; ++n) {
      const float t = static_cast<float>(settle + n) / oversampledRate;
      const float out =
        probe.process(gain * kProbeAmplitude * kFieldPerSample * std::sin(kTwoPi * kProbeHz * t));
      energy += static_cast<double>(out) * out;
    }
    const double level = std::sqrt(energy / static_cast<double>(measure));
    if (point == 0) reference = level;
    makeupTable_[point] =
      level > 1.0e-9 ? static_cast<float>(reference / level) : 1.0f;
  }
}

float TapeProcessor::driveMakeup(float driveDb) const
{
  const float span = kCalibrationMaxDb - kCalibrationMinDb;
  const float position = (std::clamp(driveDb, kCalibrationMinDb, kCalibrationMaxDb)
                          - kCalibrationMinDb) / span * static_cast<float>(kCalibrationPoints - 1);
  const auto low = static_cast<std::size_t>(position);
  const std::size_t high = std::min(low + 1U, kCalibrationPoints - 1U);
  const float fraction = position - static_cast<float>(low);
  return makeupTable_[low] + fraction * (makeupTable_[high] - makeupTable_[low]);
}

bool TapeProcessor::setParameterTarget(const std::string& key, float value)
{
  if (!std::isfinite(value)) return false;
  if (key == "drive") {
    driveDbTarget_ = std::clamp(value, -12.0f, 24.0f);
    return true;
  }
  if (key == "output_db") {
    outputTarget_ = std::pow(10.0f, std::clamp(value, -24.0f, 24.0f) / 20.0f);
    return true;
  }
  if (key == "mix") {
    mixTarget_ = std::clamp(value, 0.0f, 1.0f);
    return true;
  }
  if (key == "flutter") {
    transport_.setFlutter(std::clamp(value, 0.0f, 1.0f));
    return true;
  }
  if (key == "hiss_db") {
    transport_.setHissDb(std::clamp(value, TapeTransport::kHissOffDb, -60.0f));
    return true;
  }
  // Saturation, bias and head bump change filter and solver coefficients, so
  // they rebuild rather than smooth. They are knob moves, not automation
  // targets, and the rebuild is a handful of biquad computations.
  if (key == "saturation") {
    saturationTarget_ = std::clamp(value, 0.0f, 1.0f);
    rebuildFilters();
    calibrateDriveMakeup();
    return true;
  }
  if (key == "bias") {
    biasTarget_ = std::clamp(value, 0.0f, 1.0f);
    rebuildFilters();
    calibrateDriveMakeup();
    return true;
  }
  if (key == "head_bump") {
    headBumpTarget_ = std::clamp(value, 0.0f, 1.0f);
    rebuildFilters();
    return true;
  }
  // speed is a load-time choice: it is a `choice` control in the catalog and
  // reaches the engine by rebuilding the chain.
  return false;
}

void TapeProcessor::reset()
{
  for (Lane* lane : {&left_, &right_}) {
    lane->up2x.Reset();
    lane->up4x.Reset();
    lane->up8x.Reset();
    lane->down4x.Reset();
    lane->down2x.Reset();
    lane->down1x.Reset();
    lane->core.reset();
    lane->preEmphasis.clear();
    lane->deEmphasis.clear();
    lane->biasLoss.clear();
    lane->bumpPeak.clear();
    lane->bumpDip.clear();
    lane->dc.Init(sampleRate_);
  }
  transport_.reset();
  dryLeft_.fill(0.0f);
  dryRight_.fill(0.0f);
  dryWrite_ = 0;

  driveDb_ = driveDbTarget_;
  mix_ = mixTarget_;
  output_ = outputTarget_;
  driveGain_ = std::pow(10.0f, driveDb_ / 20.0f);
  makeup_ = driveMakeup(driveDb_);
}

float TapeProcessor::processLane(Lane& lane, float input)
{
  const float field = input * driveGain_ * kFieldPerSample;

  const auto at2x = lane.up2x.Process(field);
  float at2xFiltered[2]{};
  for (std::size_t i = 0; i < 2; ++i) {
    const auto at4x = lane.up4x.Process(at2x[i]);
    for (const float quarterRate : at4x) {
      const auto at8x = lane.up8x.Process(quarterRate);
      float at4xFiltered = 0.0f;
      for (const float sample : at8x) {
        // Pre-emphasis makes high frequencies reach the magnetics harder than
        // low ones. That asymmetry is the only reason the emphasis pair is
        // worth having: on a linear signal the pair simply cancels.
        const float emphasised = lane.preEmphasis.process(sample);
        const float magnetised = lane.core.process(emphasised);
        const float restored = lane.deEmphasis.process(magnetised);
        float decimated = 0.0f;
        if (lane.down4x.Push(restored, decimated)) at4xFiltered = decimated;
      }
      float decimated = 0.0f;
      if (lane.down2x.Push(at4xFiltered, decimated)) at2xFiltered[i] = decimated;
    }
  }
  float wet = 0.0f;
  for (const float sample : at2xFiltered) (void)lane.down1x.Push(sample, wet);

  wet = lane.biasLoss.process(wet);
  wet = lane.bumpDip.process(lane.bumpPeak.process(wet));
  return lane.dc.Process(wet * makeup_);
}

float TapeProcessor::readDry(const std::array<float, kDryBufferSize>& buffer,
                             std::size_t write) const
{
  // The wet path costs 26.25 samples through six linear-phase halfbands. That
  // is fractional, so an integer buffer cannot match it, and an unmatched dry
  // path turns mix into a comb filter instead of a blend.
  const float position = static_cast<float>(write + kDryBufferSize) - kLatencyFrames;
  const auto base = static_cast<std::size_t>(position);
  const float fraction = position - static_cast<float>(base);

  const float y0 = buffer[(base - 1U) & kDryBufferMask];
  const float y1 = buffer[base & kDryBufferMask];
  const float y2 = buffer[(base + 1U) & kDryBufferMask];
  const float y3 = buffer[(base + 2U) & kDryBufferMask];

  const float d1 = fraction - 1.0f;
  const float d2 = fraction - 2.0f;
  const float dp1 = fraction + 1.0f;

  return y0 * (-fraction * d1 * d2 / 6.0f)
       + y1 * (dp1 * d1 * d2 / 2.0f)
       + y2 * (-dp1 * fraction * d2 / 2.0f)
       + y3 * (dp1 * fraction * d1 / 6.0f);
}

StereoSample TapeProcessor::process(StereoSample input)
{
  driveDb_ += smoothing_ * (driveDbTarget_ - driveDb_);
  mix_ += smoothing_ * (mixTarget_ - mix_);
  output_ += smoothing_ * (outputTarget_ - output_);
  driveGain_ = std::pow(10.0f, driveDb_ / 20.0f);
  makeup_ = driveMakeup(driveDb_);

  dryLeft_[dryWrite_] = input.left;
  dryRight_[dryWrite_] = input.right;
  dryWrite_ = (dryWrite_ + 1U) & kDryBufferMask;

  StereoSample wet{processLane(left_, input.left), processLane(right_, input.right)};
  wet = transport_.process(wet);

  const float dryLeft = readDry(dryLeft_, dryWrite_);
  const float dryRight = readDry(dryRight_, dryWrite_);

  return {
    output_ * (dryLeft + mix_ * (wet.left - dryLeft)),
    output_ * (dryRight + mix_ * (wet.right - dryRight)),
  };
}

} // namespace ardor
```

- [ ] **Step 5: Add the source to CMake and run the tests**

In `CMakeLists.txt`, add `src/tape/TapeProcessor.cpp` to the `ardor_tape` source list.

```bash
cmake --build build -j8 --target pedal-tape-smoke \
  && ctest --test-dir build --output-on-failure -R pedal-tape-smoke
```

Expected: PASS, with the three printed diagnostics — worst in-band alias, `mix=0` error, and drive level swing.

Two failures are likely here and both have a real fix, not a threshold fix:

- **`testMixZeroIsTransparent` fails.** `TapeTransport` adds `kNominalDelay` of its own on top of the 26.25. That extra delay is in the wet path only, so either the dry read must add it too, or `kNominalDelay` must be subtracted inside the transport's own read. Subtract it inside the transport — make the transport delay-neutral at zero flutter — and re-run `testFlutterOffAddsNoSidebands` to confirm it still passes.
- **`testOversamplingSuppressesAliases` misses -70 dBc.** Check the emphasis shelf first: a +12 dB boost at 12 kHz puts far more energy into the magnetics than the drive setting alone suggests. If the aliasing is genuinely from the solver, report the measured figure and raise it with the plan author rather than loosening the threshold.

- [ ] **Step 6: Commit**

```bash
git add src/tape/TapeProcessor.h src/tape/TapeProcessor.cpp tests/tape_smoke.cpp CMakeLists.txt
git commit -F - <<'EOF'
feat(tape): assemble the tape machine block

Drive, record pre-emphasis, eight-times oversampled hysteresis, de-emphasis,
head bump, transport, output trim and a latency-matched dry mix.

The drive makeup is measured rather than dialled in. configure() runs a 1 kHz
probe through a scratch copy of the magnetics at thirteen drive settings and
stores the reciprocal of each measured level, then the audio path interpolates
between them. That keeps the level steady as drive moves without a hand-tuned
table going stale the moment the hysteresis fit changes, and the calibration
runs at load time, never in the callback. A test sweeps drive across its full
36 dB range and requires the output level to move less than 3 dB.

The dry path is delayed by the same 26.25 samples the six halfbands cost, read
with third-order Lagrange because the figure is fractional and no integer
buffer can match it. Without that, Mix at intermediate settings is a comb
filter and not a blend. The mix=0 test measures the error against the delayed
input and requires it under -80 dB, which checks the compensation and the
transparency at once.

Speed changes the head bump and the IEC emphasis constant, not a treble filter:
at 15 ips the first gap-loss null lands near 190 kHz and the machine is flat to
20 kHz at both speeds. Bias is marked in the header as the one phenomenological
control, because a real bias oscillator sits above the internal Nyquist and
simulating it would produce aliasing rather than realism.
EOF
```

---

### Task 6: Wire the block into the engine

**Files:**
- Modify: `src/preset/ChainPlan.cpp:42-47`
- Modify: `src/audio/EngineLoader.cpp:390-405` and `:758-765`
- Modify: `src/dsp/RuntimeChain.cpp` (the `DistortionProcessor` alias from Task 1)
- Modify: `src/dsp/RuntimeChain.h` (a third `addDistortion` overload)
- Modify: `src/dsp/PedalEngine.cpp` (the `addDistortion` dispatch)
- Modify: `src/dsp/ClipDiagnostics.h`
- Modify: `CMakeLists.txt:233` (link `ardor_tape` into `ardor_dsp`)
- Test: `tests/runtime_chain_smoke.cpp`

**Interfaces:**
- Consumes: `ardor::TapeProcessor` (Task 5), the `DistortionProcessor` variant (Task 1).
- Produces: a chain block with `type: "distortion"`, `params.mode: "tape"` that loads, processes and accepts live parameter changes through the existing `PedalEngine::setDistortionParameter`. No new engine entry points — the distortion family already has them.

- [ ] **Step 1: Write the failing test**

`tests/runtime_chain_smoke.cpp` is a single `main()` full of `require(...)` calls in scoped blocks. Append this block before the bypass section near the end:

```cpp
  // A tape block must load through the distortion family, process, and take a
  // live parameter change — the same contract the rat and cheese blocks meet.
  {
    ardor::RuntimeChain chain;
    ardor::TapeProcessor tape;
    std::string error;
    nlohmann::json params;
    params["mode"] = "tape";
    params["drive"] = 6.0f;
    require(tape.configure(params, 48000.0f, error), "tape must configure: " + error);
    chain.addDistortion("tape-1", std::move(tape));

    require(chain.setDistortionParameter("tape-1", "drive", 12.0f),
            "a live drive change must reach the tape block");
    require(!chain.setDistortionParameter("tape-1", "speed", 30.0f),
            "speed is a load-time choice, not a live control");
    require(!chain.setDistortionParameter("missing", "drive", 0.0f),
            "an unknown block id must be rejected");

    for (std::size_t n = 0; n < 4096; ++n) {
      const float t = static_cast<float>(n) / 48000.0f;
      const float in = 0.4f * std::sin(6.28318530718f * 220.0f * t);
      const auto out = chain.process({in, in});
      require(std::isfinite(out.left) && std::isfinite(out.right),
              "the tape block must stay finite in the chain");
    }
    chain.reset();
  }
```

Add `#include "tape/TapeProcessor.h"` to the top of the file.

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build -j8 --target pedal-runtime-chain-smoke
```

Expected: FAIL — no `addDistortion` overload taking a `TapeProcessor`.

- [ ] **Step 3: Accept the mode in the chain plan**

In `src/preset/ChainPlan.cpp:46`, change:

```cpp
  return mode == "rat" || mode == "big_cheese";
```

to:

```cpp
  return mode == "rat" || mode == "big_cheese" || mode == "tape";
```

- [ ] **Step 4: Add the variant alternative and the chain overload**

In `src/dsp/RuntimeChain.cpp`, add `#include "tape/TapeProcessor.h"` and extend the alias from Task 1:

```cpp
using DistortionProcessor = std::variant<RatProcessor, CheeseProcessor, TapeProcessor>;
```

Add the overload next to the other two:

```cpp
void RuntimeChain::addDistortion(std::string id, TapeProcessor processor)
{
  Block block;
  block.kind = Block::Kind::Distortion;
  block.id = std::move(id);
  block.distortion = std::make_unique<DistortionProcessor>(std::move(processor));
  blocks_.push_back(std::move(block));
}
```

Because Task 1 replaced every use site with `std::visit`, nothing else in this file changes.

In `src/dsp/RuntimeChain.h`, add the declaration after line 63 and the include:

```cpp
  void addDistortion(std::string id, TapeProcessor processor);
```

```cpp
#include "tape/TapeProcessor.h"
```

- [ ] **Step 5: Build it in both load paths**

In `src/audio/EngineLoader.cpp`, in `prepareLaneChain` at line 391, insert before the `big_cheese` branch:

```cpp
      const auto distortionMode = block.params.value("mode", std::string{"rat"});
      if (distortionMode == "tape") {
        TapeProcessor processor;
        if (!processor.configure(block.params, static_cast<float>(options.sampleRate), error)) {
          return false;
        }
        chain.addDistortion(block.id, std::move(processor));
        continue;
      }
      if (distortionMode == "big_cheese") {
```

and change the following `if (block.params.value("mode", std::string{"rat"}) == "big_cheese") {` line to close that rewrite — the branch body itself is unchanged.

Add the include at the top of the file:

```cpp
#include "tape/TapeProcessor.h"
```

In `src/dsp/PedalEngine.cpp`, find `PedalEngine::addDistortion` and add the `tape` branch alongside the existing `big_cheese` one, following the shape already there. Add the same include.

- [ ] **Step 6: Name the stage for diagnostics**

No edit needed. `SignalStageKind::Distortion` already exists at `src/dsp/ClipDiagnostics.h:23` and `signalStageKindName` already returns `"distortion"` for it at `src/dsp/PedalEngine.cpp:82`. The whole distortion family shares one stage kind, so the tape block is covered. Confirm and move on:

```bash
grep -n "Distortion" src/dsp/ClipDiagnostics.h src/dsp/PedalEngine.cpp
```

Expected: `ClipDiagnostics.h:23:  Distortion,` and `PedalEngine.cpp:82:  case SignalStageKind::Distortion: return "distortion";`

- [ ] **Step 7: Link the library**

In `CMakeLists.txt:233`, add `ardor_tape` to the `ardor_dsp` link line, keeping the list alphabetical:

```cmake
target_link_libraries(ardor_dsp PUBLIC ardor_cheese ardor_daisyfx ardor_dynamics ardor_equalizer ardor_rat ardor_tape ardor_wah)
```

- [ ] **Step 8: Run the tests**

```bash
cmake --build build -j8 && ctest --test-dir build --output-on-failure
```

Expected: every test passes, including `pedal-runtime-chain-smoke` and `pedal-tape-smoke`.

- [ ] **Step 9: Commit**

```bash
git add src/preset/ChainPlan.cpp src/audio/EngineLoader.cpp src/dsp/RuntimeChain.h \
        src/dsp/RuntimeChain.cpp src/dsp/PedalEngine.cpp tests/runtime_chain_smoke.cpp CMakeLists.txt
git commit -F - <<'EOF'
feat(tape): run the tape block in the chain

The block joins the distortion family beside the RAT and the Big Cheese, so it
inherits the engine entry points, the preset validation and the live parameter
path that family already has rather than adding a fourth block type.

Because the distortion storage became a variant first, adding a third processor
is one alternative and one overload; every use site was already a std::visit.

A chain test loads the block, drives 4096 samples through it, asserts the
output stays finite, and asserts a live drive change lands while speed is
refused — speed is a load-time choice that reaches the engine by rebuilding the
chain, not a smoothed control.
EOF
```

---

### Task 7: Surface the block in the UI and the catalog

**Files:**
- Modify: `apps/manager/src/effects/catalog.v1.json` (after the `distortion:big_cheese` definition, around line 772)
- Modify: `apps/manager/src/effects/catalog.test.ts`
- Modify: `apps/manager/src/presets/block-browser/BlockBrowser.test.tsx`
- Modify: `src/ui/UiModel.cpp` (block name near line 144, defaults near line 201, drive assets near line 442, choice allow-list near line 1206)
- Modify: `src/ui/ParameterControls.cpp`
- Modify: `src/ui/LvglChainLayout.cpp:57-59`
- Modify: `apps/pedal-poc/main.cpp` — no change needed; verify below
- Test: `tests/manager_effect_catalog_smoke.cpp`, `tests/ui_model_smoke.cpp`

**Interfaces:**
- Consumes: the block from Task 6.
- Produces: catalog id `distortion:tape`, `blockType` `distortion`, `mode` `tape`, category `drive`, nine controls whose keys match `TapeProcessor::setParameterTarget` exactly: `drive`, `saturation`, `bias`, `speed`, `head_bump`, `flutter`, `hiss_db`, `mix`, `output_db`.

- [ ] **Step 1: Add the catalog definition**

In `apps/manager/src/effects/catalog.v1.json`, insert after the `distortion:big_cheese` definition:

```json
    {
      "id": "distortion:tape",
      "blockType": "distortion",
      "mode": "tape",
      "name": "Tape Machine",
      "description": "Magnetic hysteresis and transport, voiced after a Studer A800.",
      "category": "drive",
      "aliases": [
        "tape",
        "saturation",
        "studer",
        "reel to reel"
      ],
      "controls": [
        {
          "kind": "number",
          "key": "drive",
          "label": "Drive",
          "minimum": -12,
          "maximum": 24,
          "step": 0.5,
          "unit": "db",
          "defaultValue": 0
        },
        {
          "kind": "number",
          "key": "saturation",
          "label": "Saturation",
          "minimum": 0,
          "maximum": 1,
          "step": 0.01,
          "unit": "ratio",
          "defaultValue": 0.5
        },
        {
          "kind": "number",
          "key": "bias",
          "label": "Bias",
          "minimum": 0,
          "maximum": 1,
          "step": 0.01,
          "unit": "ratio",
          "defaultValue": 0.5
        },
        {
          "kind": "choice",
          "key": "speed",
          "label": "Tape speed",
          "choices": [
            { "value": "15", "label": "15 ips" },
            { "value": "30", "label": "30 ips" }
          ],
          "defaultValue": "15"
        },
        {
          "kind": "number",
          "key": "head_bump",
          "label": "Head Bump",
          "minimum": 0,
          "maximum": 1,
          "step": 0.01,
          "unit": "ratio",
          "defaultValue": 0.5
        },
        {
          "kind": "number",
          "key": "flutter",
          "label": "Flutter",
          "minimum": 0,
          "maximum": 1,
          "step": 0.01,
          "unit": "ratio",
          "defaultValue": 0
        },
        {
          "kind": "number",
          "key": "hiss_db",
          "label": "Hiss",
          "minimum": -120,
          "maximum": -60,
          "step": 1,
          "unit": "db",
          "defaultValue": -120
        },
        {
          "kind": "number",
          "key": "mix",
          "label": "Mix",
          "minimum": 0,
          "maximum": 1,
          "step": 0.01,
          "unit": "ratio",
          "defaultValue": 1
        },
        {
          "kind": "number",
          "key": "output_db",
          "label": "Output",
          "minimum": -24,
          "maximum": 24,
          "step": 0.5,
          "unit": "db",
          "defaultValue": 0
        }
      ]
    },
```

- [ ] **Step 2: Run the manager tests to see what fails**

```bash
cd apps/manager && npm test -- catalog && npx tsc --noEmit && cd ../..
```

Expected: `catalog.test.ts` and `BlockBrowser.test.tsx` fail on a block count that is now one higher. Read the failures and update those counts and any exhaustive id list. Do not change the assertions' intent — only the numbers and lists that legitimately grew.

- [ ] **Step 3: Add the pedal UI defaults and naming**

In `src/ui/UiModel.cpp`:

Block name, next to the `big_cheese` case near line 147:

```cpp
  if (block.type == "distortion" && block.params.value("mode", "") == "tape") {
    return "Tape Machine";
  }
```

Defaults, next to `defaultCheeseParams()`:

```cpp
nlohmann::json defaultTapeParams()
{
  return {
    {"mode", "tape"},
    {"drive", 0.0f},
    {"saturation", 0.5f},
    {"bias", 0.5f},
    {"speed", "15"},
    {"head_bump", 0.5f},
    {"flutter", 0.0f},
    {"hiss_db", -120.0f},
    {"mix", 1.0f},
    {"output_db", 0.0f},
  };
}
```

Wire it into both dispatch chains — the one near line 274 and the one near line 342 — following the `big_cheese` lines already there:

```cpp
  } else if (type == "distortion" && params.value("mode", "") == "tape") {
    defaults = defaultTapeParams();
```

```cpp
    } else if (asset.blockType == "distortion" && asset.mode == "tape") {
      params = defaultTapeParams();
```

Browser entry, in `appendDriveAssets` near line 447:

```cpp
  state.assets.push_back({"Tape Machine", "", "drive", "distortion", "tape",
                          "Drive · Studer A800 tape machine"});
```

Choice allow-list, in `setSelectedBlockParamValue` near line 1206. Add alongside `compressorValue`:

```cpp
  const bool tapeSpeedValue = block.type == "distortion"
    && block.params.value("mode", "") == "tape"
    && key == "speed" && value.is_string()
    && (value == "15" || value == "30");
```

and add `&& !tapeSpeedValue` to the guard's condition list.

- [ ] **Step 4: Add the parameter controls**

In `src/ui/ParameterControls.cpp`, next to the existing distortion cases:

```cpp
  if (block.type == "distortion" && block.params.value("mode", "") == "tape") {
    const auto number = [&](const char* key, float fallback) { return block.params.value(key, fallback); };
    const auto speed = block.params.value("speed", std::string{"15"});
    return {
      control("drive", "Drive", -12.0f, 24.0f, 0.5f, number("drive", 0.0f), formatDb),
      control("saturation", "Saturation", 0.0f, 1.0f, 0.01f, number("saturation", 0.5f), formatPercent),
      control("bias", "Bias", 0.0f, 1.0f, 0.01f, number("bias", 0.5f), formatPercent),
      choiceControl("speed", "Tape speed", {"15 ips", "30 ips"}, speed == "30" ? 1 : 0,
                    ParameterControlKind::Choice),
      control("head_bump", "Head Bump", 0.0f, 1.0f, 0.01f, number("head_bump", 0.5f), formatPercent),
      control("flutter", "Flutter", 0.0f, 1.0f, 0.01f, number("flutter", 0.0f), formatPercent),
      control("hiss_db", "Hiss", -120.0f, -60.0f, 1.0f, number("hiss_db", -120.0f), formatDb),
      control("mix", "Mix", 0.0f, 1.0f, 0.01f, number("mix", 1.0f), formatPercent),
      control("output_db", "Output", -24.0f, 24.0f, 0.5f, number("output_db", 0.0f), formatDb),
    };
  }
```

- [ ] **Step 5: Map the choice index back to its stored string**

`applyParameterDelta` converts a choice control's selected index into the value written to the preset, and at `src/ui/ParameterControls.cpp:349-355` it does so with a hard-coded two-way branch: `inputMode` gets an array, and **everything else falls through to `selected == 0 ? "peak" : "rms"`**. A `speed` control added without touching this would silently write `"peak"` into the tape block.

Replace that block:

```cpp
    const std::string before = selectedBlock->params.value(control.key, std::string{});
    if (control.key == "inputMode") {
      constexpr const char* kInputModes[] = {"sum", "left", "right"};
      setSelectedBlockParamValue(state, control.key, kInputModes[std::min<std::size_t>(selected, 2)]);
    } else if (control.key == "speed") {
      setSelectedBlockParamValue(state, control.key, selected == 0 ? "15" : "30");
    } else {
      setSelectedBlockParamValue(state, control.key, selected == 0 ? "peak" : "rms");
    }
```

- [ ] **Step 6: Add the chain badge**

In `src/ui/LvglChainLayout.cpp:57-59`, replace the distortion branch:

```cpp
  if (block.type == "distortion") {
    const auto mode = block.params.value("mode", std::string{});
    if (mode == "big_cheese") return "FUZZ";
    if (mode == "tape") return "TAPE";
    return "RAT";
  }
```

- [ ] **Step 7: Confirm the preset parameter path needs no change**

```bash
grep -n -A2 'block->type == "distortion"' apps/pedal-poc/main.cpp
```

Expected: it forwards every key to `engine.setDistortionParameter` with no mode switch, so the tape block is already covered. If it does switch on mode, add the `tape` case there.

- [ ] **Step 8: Run everything**

```bash
cmake --build build -j8 && ctest --test-dir build --output-on-failure
cd apps/manager && npm test && npx tsc --noEmit && cd ../..
```

Expected: all C++ tests pass, all manager tests pass, `tsc` is clean. `tests/manager_effect_catalog_smoke.cpp` and `tests/ui_model_smoke.cpp` will need their counts and lists updated the same way the manager tests did.

- [ ] **Step 9: Commit**

```bash
git add apps/manager/src src/ui tests/manager_effect_catalog_smoke.cpp tests/ui_model_smoke.cpp
git commit -F - <<'EOF'
feat(ui): list the tape machine as a Drive block

Nine controls in the catalog and in the pedal's parameter panel, with keys that
match TapeProcessor::setParameterTarget exactly, and a TAPE badge in the chain
strip.

Tape speed is a choice rather than a number because it is a load-time property:
it changes filter and solver coefficients, so it reaches the engine by
rebuilding the chain the way the compressor's detector does. That means adding
it to the explicit allow-list in setSelectedBlockParamValue, which is the only
route a choice edit has to requeue a preview.

Flutter and hiss ship at their inert defaults. A player who adds tape
saturation gets saturation, and reaches for the pitch movement and the noise
floor deliberately.
EOF
```

---

### Task 8: Tune the voicing and measure it on the Pi

The tests so far prove the model is correct and stable. They do not prove it sounds like a Studer, and they do not prove it fits the CPU budget. Both are measured here.

**Files:**
- Modify: `src/tape/TapeHysteresis.h` (the `Parameters` defaults, if tuning moves them)
- Create: `benchmark-results/tape-pi4-2026-08-16.txt`
- Modify: `docs/superpowers/specs/2026-08-16-tape-saturation-design.md` (record the measured figures)

**Interfaces:**
- Consumes: everything.
- Produces: measured CPU and voicing figures, and updated `TapeHysteresis::Parameters` defaults if the tuning changed them.

- [ ] **Step 1: Render audible material through the block**

Use `dryguitar.wav` at the repository root:

```bash
./build/ardor-pedal --help 2>&1 | head -40
```

Read the offline-render flags this build exposes, then render `dryguitar.wav` through a preset holding one tape block, at drive 0, 12 and 24 dB. Listen to all three against the dry file.

What to listen for, in order of how often each goes wrong:

1. **Level jumps between the three renders.** The calibration should hold them within 3 dB. If they jump, `kFieldPerSample` is placing the signal outside the calibrated knee.
2. **A brittle or fizzy top end.** That is the emphasis pair, not the magnetics. Reduce `kEmphasisDb`.
3. **Low end that thickens into mud at high drive.** That is the head bump interacting with saturation. Check the bump gains.
4. **No audible difference between drive 0 and drive 24.** `kFieldPerSample` is too small and the signal never reaches the knee.

- [ ] **Step 2: Adjust and re-run the test suite after every change**

```bash
cmake --build build -j8 && ctest --test-dir build --output-on-failure -R pedal-tape-smoke
```

Every tuning change must leave all fifteen assertions green. A change that needs a threshold moved is a change that needs re-thinking — say so rather than moving the threshold.

- [ ] **Step 3: Benchmark on the Pi**

Copy the build to the pedal and measure. See `docs/hardware-assembly.md:319` for the standing conditions — 48 kHz, block size 64, ten minutes, zero ALSA xruns.

```bash
# On the Pi, with a preset holding one tape block:
./audio-probe-pi --minutes 10 --block-size 64 | tee benchmark-results/tape-pi4-2026-08-16.txt
```

Record whole-core utilisation with the block bypassed and engaged; the difference is the block's cost.

- [ ] **Step 4: Decide against the budget**

The budget is roughly 10% of one core.

- **Inside budget:** record the figure and continue.
- **Over budget:** drop to 4x oversampling by removing the `up8x`/`down4x` stage pair from `TapeProcessor::processLane`, set `kOversampling` to 4 and `kLatencyFrames` to 22.5, then re-run the aliasing test and **report the new measured dBc figure**. Do not silently loosen the -70 dBc threshold; report what 4x actually achieves and let the spec be amended.

- [ ] **Step 5: Write the measurements into the spec**

Add a `## Measured` section to `docs/superpowers/specs/2026-08-16-tape-saturation-design.md` holding: the Pi 4 whole-core cost, the worst in-band alias in dBc, the `mix = 0` error in dB, the drive level swing in dB, and any `TapeHysteresis::Parameters` values that moved during tuning, with what each one changed.

- [ ] **Step 6: Commit**

```bash
git add src/tape docs/superpowers/specs/2026-08-16-tape-saturation-design.md benchmark-results/
git commit -F - <<'EOF'
perf(tape): tune the voicing and record what it costs

Measured on a Pi 4B at 48 kHz with block size 64, and listened to against
dryguitar.wav at three drive settings.

The spec now carries a Measured section holding the whole-core cost, the worst
in-band alias, the mix=0 error and the drive level swing, so the next person to
touch the hysteresis fit can tell whether they made it better or worse rather
than guessing.
EOF
```

---

## Self-Review

**Spec coverage.** Every section of the spec maps to a task: module layout → Tasks 3, 4, 5; the hysteresis model, solver and all three numerical hazards → Task 3; speed, emphasis and head bump → Task 5, tested in Task 5 step 1; bias as phenomenological → Task 5; transport and its correlation rules → Task 4; latency and mix → Task 5; the nine controls → Tasks 5 and 7; all fifteen tests → Tasks 3, 4, 5 and 8; the integration surface → Tasks 6 and 7.

**Two additions the spec did not name**, both found while reading the code and both prerequisites rather than scope creep:

- **Task 1** (the distortion variant). The spec assumed a third distortion processor would drop in. It would not: `RuntimeChain::Block` holds two nullable pointers read through a ternary that dereferences the second without checking it, at six sites.
- **Task 2** (`makeHighShelf`). The spec's emphasis pair needs a shelf; `ParametricEqMath` has none.

**One test added beyond the spec's fifteen:** `testRejectsBadConfiguration` in Task 5, covering the input validation at the block's trust boundary — a zero sample rate, a wrong mode, an unknown parameter key, and `speed` being refused as a live control.

**Type consistency.** `TapeHysteresis::configure` takes the *oversampled* rate in every call site (Tasks 3, 5). `TapeTransport::configure` takes the *host* rate. `kHissOffDb` is referenced by that name in Tasks 4, 5 and 7. Control keys are identical across `setParameterTarget` (Task 5), the catalog JSON (Task 7) and `ParameterControls.cpp` (Task 7): `drive`, `saturation`, `bias`, `speed`, `head_bump`, `flutter`, `hiss_db`, `mix`, `output_db`.

**Three things were verified against the source after the first draft, and each changed the plan:**

- `choiceControl` takes a `ParameterControlKind`, not a list of stored values (`src/ui/ParameterControls.cpp:66`). Corrected in Task 7 step 4.
- `applyParameterDelta` maps a choice index back to its stored string through a hard-coded branch whose `else` returns `"peak"` or `"rms"` (`src/ui/ParameterControls.cpp:349-355`). A `speed` control added without touching it would silently write `"peak"` into the tape block. That is now Task 7 step 5, and it is the kind of bug that ships silently because nothing crashes.
- `SignalStageKind::Distortion` already exists and is already named, so Task 6 step 6 is a confirmation rather than an edit.
