# Five-Band Parametric EQ Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a stereo five-band parametric EQ block with canonical preset data, realtime-smoothed live edits, and the approved full-width LVGL response-curve editor.

**Architecture:** A new `ardor_equalizer` library owns typed EQ parameters, shared RBJ coefficient/response math, and the allocation-free processor. Existing preset and runtime layers add a narrow `eq` block path addressed by stable block ID; the LVGL editor uses a UI-only geometry model and sends typed band updates to the running engine while also updating preset JSON.

**Tech Stack:** C++20, nlohmann/json, LVGL 9.5, CMake/CTest, existing Ardor smoke-test executables.

---

## File structure

### New files

- `src/equalizer/EqParameters.h` — typed five-band values, constants, defaults, tolerant JSON parsing, and canonical serialization.
- `src/equalizer/EqParameters.cpp` — parameter validation and JSON implementation.
- `src/equalizer/ParametricEqMath.h` — normalized biquad coefficients and response-evaluation API.
- `src/equalizer/ParametricEqMath.cpp` — RBJ peaking-EQ coefficient and magnitude implementation.
- `src/equalizer/ParametricEqProcessor.h` — atomic targets, smoothed working state, stereo filter state, and realtime API.
- `src/equalizer/ParametricEqProcessor.cpp` — block-rate coefficient updates and transposed direct-form II processing.
- `src/ui/EqEditorModel.h` — EQ graph coordinate conversion, hit testing, field adjustment, and fixed-size curve data.
- `src/ui/EqEditorModel.cpp` — UI-only calculations with no LVGL dependency.
- `tests/parametric_eq_smoke.cpp` — parameter, math, processor, smoothing, and stereo tests.
- `tests/eq_editor_model_smoke.cpp` — graph mapping, curve, hit-test, and edit tests.

### Modified files

- `CMakeLists.txt` — add `ardor_equalizer`, two focused tests, links, and CTest entries.
- `src/dsp/RuntimeChain.h`, `src/dsp/RuntimeChain.cpp` — own, order, process, reset, and target EQ blocks.
- `src/dsp/PedalEngine.h`, `src/dsp/PedalEngine.cpp` — expose EQ construction and stable-ID live updates.
- `src/preset/ChainPlan.cpp` — recognize and canonicalize `eq` / `parametric_eq_5` blocks.
- `src/audio/EngineLoader.cpp` — construct EQ blocks while preparing a chain.
- `src/ui/UiModel.h`, `src/ui/UiModel.cpp` — expose the EQ asset/defaults and nested-band mutations.
- `src/ui/LvglUi.h`, `src/ui/LvglUi.cpp` — render and interact with the purpose-built editor and expose the typed live-update action.
- `tests/preset_smoke.cpp` — ready/unsupported EQ planning cases.
- `tests/runtime_chain_smoke.cpp` — serial processing and stable-ID targeting.
- `tests/offline_smoke.cpp` — EngineLoader EQ construction and invalid-mode behavior.
- `tests/ui_model_smoke.cpp` — insertion, canonical load/save, field edits, enable, and reset.
- `tests/lvgl_ui_smoke.cpp` — approved layout, curve, nodes, pads, toggle, and touch behavior.
- `apps/pedal-poc/main.cpp` — active-engine wiring for immediate EQ preview.
- `tests/engine_contract_smoke.cpp` — live update success/failure and convergence.
- `tests/dsp_bench.cpp` — five-active-band benchmark case.
- `README.md` — document the new preset contract and ranges.

## Task 1: Typed EQ parameters and shared response math

**Files:**
- Create: `src/equalizer/EqParameters.h`
- Create: `src/equalizer/EqParameters.cpp`
- Create: `src/equalizer/ParametricEqMath.h`
- Create: `src/equalizer/ParametricEqMath.cpp`
- Create: `tests/parametric_eq_smoke.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Register the equalizer library and failing test target**

Add after `ardor_dynamics` in `CMakeLists.txt`:

```cmake
add_library(ardor_equalizer
  src/equalizer/EqParameters.cpp
  src/equalizer/ParametricEqMath.cpp
)
target_include_directories(ardor_equalizer PUBLIC
  src
  ${neuralampmodelercore_SOURCE_DIR}/Dependencies
)
target_compile_features(ardor_equalizer PUBLIC cxx_std_20)
```

Add near the other smoke executables and tests:

```cmake
add_executable(pedal-parametric-eq-smoke tests/parametric_eq_smoke.cpp)
target_link_libraries(pedal-parametric-eq-smoke PRIVATE ardor_equalizer)

add_test(NAME pedal-parametric-eq-smoke COMMAND pedal-parametric-eq-smoke)
```

- [ ] **Step 2: Write failing parameter and response tests**

Create `tests/parametric_eq_smoke.cpp` with this first test body:

```cpp
#include "equalizer/EqParameters.h"
#include "equalizer/ParametricEqMath.h"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char* message)
{
  if (!condition) throw std::runtime_error(message);
}
}

int main()
{
  const auto defaults = ardor::defaultParametricEqParams();
  require(defaults.bands.size() == 5, "five EQ bands");
  require(defaults.bands[0].frequencyHz == 80.0f, "band 1 default");
  require(defaults.bands[4].frequencyHz == 8000.0f, "band 5 default");

  const nlohmann::json supplied{
    {"mode", "parametric_eq_5"},
    {"bands", nlohmann::json::array({
      {{"enabled", false}, {"frequency_hz", 1.0f}, {"q", 99.0f}, {"gain_db", -99.0f}},
      {{"enabled", "bad"}, {"frequency_hz", std::numeric_limits<float>::infinity()}},
      nlohmann::json::object(), nlohmann::json::object(), nlohmann::json::object(),
      {{"frequency_hz", 16000.0f}}
    })}
  };
  const auto parsed = ardor::parametricEqParamsFromJson(supplied);
  require(!parsed.bands[0].enabled, "valid band enabled value");
  require(parsed.bands[0].frequencyHz == 20.0f, "frequency clamps low");
  require(parsed.bands[0].q == 18.0f, "Q clamps high");
  require(parsed.bands[0].gainDb == -18.0f, "gain clamps low");
  require(parsed.bands[1].enabled, "wrong enabled type uses default");
  require(parsed.bands[1].frequencyHz == 250.0f, "non-finite frequency uses default");

  const auto canonical = ardor::parametricEqParamsToJson(parsed);
  require(canonical.at("bands").size() == 5, "canonical output has five bands");
  require(canonical.at("mode") == "parametric_eq_5", "canonical mode");

  const auto coefficients = ardor::makePeakingEq(48000.0f, 1000.0f, 1.0f, 6.0f);
  require(std::fabs(ardor::biquadMagnitudeDb(coefficients, 1000.0f, 48000.0f) - 6.0f) < 0.01f,
          "center frequency has requested gain");
  const auto neutral = ardor::makePeakingEq(48000.0f, 1000.0f, 1.0f, 0.0f);
  require(std::fabs(ardor::biquadMagnitudeDb(neutral, 20.0f, 48000.0f)) < 0.0001f,
          "zero-gain biquad is neutral");
}
```

- [ ] **Step 3: Run the test to verify it fails**

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target pedal-parametric-eq-smoke -j2
```

Expected: compilation fails because `EqParameters.h` and
`ParametricEqMath.h` do not exist.

- [ ] **Step 4: Implement the typed parameter API**

Create `src/equalizer/EqParameters.h`:

```cpp
#pragma once

#include <array>
#include <cstddef>
#include <nlohmann/json.hpp>

namespace ardor {

inline constexpr std::size_t kParametricEqBandCount = 5;
inline constexpr float kEqMinimumFrequencyHz = 20.0f;
inline constexpr float kEqMaximumFrequencyHz = 20000.0f;
inline constexpr float kEqMinimumQ = 0.1f;
inline constexpr float kEqMaximumQ = 18.0f;
inline constexpr float kEqMinimumGainDb = -18.0f;
inline constexpr float kEqMaximumGainDb = 18.0f;

struct EqBandParams {
  bool enabled = true;
  float frequencyHz = 1000.0f;
  float q = 1.0f;
  float gainDb = 0.0f;
};

struct ParametricEqParams {
  std::array<EqBandParams, kParametricEqBandCount> bands{};
};

EqBandParams defaultParametricEqBand(std::size_t index);
ParametricEqParams defaultParametricEqParams();
ParametricEqParams parametricEqParamsFromJson(const nlohmann::json& params);
nlohmann::json parametricEqParamsToJson(const ParametricEqParams& params);
bool isParametricEqMode(const nlohmann::json& params);

} // namespace ardor
```

Create `src/equalizer/EqParameters.cpp` with one finite-number helper, the
default frequency table `{80, 250, 800, 2500, 8000}`, and this normalization
loop:

```cpp
ParametricEqParams parametricEqParamsFromJson(const nlohmann::json& params)
{
  auto result = defaultParametricEqParams();
  const auto bands = params.find("bands");
  if (bands == params.end() || !bands->is_array()) return result;

  const auto count = std::min<std::size_t>(bands->size(), kParametricEqBandCount);
  for (std::size_t i = 0; i < count; ++i) {
    const auto& supplied = (*bands)[i];
    if (!supplied.is_object()) continue;
    auto& band = result.bands[i];
    const auto enabled = supplied.find("enabled");
    if (enabled != supplied.end() && enabled->is_boolean()) band.enabled = enabled->get<bool>();
    band.frequencyHz = finiteClamped(supplied, "frequency_hz", band.frequencyHz,
                                     kEqMinimumFrequencyHz, kEqMaximumFrequencyHz);
    band.q = finiteClamped(supplied, "q", band.q, kEqMinimumQ, kEqMaximumQ);
    band.gainDb = finiteClamped(supplied, "gain_db", band.gainDb,
                                kEqMinimumGainDb, kEqMaximumGainDb);
  }
  return result;
}
```

Serialize `mode` and exactly five objects with the keys shown in the design
specification. `isParametricEqMode()` returns true only when `mode` is the
string `parametric_eq_5`.

- [ ] **Step 5: Implement shared RBJ coefficient and response math**

Create `src/equalizer/ParametricEqMath.h`:

```cpp
#pragma once

namespace ardor {

struct BiquadCoefficients {
  float b0 = 1.0f;
  float b1 = 0.0f;
  float b2 = 0.0f;
  float a1 = 0.0f;
  float a2 = 0.0f;
};

BiquadCoefficients makePeakingEq(float sampleRate, float frequencyHz, float q, float gainDb);
float biquadMagnitudeDb(const BiquadCoefficients& coefficients, float frequencyHz, float sampleRate);

} // namespace ardor
```

Create `src/equalizer/ParametricEqMath.cpp` using the normalized RBJ peaking
equations:

```cpp
const double A = std::pow(10.0, gainDb / 40.0);
const double omega = 2.0 * std::numbers::pi * frequencyHz / sampleRate;
const double alpha = std::sin(omega) / (2.0 * q);
const double cosine = std::cos(omega);
const double a0 = 1.0 + alpha / A;
return {
  static_cast<float>((1.0 + alpha * A) / a0),
  static_cast<float>((-2.0 * cosine) / a0),
  static_cast<float>((1.0 - alpha * A) / a0),
  static_cast<float>((-2.0 * cosine) / a0),
  static_cast<float>((1.0 - alpha / A) / a0),
};
```

For `biquadMagnitudeDb`, evaluate numerator and denominator at
`z^-1 = exp(-j*omega)`, clamp the magnitude to at least `1.0e-12`, and return
`20 * log10(magnitude)`.

- [ ] **Step 6: Run the focused test**

Run:

```bash
cmake --build build --target pedal-parametric-eq-smoke -j2
./build/pedal-parametric-eq-smoke
```

Expected: exit code 0.

- [ ] **Step 7: Commit the parameter and math foundation**

```bash
git add CMakeLists.txt src/equalizer/EqParameters.h src/equalizer/EqParameters.cpp \
  src/equalizer/ParametricEqMath.h src/equalizer/ParametricEqMath.cpp \
  tests/parametric_eq_smoke.cpp
git commit -m "feat: add parametric eq parameter and response model"
```

## Task 2: Realtime-safe stereo EQ processor

**Files:**
- Create: `src/equalizer/ParametricEqProcessor.h`
- Create: `src/equalizer/ParametricEqProcessor.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/parametric_eq_smoke.cpp`

- [ ] **Step 1: Add failing processor behavior tests**

Extend `tests/parametric_eq_smoke.cpp` after the math assertions:

```cpp
ardor::ParametricEqProcessor processor;
std::string error;
auto settings = ardor::defaultParametricEqParams();
settings.bands[2] = {true, 1000.0f, 1.0f, 6.0f};
require(processor.configure(settings, 48000.0f, error), "processor configures");

std::array<float, 64> left{};
std::array<float, 64> right{};
left[0] = 1.0f;
right[0] = 0.5f;
processor.processBlock(left.data(), right.data(), left.data(), right.data(), left.size());
for (std::size_t i = 0; i < left.size(); ++i) {
  require(std::isfinite(left[i]) && std::isfinite(right[i]), "finite stereo output");
  require(std::fabs(left[i] - right[i] * 2.0f) < 0.0001f, "stereo state remains independent");
}

require(processor.setBandTarget(2, {false, 1000.0f, 1.0f, 6.0f}), "valid target accepted");
require(!processor.setBandTarget(5, settings.bands[0]), "invalid band rejected");
processor.reset();
float firstLeft = 1.0f;
float firstRight = 1.0f;
processor.process(firstLeft, firstRight);
processor.reset();
float repeatedLeft = 1.0f;
float repeatedRight = 1.0f;
processor.process(repeatedLeft, repeatedRight);
require(firstLeft == repeatedLeft && firstRight == repeatedRight, "reset deterministic");
```

Also add headers `<array>`, `<string>`, and
`equalizer/ParametricEqProcessor.h`.

- [ ] **Step 2: Run the focused test to verify it fails**

Run:

```bash
cmake --build build --target pedal-parametric-eq-smoke -j2
```

Expected: compilation fails because `ParametricEqProcessor` is not declared.

- [ ] **Step 3: Implement the processor interface and fixed state**

First add the implementation file to the existing target:

```cmake
target_sources(ardor_equalizer PRIVATE
  src/equalizer/ParametricEqProcessor.cpp
)
```

Create `src/equalizer/ParametricEqProcessor.h`:

```cpp
#pragma once

#include "equalizer/EqParameters.h"
#include "equalizer/ParametricEqMath.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <string>

namespace ardor {

class ParametricEqProcessor {
public:
  bool configure(const ParametricEqParams& params, float sampleRate, std::string& error);
  bool setBandTarget(std::size_t index, const EqBandParams& params);
  void process(float& left, float& right);
  void processBlock(const float* inputLeft, const float* inputRight,
                    float* outputLeft, float* outputRight, std::size_t frames);
  void reset();

private:
  struct AtomicBand {
    std::atomic<bool> enabled{true};
    std::atomic<float> frequencyHz{1000.0f};
    std::atomic<float> q{1.0f};
    std::atomic<float> gainDb{0.0f};
  };
  struct FilterState {
    float z1 = 0.0f;
    float z2 = 0.0f;
    float process(float input, const BiquadCoefficients& c);
  };

  void updateCoefficients(std::size_t frames);
  void processPrepared(float& left, float& right);

  std::array<AtomicBand, kParametricEqBandCount> targets_{};
  std::array<EqBandParams, kParametricEqBandCount> current_{};
  std::array<BiquadCoefficients, kParametricEqBandCount> coefficients_{};
  std::array<std::array<FilterState, 2>, kParametricEqBandCount> states_{};
  float sampleRate_ = 48000.0f;
  std::size_t scalarSamplesUntilUpdate_ = 0;
  bool configured_ = false;
};

} // namespace ardor
```

- [ ] **Step 4: Implement configuration, smoothing, and DF2T processing**

In `src/equalizer/ParametricEqProcessor.cpp`:

```cpp
float ParametricEqProcessor::FilterState::process(float input, const BiquadCoefficients& c)
{
  const float output = c.b0 * input + z1;
  z1 = c.b1 * input - c.a1 * output + z2;
  z2 = c.b2 * input - c.a2 * output;
  return output;
}
```

`configure()` rejects a non-finite or non-positive sample rate, stores all
targets with `memory_order_relaxed`, copies them to `current_`, calculates the
initial coefficients, resets state, and sets `configured_`.

In `updateCoefficients(frames)`, use:

```cpp
const float elapsed = static_cast<float>(frames) / sampleRate_;
const float alpha = 1.0f - std::exp(-elapsed / 0.015f);
for (std::size_t i = 0; i < kParametricEqBandCount; ++i) {
  const float targetFrequency = targets_[i].frequencyHz.load(std::memory_order_relaxed);
  current_[i].frequencyHz = std::exp(std::log(current_[i].frequencyHz)
    + (std::log(targetFrequency) - std::log(current_[i].frequencyHz)) * alpha);
  current_[i].q += (targets_[i].q.load(std::memory_order_relaxed) - current_[i].q) * alpha;
  const float requestedGain = targets_[i].enabled.load(std::memory_order_relaxed)
    ? targets_[i].gainDb.load(std::memory_order_relaxed) : 0.0f;
  current_[i].gainDb += (requestedGain - current_[i].gainDb) * alpha;
  coefficients_[i] = makePeakingEq(sampleRate_, current_[i].frequencyHz,
                                    current_[i].q, current_[i].gainDb);
}
```

`processBlock()` calls `updateCoefficients(frames)` exactly once, supports
in-place buffers, and runs both channels through all five `FilterState`s.
For the existing scalar compatibility path, `process()` calls
`updateCoefficients(64)` only when `scalarSamplesUntilUpdate_` is zero, resets
the counter to 64, decrements it once per sample, and calls `processPrepared()`.
This preserves block-rate coefficient work even for scalar callers.
`setBandTarget()` rejects an invalid index or non-finite value, clamps finite
fields, and stores atomics relaxed.

- [ ] **Step 5: Add smoothing convergence and extreme-value tests**

Append a block loop that sends a final target `{true, 20000, 18, 18}`, renders
200 blocks after an impulse, and asserts every sample is finite. For smoothing,
configure only band 3 at 1 kHz/+6 dB, render a 1 kHz sine until settled, disable
the band, render another 200 blocks, and compare the final-block RMS with the
dry sine RMS. Require the disabled output to be within 1% of dry and require
the first block after disabling to remain between the boosted and dry RMS.
This verifies behavior without exposing processor internals for tests.

- [ ] **Step 6: Run the focused test**

```bash
cmake --build build --target pedal-parametric-eq-smoke -j2
./build/pedal-parametric-eq-smoke
```

Expected: exit code 0.

- [ ] **Step 7: Commit the processor**

```bash
git add CMakeLists.txt src/equalizer/ParametricEqProcessor.h \
  src/equalizer/ParametricEqProcessor.cpp tests/parametric_eq_smoke.cpp
git commit -m "feat: add realtime five-band eq processor"
```

## Task 3: Preset, chain, and engine integration

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `src/preset/ChainPlan.cpp`
- Modify: `src/dsp/RuntimeChain.h`
- Modify: `src/dsp/RuntimeChain.cpp`
- Modify: `src/dsp/PedalEngine.h`
- Modify: `src/dsp/PedalEngine.cpp`
- Modify: `src/audio/EngineLoader.cpp`
- Modify: `tests/preset_smoke.cpp`
- Modify: `tests/runtime_chain_smoke.cpp`
- Modify: `tests/offline_smoke.cpp`

- [ ] **Step 1: Write failing planning and engine-loading tests**

In `tests/preset_smoke.cpp`, add ready and unsupported blocks to
`chainPreset`:

```cpp
chainPreset.blocks.push_back({"eq-ready", "eq", true, "",
  nlohmann::json{{"mode", "parametric_eq_5"}}});
chainPreset.blocks.push_back({"eq-unknown", "eq", true, "",
  nlohmann::json{{"mode", "future_eq"}}});
```

Assert `eq-ready` is `Ready`, has five canonical bands, and `eq-unknown` is
`Unsupported`. Update expected block and runnable counts.

In `tests/offline_smoke.cpp`, create a ready EQ `ChainPlan`, apply it, and
assert the impulse output is finite and differs from dry when band 3 is +6 dB.
Also assert a plan with an unknown mode fails before replacing the live engine.

- [ ] **Step 2: Write failing runtime stable-ID tests**

In `tests/runtime_chain_smoke.cpp`, add two EQ blocks with IDs `eq-a` and
`eq-b`, set different center frequencies, then assert:

```cpp
require(chain.setParametricEqBand("eq-b", 1, {true, 1000.0f, 1.0f, 12.0f}),
        "target existing EQ by stable ID");
require(!chain.setParametricEqBand("missing", 1, {true, 1000.0f, 1.0f, 12.0f}),
        "missing EQ ID rejected");
require(!chain.setParametricEqBand("eq-b", 5, {true, 1000.0f, 1.0f, 12.0f}),
        "invalid EQ band rejected");
```

Render an impulse through `eq-a -> compressor -> eq-b` and assert finite
output and a different response when order is reversed.

- [ ] **Step 3: Run the three tests to verify they fail**

```bash
cmake --build build --target pedal-preset-smoke pedal-runtime-chain-smoke pedal-offline-smoke -j2
```

Expected: compilation or assertion failures because `eq` is unsupported and
the runtime API is missing.

- [ ] **Step 4: Link the new library into DSP and preset targets**

Change the existing links in `CMakeLists.txt` to:

```cmake
target_link_libraries(ardor_dsp PUBLIC ardor_daisyfx ardor_dynamics ardor_equalizer)
target_link_libraries(ardor_preset PUBLIC ardor_daisyfx ardor_equalizer)
```

- [ ] **Step 5: Recognize and canonicalize EQ chain plans**

In `src/preset/ChainPlan.cpp`, include `equalizer/EqParameters.h`, add:

```cpp
bool isSupportedEqBlock(const std::string& type, const nlohmann::json& params)
{
  return type == "eq" && isParametricEqMode(params);
}
```

Handle `type == "eq"` beside dynamics. Ready EQ plans increment
`runnableBlockCount` and replace `blockPlan.params` with:

```cpp
blockPlan.params = parametricEqParamsToJson(parametricEqParamsFromJson(blockPlan.params));
```

Unknown EQ modes remain `Unsupported`.

- [ ] **Step 6: Add the runtime EQ block kind and stable-ID setter**

In `RuntimeChain.h`, include the EQ headers and add:

```cpp
bool addParametricEq(std::string id, const ParametricEqParams& params,
                     float sampleRate, std::string& error);
bool setParametricEqBand(const std::string& id, std::size_t band, const EqBandParams& params);
```

Add `Equalizer` to `Block::Kind`, plus `std::string id` and
`std::unique_ptr<ParametricEqProcessor> equalizer`. Construct the processor in
place in `addParametricEq()`. In both scalar and block switches, preserve
stereo and call the processor; in `reset()`, reset it. The setter finds a block
whose kind is `Equalizer` and whose `id` matches, then forwards the target.

- [ ] **Step 7: Expose engine construction and updates**

Add to `PedalEngine.h`:

```cpp
bool addParametricEq(const std::string& id, const nlohmann::json& params,
                     float sampleRate, std::string& error);
bool setParametricEqBand(const std::string& id, std::size_t band, const EqBandParams& params);
```

Implement by parsing canonical parameters and forwarding to `RuntimeChain`:

```cpp
bool PedalEngine::addParametricEq(const std::string& id, const nlohmann::json& params,
                                  float sampleRate, std::string& error)
{
  return chain_.addParametricEq(id, parametricEqParamsFromJson(params), sampleRate, error);
}

bool PedalEngine::setParametricEqBand(const std::string& id, std::size_t band,
                                      const EqBandParams& params)
{
  return chain_.setParametricEqBand(id, band, params);
}
```

- [ ] **Step 8: Construct EQ blocks in EngineLoader**

Before the Daisy branch in `prepareChainPlan()`, add:

```cpp
if (block.type == "eq") {
  if (!engine.addParametricEq(block.id, block.params,
                              static_cast<float>(options.sampleRate), error)) {
    return false;
  }
  continue;
}
```

EQ does not set `stereoEstablished`; it accepts and preserves either mono-
duplicated or stereo input.

- [ ] **Step 9: Run focused integration tests**

```bash
cmake --build build --target pedal-preset-smoke pedal-runtime-chain-smoke pedal-offline-smoke -j2
./build/pedal-preset-smoke
./build/pedal-runtime-chain-smoke
./build/pedal-offline-smoke
```

Expected: all exit 0.

- [ ] **Step 10: Commit runtime integration**

```bash
git add CMakeLists.txt src/preset/ChainPlan.cpp src/dsp/RuntimeChain.h \
  src/dsp/RuntimeChain.cpp src/dsp/PedalEngine.h src/dsp/PedalEngine.cpp \
  src/audio/EngineLoader.cpp tests/preset_smoke.cpp tests/runtime_chain_smoke.cpp \
  tests/offline_smoke.cpp
git commit -m "feat: integrate parametric eq into runtime chains"
```

## Task 4: UI state and testable editor model

**Files:**
- Create: `src/ui/EqEditorModel.h`
- Create: `src/ui/EqEditorModel.cpp`
- Create: `tests/eq_editor_model_smoke.cpp`
- Modify: `CMakeLists.txt`
- Modify: `src/ui/UiModel.h`
- Modify: `src/ui/UiModel.cpp`
- Modify: `tests/ui_model_smoke.cpp`

- [ ] **Step 1: Add failing UI-model tests**

In `tests/ui_model_smoke.cpp`, find the `Five Band EQ` asset, append it, and
assert type, mode, five default bands, no asset path, and dirty state. Then:

```cpp
ardor::selectBlock(state, state.bank.presets[state.activePreset].blocks.size() - 1);
require(ardor::setSelectedEqBand(state, 1, {false, 1.0f, 99.0f, -99.0f}),
        "edit selected EQ band");
const auto editedEq = ardor::selectedParametricEqParams(state);
require(!editedEq.bands[1].enabled, "per-band disable persists");
require(editedEq.bands[1].frequencyHz == 20.0f, "UI clamps frequency");
require(editedEq.bands[1].q == 18.0f, "UI clamps Q");
require(editedEq.bands[1].gainDb == -18.0f, "UI clamps gain");
require(ardor::resetSelectedEqBand(state, 1), "reset selected band");
require(ardor::selectedParametricEqParams(state).bands[1].frequencyHz == 250.0f,
        "band reset uses indexed default");
```

Round-trip through `activePresetToPreset()` and `replaceActivePreset()` and
assert exactly five complete band objects remain.

- [ ] **Step 2: Add failing editor geometry and curve tests**

Create `tests/eq_editor_model_smoke.cpp`:

```cpp
#include "ui/EqEditorModel.h"

#include <cmath>
#include <stdexcept>

namespace { void require(bool c, const char* m) { if (!c) throw std::runtime_error(m); } }

int main()
{
  require(std::fabs(ardor::eqFrequencyFromX(ardor::eqXFromFrequency(1000.0f, 1100), 1100)
                    - 1000.0f) < 1.0f, "log frequency mapping round trip");
  require(std::fabs(ardor::eqGainFromY(ardor::eqYFromGain(6.0f, 132), 132) - 6.0f) < 0.1f,
          "linear gain mapping round trip");

  auto params = ardor::defaultParametricEqParams();
  params.bands[1].gainDb = 6.0f;
  const auto curves = ardor::makeEqCurveData(params, 48000.0f);
  require(curves.combinedDb.size() == 256, "fixed curve point count");
  require(curves.combinedDb[0] >= -18.0f && curves.combinedDb[0] <= 18.0f,
          "display curve is viewport-clamped");

  auto band = params.bands[1];
  ardor::adjustEqBandField(band, ardor::EqBandField::Gain, 2);
  require(band.gainDb == 7.0f, "gain encoder step is 0.5 dB");
  ardor::adjustEqBandField(band, ardor::EqBandField::Frequency, 24);
  require(std::fabs(band.frequencyHz - 500.0f) < 1.0f, "24 frequency ticks equal one octave");
}
```

- [ ] **Step 3: Register the editor model and failing test**

Add `src/ui/EqEditorModel.cpp` to `ardor_ui`. Add:

```cmake
add_executable(pedal-eq-editor-model-smoke tests/eq_editor_model_smoke.cpp)
target_link_libraries(pedal-eq-editor-model-smoke PRIVATE ardor_ui)
add_test(NAME pedal-eq-editor-model-smoke COMMAND pedal-eq-editor-model-smoke)
```

Run the two targets and expect missing-symbol failures.

- [ ] **Step 4: Add EQ assets, canonical defaults, and nested mutations**

In `UiModel.cpp`:

- map block type `eq` to label `EQ`;
- add `{"Five Band EQ", "", "eq", "eq", "parametric_eq_5"}` in both demo
  assets and `loadAssetsFromDataRoot()`;
- include `eq` in valid category filters;
- in `paramsWithKnownDefaults()`, canonicalize a supported EQ using
  `parametricEqParamsToJson(parametricEqParamsFromJson(params))`;
- in `insertAssetBlock()`, use `parametricEqParamsToJson(defaultParametricEqParams())`.

Add to `UiModel.h` and implement with canonical parse/mutate/serialize:

```cpp
ParametricEqParams selectedParametricEqParams(const UiState& state);
bool setSelectedEqBand(UiState& state, std::size_t band, EqBandParams params);
bool resetSelectedEqBand(UiState& state, std::size_t band);
```

`setSelectedEqBand()` returns false unless the selected block is the supported
EQ and the band index is valid. It clamps via a JSON round trip, writes the
complete canonical `params`, and sets `dirty` only when the canonical band
changes.

- [ ] **Step 5: Implement editor geometry, adjustment, and curve data**

Create `EqEditorModel.h` with:

```cpp
enum class EqBandField { Frequency, Q, Gain };
inline constexpr std::size_t kEqCurvePointCount = 256;

struct EqCurveData {
  std::array<float, kEqCurvePointCount> frequencyHz{};
  std::array<std::array<float, kEqCurvePointCount>, kParametricEqBandCount> bandDb{};
  std::array<float, kEqCurvePointCount> combinedDb{};
};

int eqXFromFrequency(float frequencyHz, int width);
float eqFrequencyFromX(int x, int width);
int eqYFromGain(float gainDb, int height);
float eqGainFromY(int y, int height);
void adjustEqBandField(EqBandParams& band, EqBandField field, int ticks);
EqCurveData makeEqCurveData(const ParametricEqParams& params, float sampleRate);
```

Use logarithmic frequency mapping, linear `+18 dB -> y=0` and `-18 dB ->
y=height-1`, and these encoder steps:

```cpp
case EqBandField::Frequency:
  band.frequencyHz *= std::pow(2.0f, static_cast<float>(ticks) / 24.0f);
  break;
case EqBandField::Q:
  band.q *= std::pow(2.0f, static_cast<float>(ticks) / 24.0f);
  break;
case EqBandField::Gain:
  band.gainDb += 0.5f * static_cast<float>(ticks);
  break;
```

Clamp afterward. For each curve point, evaluate every band's configured
response with shared math so disabled bands retain their dimmed trace. Sum only
enabled bands into the combined response. Clamp only stored display values to
`[-18, 18]`.

- [ ] **Step 6: Run focused UI-model tests**

```bash
cmake --build build --target pedal-ui-model-smoke pedal-eq-editor-model-smoke -j2
./build/pedal-ui-model-smoke
./build/pedal-eq-editor-model-smoke
```

Expected: both exit 0.

- [ ] **Step 7: Commit UI state and editor model**

```bash
git add CMakeLists.txt src/ui/UiModel.h src/ui/UiModel.cpp src/ui/EqEditorModel.h \
  src/ui/EqEditorModel.cpp tests/ui_model_smoke.cpp tests/eq_editor_model_smoke.cpp
git commit -m "feat: add parametric eq editor model"
```

## Task 5: Purpose-built LVGL response-curve editor

**Files:**
- Modify: `src/ui/LvglUi.h`
- Modify: `src/ui/LvglUi.cpp`
- Modify: `tests/lvgl_ui_smoke.cpp`

- [ ] **Step 1: Add failing render and interaction assertions**

In `tests/lvgl_ui_smoke.cpp`, insert/select the EQ and build edit mode. Assert:

- labels `Five Band EQ`, `FREQUENCY`, `Q`, `GAIN`, `BAND 1`, `Enabled`, and
  `Reset band` exist;
- one combined line and five per-band lines each contain 256 points;
- five numbered node labels exist;
- the generic `PAGE 1 / 1` label and seven-knob row do not appear for EQ;
- the whole panel remains 1240 x 286 and inside the 1280 x 720 canvas.

Use the existing simulated pointer to press node 2, drag right/up, release,
and assert frequency and gain increase. Focus Q, call
`applyFocusedParameterDelta(state, 1)`, and assert Q increases. Click Enabled
and Reset band, asserting the canonical nested state after each.

- [ ] **Step 2: Run the LVGL test to verify it fails**

```bash
cmake --build build --target pedal-lvgl-ui-smoke -j2
./build/pedal-lvgl-ui-smoke
```

Expected: missing labels/lines and unchanged EQ state.

- [ ] **Step 3: Add persistent editor widget state to `LvglUi`**

Add to `LvglUi.h`:

```cpp
std::size_t selectedEqBand_ = 0;
EqBandField focusedEqField_ = EqBandField::Frequency;
std::array<std::array<lv_point_precise_t, kEqCurvePointCount>, 6> eqCurvePoints_{};
std::array<lv_obj_t*, 6> eqCurveLines_{};
std::array<lv_obj_t*, kParametricEqBandCount> eqNodes_{};
lv_obj_t* eqFrequencyValue_ = nullptr;
lv_obj_t* eqQValue_ = nullptr;
lv_obj_t* eqGainValue_ = nullptr;
lv_obj_t* eqPlot_ = nullptr;
uint32_t lastEqCurveRefreshMs_ = 0;
```

Reset the pointers at the start of `build()`. Reset `selectedEqBand_` and EQ
focus when selecting a different block or preset.

- [ ] **Step 4: Branch the parameter panel to the custom EQ renderer**

At the start of the body portion of `renderParameterPanel()`, detect:

```cpp
const bool isEq = state.paramTarget == UiParamTarget::Block
  && state.selectedBlock < blocks.size()
  && blocks[state.selectedBlock].type == "eq"
  && isParametricEqMode(blocks[state.selectedBlock].params);
```

Keep the existing header/title/bypass/close controls. If `isEq`, call
`renderEqEditor(panelObject, state, context)` and return before page navigation
or knob creation.

- [ ] **Step 5: Render the approved layout B**

Implement `renderEqEditor()` with these fixed panel-local rectangles:

```cpp
constexpr int kPlotX = 62;
constexpr int kPlotY = 57;
constexpr int kPlotWidth = 1140;
constexpr int kPlotHeight = 132;
constexpr int kStripY = 198;
constexpr int kStripHeight = 68;
```

Create the grid and ±18/±9/0 plus 20/100/1k/10k/20k labels. Create five faint
line objects followed by one orange combined line, all backed by
`eqCurvePoints_`. Create five clickable node objects with labels 1–5 and a
white ring on `selectedEqBand_`. Create the bottom strip with band status,
three value pads, and Reset band. Use existing Ardor panel, accent, muted, and
font constants so the new editor matches the rest of the UI.

Also add `std::pair{"EQ", "eq"}` to the block drawer's filter list so the new
category is reachable from the approved UI.

Add formatters:

```cpp
std::string formatEqFrequency(float hz); // "80 Hz", "2.50 kHz"
std::string formatEqQ(float q);          // two decimals
std::string formatEqGain(float db);      // signed, one decimal, " dB"
```

- [ ] **Step 6: Update curves and widgets without rebuilding during drag**

Implement `refreshEqEditorVisuals()` to call `makeEqCurveData()`, fill all six
point arrays using the geometry helpers, set node positions from each band's
frequency/gain, dim disabled bands, and update the three value labels. Return
early during continuous drag when fewer than 33 ms have elapsed since
`lastEqCurveRefreshMs_`; force one final refresh on release.

- [ ] **Step 7: Implement node, value-pad, toggle, and reset events**

Node press selects the band's `context->index`, stores the canvas-space press
point, and begins interaction. Node pressing converts the current pointer to
plot-local coordinates, then writes:

```cpp
band.frequencyHz = eqFrequencyFromX(localX, kPlotWidth);
band.gainDb = eqGainFromY(localY, kPlotHeight);
```

Value pads set `focusedEqField_`; vertical dragging converts each 12 pixels to
one tick and calls `adjustEqBandField()`. The hardware encoder path detects a
selected EQ before generic `parameterPage()` lookup and applies the same
function. Enabled toggles `band.enabled`. Reset replaces the band with
`defaultParametricEqBand(selectedEqBand_)`. Every successful edit calls the
single private `LvglUi::commitEqBandEdit()` helper. In this task, declare the
helper in `LvglUi.h`; its implementation calls `setSelectedEqBand()` and
refreshes the widgets. Task 6 extends that same helper with the live-engine
callback, so gesture code remains unchanged.

- [ ] **Step 8: Run the LVGL smoke test**

```bash
cmake --build build --target pedal-lvgl-ui-smoke -j2
./build/pedal-lvgl-ui-smoke
```

Expected: exit code 0.

- [ ] **Step 9: Commit the custom editor**

```bash
git add src/ui/LvglUi.h src/ui/LvglUi.cpp tests/lvgl_ui_smoke.cpp
git commit -m "feat: add interactive eq response editor"
```

## Task 6: Wire immediate UI edits to the running engine

**Files:**
- Modify: `src/ui/LvglUi.h`
- Modify: `src/ui/LvglUi.cpp`
- Modify: `apps/pedal-poc/main.cpp`
- Modify: `tests/engine_contract_smoke.cpp`
- Modify: `tests/lvgl_ui_smoke.cpp`

- [ ] **Step 1: Add failing callback and engine-update tests**

In `tests/lvgl_ui_smoke.cpp`, construct `LvglUi` with an EQ update callback
that captures block ID, band index, and typed band. Drag a node and assert the
callback fires with the selected block's stable ID and canonical clamped data.

In `tests/engine_contract_smoke.cpp`, add an EQ, then assert:

```cpp
require(engine.setParametricEqBand("eq-live", 2, {true, 1000.0f, 1.0f, 12.0f}),
        "live EQ update accepted");
require(!engine.setParametricEqBand("missing", 2, {true, 1000.0f, 1.0f, 12.0f}),
        "stale EQ block ID rejected");
```

Render 200 64-frame sine blocks and assert output remains finite and the final
RMS differs from the neutral pre-edit RMS. Disable the safety limiter before
this measurement so the protective clipper does not hide the EQ gain change.

- [ ] **Step 2: Run both tests to verify callback failure**

```bash
cmake --build build --target pedal-lvgl-ui-smoke pedal-engine-contract-smoke -j2
```

Expected: compilation fails because `UiActions` has no EQ callback.

- [ ] **Step 3: Extend `UiActions` with the typed live-update callback**

In `LvglUi.h`:

```cpp
struct UiActions {
  std::function<void(std::size_t)> selectPreset;
  std::function<void()> savePreset;
  std::function<void(const std::string&, std::size_t, const EqBandParams&)> updateEqBand;
};
```

Replace the Task 5 helper body with:

```cpp
bool LvglUi::commitEqBandEdit(UiState& state, std::size_t band, EqBandParams params)
{
  if (!setSelectedEqBand(state, band, params)) return false;
  const auto& block = state.bank.presets[state.activePreset].blocks[state.selectedBlock];
  const auto canonical = selectedParametricEqParams(state).bands[band];
  if (actions_.updateEqBand) actions_.updateEqBand(block.id, band, canonical);
  refreshEqEditorVisuals(state, true);
  return true;
}
```

Route node drag, value-pad drag, encoder, enable, and reset through this helper.

- [ ] **Step 4: Wire the active `PedalEngine` in the realtime app**

Add the third `UiActions` initializer in `apps/pedal-poc/main.cpp`:

```cpp
[&](const std::string& blockId, std::size_t band, const ardor::EqBandParams& params) {
  if (!liveEngine->setParametricEqBand(blockId, band, params)) {
    std::cerr << "Ignored stale EQ update for " << blockId << "\n";
  }
},
```

The UI loop and preset swap already run on the same control thread, so the
`liveEngine` pointer remains valid for the duration of the callback. The audio
thread only observes atomic band targets.

- [ ] **Step 5: Run callback and engine contract tests**

```bash
cmake --build build --target pedal-lvgl-ui-smoke pedal-engine-contract-smoke pedal-poc -j2
./build/pedal-lvgl-ui-smoke
./build/pedal-engine-contract-smoke
```

Expected: both tests exit 0 and `pedal-poc` links successfully.

- [ ] **Step 6: Commit live preview wiring**

```bash
git add src/ui/LvglUi.h src/ui/LvglUi.cpp apps/pedal-poc/main.cpp \
  tests/lvgl_ui_smoke.cpp tests/engine_contract_smoke.cpp
git commit -m "feat: preview eq edits in the live engine"
```

## Task 7: Benchmark, documentation, and full verification

**Files:**
- Modify: `tests/dsp_bench.cpp`
- Modify: `README.md`

- [ ] **Step 1: Add the five-band benchmark case**

Include `equalizer/ParametricEqProcessor.h`, configure all five bands enabled
with gains `{6, -6, 9, -9, 12}` dB, and add:

```cpp
report("eq/parametric5", bench([&](const float* in, float* out, size_t frames) {
  std::array<float, kBlockSize> right{};
  std::copy(in, in + frames, right.begin());
  equalizer.processBlock(in, right.data(), out, right.data(), frames);
}));
```

The benchmark remains informational rather than a CTest pass/fail gate because
shared-host timings are noisy.

- [ ] **Step 2: Document the preset contract**

Add to README's supported parameters:

```markdown
Five Band EQ blocks use `type: "eq"`, mode `parametric_eq_5`, and five entries
in `params.bands`. Every band stores `enabled`, `frequency_hz` (20–20,000 Hz),
`q` (0.1–18), and `gain_db` (-18 to +18 dB). Missing fields receive the indexed
band defaults; saved presets contain exactly five complete bands.
```

- [ ] **Step 3: Build and run the complete desktop test suite**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

Expected: build succeeds and every registered test passes.

- [ ] **Step 4: Run the DSP benchmark and inspect the EQ row**

```bash
./build/pedal-dsp-bench
```

Expected: an `eq/parametric5` row with finite timings. Record the Pi measurement
in the implementation handoff; do not turn a desktop timing into a hardware
claim.

- [ ] **Step 5: Verify the UI-backend-none build**

```bash
cmake -S . -B build-no-ui -DARDOR_UI_BACKEND=none -DCMAKE_BUILD_TYPE=Release
cmake --build build-no-ui -j2
ctest --test-dir build-no-ui --output-on-failure
```

Expected: non-LVGL targets compile and all registered no-UI tests pass.

- [ ] **Step 6: Verify the Raspberry Pi Buildroot package**

When the documented `buildroot_vol` Docker volume is available, run the same
package-clean build used by `BUILD.md`:

```bash
docker run --rm \
  -v buildroot_vol:/buildroot \
  -v "$PWD":/ardor:ro \
  -w /buildroot \
  ubuntu:24.04 bash -lc '
    apt-get update &&
    DEBIAN_FRONTEND=noninteractive apt-get install -y build-essential git rsync cpio unzip file bc wget python3 &&
    make raspberrypi4_ardor_pedal_defconfig BR2_EXTERNAL=/ardor/buildroot/external &&
    make ardor-pedal-dirclean BR2_EXTERNAL=/ardor/buildroot/external &&
    make ardor-pedal BR2_EXTERNAL=/ardor/buildroot/external
  '
```

Expected: the `ardor-pedal` package builds with `ARDOR_UI_BACKEND=fbdev`. If
the named volume is absent, report that external prerequisite explicitly in
the handoff rather than claiming Pi verification.

- [ ] **Step 7: Manually inspect the simulator interaction**

```bash
./build/pedal-ui-sim --data-root . --bank 0
```

Add Five Band EQ, open it, and verify node selection, two-axis drag, Q pad,
hardware/mouse-wheel equivalent encoder path, enable dimming, reset, save, and
reload match the approved layout. Close the simulator after the check.

- [ ] **Step 8: Commit benchmark and documentation**

```bash
git add tests/dsp_bench.cpp README.md
git commit -m "docs: document and benchmark five-band eq"
```

- [ ] **Step 9: Confirm the worktree contains only intended changes**

```bash
git status --short
git log -8 --oneline
```

Expected: the seven EQ commits are present. Preserve any unrelated pre-existing
working-tree changes; do not include or discard them.
