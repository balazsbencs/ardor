# Audio Engine Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

> **STATUS: COMPLETE — DO NOT RE-EXECUTE.** All tasks landed on or before 2026-07-08; the unchecked boxes below are historical. Verify against the source tree, not the checkboxes. Listed under "Already Implemented" in the master roadmap.

**Goal:** Make saved presets drive the realtime/offline audio engine through a small, testable chain contract.

**Architecture:** Reuse the existing preset JSON, `ChainPlan`, `PedalEngine`, and `MiniaudioBackend`. Add only the missing glue: resolved chain assets, shared WAV loading, a preset-to-engine loader, UI/preset conversion, and telemetry state.

**Tech Stack:** C++20, CMake, nlohmann/json from NeuralAmpModelerCore dependencies, miniaudio, existing assert-style smoke tests.

## Global Constraints

- Keep one serial chain for v1.
- Relative asset paths only; absolute paths and `..` traversal stay rejected.
- Realtime default target remains `48000 Hz`, `--block-size 64`, `--ir-samples 8192`.
- V1 runtime loads the first ready `nam` block and first ready `cab` block; later DSP block types are pass-through placeholders.
- Do not polish LVGL theme in this phase.
- Do not add new dependencies.

---

## File Structure

- `src/preset/ChainPlan.h/.cpp`: extend the existing preset-to-runtime contract with resolved asset paths, params, and linear global gains.
- `src/audio/WavIo.h/.cpp`: move mono WAV decode out of `apps/pedal-poc/main.cpp` so runtime and CLI share it.
- `src/audio/EngineLoader.h/.cpp`: apply a `ChainPlan` to `PedalEngine` by loading NAM, cab IR, and global gains.
- `src/ui/UiModel.h/.cpp`: add conversion between current UI demo blocks and `Preset`/`PresetBlock`.
- `apps/pedal-poc/main.cpp`: switch duplicated WAV reading and manual engine setup to the shared helpers.
- `tests/preset_smoke.cpp`, `tests/offline_smoke.cpp`, `tests/ui_model_smoke.cpp`: grow existing smoke checks instead of adding a test framework.
- `CMakeLists.txt`: add new source files to existing libraries.

---

### Task 1: Extend ChainPlan Into The Preset-To-Engine Contract

**Files:**
- Modify: `src/preset/ChainPlan.h`
- Modify: `src/preset/ChainPlan.cpp`
- Modify: `tests/preset_smoke.cpp`

**Interfaces:**
- Consumes: `ardor::Preset`, `ardor::PresetBlock`, `ardor::isValidBlockAssetPath`
- Produces:
  - `float ardor::dbToGain(float db)`
  - `ChainBlockPlan::assetPath`
  - `ChainBlockPlan::params`
  - `ChainPlan::inputGain`, `outputGain`, `safetyLimit`

- [ ] **Step 1: Write the failing test**

Add assertions to the existing chain-plan section in `tests/preset_smoke.cpp` after creating `dataRoot`:

```cpp
std::filesystem::create_directories(dataRoot / "irs");
std::ofstream(dataRoot / "irs/ok.wav").put('\n');

chainPreset.global.inputGainDb = -6.0f;
chainPreset.global.outputGainDb = -3.0f;
chainPreset.global.safetyLimitDb = -2.0f;
chainPreset.blocks[0].params = nlohmann::json{{"levelDb", -1.0f}};
chainPreset.blocks.push_back({"cab-ready", "cab", true, "irs/ok.wav", nlohmann::json{{"mix", 1.0f}}});
```

Add these assertions after the `buildChainPlan(chainPreset, dataRoot)` call:

```cpp
require(std::fabs(plan.inputGain - ardor::dbToGain(-6.0f)) < 0.0001f, "chain input gain");
require(std::fabs(plan.outputGain - ardor::dbToGain(-3.0f)) < 0.0001f, "chain output gain");
require(std::fabs(plan.safetyLimit - ardor::dbToGain(-2.0f)) < 0.0001f, "chain safety limit");
require(plan.blocks[0].assetPath == dataRoot / "models/ok.nam", "resolved nam asset");
require(plan.blocks[0].params.at("levelDb").get<float>() == -1.0f, "chain params copied");
require(plan.blocks.back().assetPath == dataRoot / "irs/ok.wav", "resolved cab asset");
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
cmake --build build --target pedal-preset-smoke && ./build/pedal-preset-smoke
```

Expected: compile failure for missing `dbToGain`, `inputGain`, `assetPath`, or `params`.

- [ ] **Step 3: Write minimal implementation**

Update `src/preset/ChainPlan.h`:

```cpp
#include <nlohmann/json.hpp>

struct ChainBlockPlan {
  std::string id;
  std::string type;
  ChainBlockStatus status = ChainBlockStatus::Ready;
  std::filesystem::path assetPath;
  nlohmann::json params = nlohmann::json::object();
};

struct ChainPlan {
  std::vector<ChainBlockPlan> blocks;
  std::size_t runnableBlockCount = 0;
  float inputGain = 1.0f;
  float outputGain = 1.0f;
  float safetyLimit = 0.8912509f;
};

float dbToGain(float db);
```

Update `src/preset/ChainPlan.cpp`:

```cpp
#include <cmath>

float dbToGain(float db)
{
  return std::pow(10.0f, db / 20.0f);
}

ChainPlan buildChainPlan(const Preset& preset, const std::filesystem::path& dataRoot)
{
  ChainPlan plan;
  plan.inputGain = dbToGain(preset.global.inputGainDb);
  plan.outputGain = dbToGain(preset.global.outputGainDb);
  plan.safetyLimit = dbToGain(preset.global.safetyLimitDb);

  for (const auto& block : preset.blocks) {
    ChainBlockPlan blockPlan;
    blockPlan.id = block.id;
    blockPlan.type = block.type;
    blockPlan.params = block.params.is_null() ? nlohmann::json::object() : block.params;
    if (isValidBlockAssetPath(block.asset)) {
      blockPlan.assetPath = dataRoot / block.asset;
    }
    if (!block.enabled) {
      blockPlan.status = ChainBlockStatus::Disabled;
    } else if (!isSupportedBlockType(block.type)) {
      blockPlan.status = ChainBlockStatus::Unsupported;
    } else if (block.asset.empty()) {
      blockPlan.status = ChainBlockStatus::MissingAsset;
    } else if (!isValidBlockAssetPath(block.asset)) {
      blockPlan.status = ChainBlockStatus::MissingAsset;
    } else if (!std::filesystem::exists(blockPlan.assetPath)) {
      blockPlan.status = ChainBlockStatus::MissingAsset;
    } else {
      blockPlan.status = ChainBlockStatus::Ready;
      ++plan.runnableBlockCount;
    }

    plan.blocks.push_back(std::move(blockPlan));
  }
  return plan;
}
```

Keep the existing status logic; set `assetPath` before the status checks and increment `runnableBlockCount` only for `Ready`.

- [ ] **Step 4: Run test to verify it passes**

Run:

```bash
cmake --build build --target pedal-preset-smoke && ./build/pedal-preset-smoke
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/preset/ChainPlan.h src/preset/ChainPlan.cpp tests/preset_smoke.cpp
git commit -m "feat: expose preset engine chain plan"
```

---

### Task 2: Move WAV Decode Into A Shared Helper

**Files:**
- Create: `src/audio/WavIo.h`
- Create: `src/audio/WavIo.cpp`
- Modify: `apps/pedal-poc/main.cpp`
- Modify: `tests/offline_smoke.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: miniaudio
- Produces:
  - `struct MonoWav { std::vector<float> samples; uint32_t sampleRate = 0; };`
  - `MonoWav readMonoWav(const std::filesystem::path& path);`

- [ ] **Step 1: Write the failing test**

In `tests/offline_smoke.cpp`, include `audio/WavIo.h` and `miniaudio.h`. Add a tiny temp WAV write/read check near the start:

```cpp
const auto wavPath = std::filesystem::temp_directory_path() / "ardor-wav-io-smoke.wav";
{
  const float samples[] = {0.25f, -0.5f};
  ma_encoder_config cfg = ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32, 1, 48000);
  ma_encoder encoder;
  require(ma_encoder_init_file(wavPath.string().c_str(), &cfg, &encoder) == MA_SUCCESS);
  ma_encoder_write_pcm_frames(&encoder, samples, 2, nullptr);
  ma_encoder_uninit(&encoder);
}
const auto wav = ardor::readMonoWav(wavPath);
require(wav.sampleRate == 48000);
require(wav.samples.size() == 2);
require(std::fabs(wav.samples[0] - 0.25f) < 0.0001f);
std::filesystem::remove(wavPath);
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
cmake --build build --target pedal-offline-smoke
```

Expected: compile failure for missing `audio/WavIo.h` or `readMonoWav`.

- [ ] **Step 3: Write minimal implementation**

Create `src/audio/WavIo.h`:

```cpp
#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace ardor {

struct MonoWav {
  std::vector<float> samples;
  uint32_t sampleRate = 0;
};

MonoWav readMonoWav(const std::filesystem::path& path);

} // namespace ardor
```

Create `src/audio/WavIo.cpp` by moving the current `readMono` logic from `apps/pedal-poc/main.cpp`:

```cpp
#include "audio/WavIo.h"

#include "miniaudio.h"

#include <stdexcept>

namespace ardor {

MonoWav readMonoWav(const std::filesystem::path& path)
{
  ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 1, 48000);
  ma_decoder decoder;
  if (ma_decoder_init_file(path.string().c_str(), &cfg, &decoder) != MA_SUCCESS) {
    throw std::runtime_error("failed to open wav: " + path.string());
  }

  MonoWav wav;
  wav.sampleRate = decoder.outputSampleRate;
  float chunk[4096];
  for (;;) {
    ma_uint64 framesRead = 0;
    ma_decoder_read_pcm_frames(&decoder, chunk, 4096, &framesRead);
    if (framesRead == 0) break;
    wav.samples.insert(wav.samples.end(), chunk, chunk + framesRead);
  }
  ma_decoder_uninit(&decoder);
  return wav;
}

} // namespace ardor
```

Update `CMakeLists.txt`:

```cmake
add_library(ardor_audio
  src/audio/MiniaudioBackend.cpp
  src/audio/WavIo.cpp
)

target_link_libraries(pedal-offline-smoke PRIVATE ardor_dsp ardor_audio)
```

In `apps/pedal-poc/main.cpp`, replace calls to `readMono(args.ir, irRate)` with:

```cpp
auto irWav = ardor::readMonoWav(args.ir);
auto impulse = std::move(irWav.samples);
const ma_uint32 irRate = irWav.sampleRate;
```

Do the same for input WAV. Leave `writeStereo` in `main.cpp` for now.

- [ ] **Step 4: Run test to verify it passes**

Run:

```bash
cmake --build build --target pedal-offline-smoke pedal-poc && ./build/pedal-offline-smoke
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt apps/pedal-poc/main.cpp src/audio/WavIo.h src/audio/WavIo.cpp tests/offline_smoke.cpp
git commit -m "refactor: share mono wav loading"
```

---

### Task 3: Apply ChainPlan To PedalEngine

**Files:**
- Create: `src/audio/EngineLoader.h`
- Create: `src/audio/EngineLoader.cpp`
- Modify: `tests/offline_smoke.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `ardor::ChainPlan`, `ardor::PedalEngine`, `ardor::readMonoWav`
- Produces:
  - `struct EngineLoadOptions { uint32_t sampleRate; uint32_t blockSize; size_t irSamples; };`
  - `bool applyChainPlan(PedalEngine& engine, const ChainPlan& plan, const EngineLoadOptions& options, std::string& error);`

- [ ] **Step 1: Write the failing test**

In `tests/offline_smoke.cpp`, include `audio/EngineLoader.h` and `preset/ChainPlan.h`. Add this test after the WAV IO check:

```cpp
ardor::ChainPlan cabOnly;
cabOnly.inputGain = 1.0f;
cabOnly.outputGain = 0.5f;
cabOnly.safetyLimit = 1.0f;
cabOnly.blocks.push_back({"cab-1", "cab", ardor::ChainBlockStatus::Ready, wavPath, nlohmann::json::object()});

ardor::PedalEngine loadedEngine;
std::string loadError;
require(ardor::applyChainPlan(loadedEngine, cabOnly, {48000, 64, 8192}, loadError));
auto [loadedLeft, loadedRight] = loadedEngine.process(0.5f);
require(std::fabs(loadedLeft - 0.0625f) < 0.0001f);
require(std::fabs(loadedRight - 0.0625f) < 0.0001f);
```

Use an impulse WAV whose first sample is `0.25f`; input `0.5 * impulse 0.25 * outputGain 0.5 = 0.0625`.

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
cmake --build build --target pedal-offline-smoke
```

Expected: compile failure for missing `audio/EngineLoader.h` or `applyChainPlan`.

- [ ] **Step 3: Write minimal implementation**

Create `src/audio/EngineLoader.h`:

```cpp
#pragma once

#include "dsp/PedalEngine.h"
#include "preset/ChainPlan.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace ardor {

struct EngineLoadOptions {
  uint32_t sampleRate = 48000;
  uint32_t blockSize = 64;
  size_t irSamples = 8192;
};

bool applyChainPlan(PedalEngine& engine, const ChainPlan& plan, const EngineLoadOptions& options, std::string& error);

} // namespace ardor
```

Create `src/audio/EngineLoader.cpp`:

```cpp
#include "audio/EngineLoader.h"

#include "audio/WavIo.h"

#include <algorithm>

namespace ardor {

bool applyChainPlan(PedalEngine& engine, const ChainPlan& plan, const EngineLoadOptions& options, std::string& error)
{
  engine.setInputGain(plan.inputGain);
  engine.setOutputGain(plan.outputGain);
  engine.setSafetyLimit(plan.safetyLimit);
  engine.setSafetyLimiterEnabled(true);

  for (const auto& block : plan.blocks) {
    if (block.status != ChainBlockStatus::Ready) continue;
    if (block.type == "nam") {
      if (!engine.loadNam(block.assetPath, options.sampleRate, static_cast<int>(options.blockSize))) {
        error = "failed to load NAM: " + block.assetPath.string();
        return false;
      }
      continue;
    }
    if (block.type == "cab") {
      auto wav = readMonoWav(block.assetPath);
      if (wav.sampleRate != options.sampleRate) {
        error = "IR sample rate mismatch: " + block.assetPath.string();
        return false;
      }
      if (options.irSamples > 0 && wav.samples.size() > options.irSamples) {
        wav.samples.resize(options.irSamples);
      }
      engine.loadIr(std::move(wav.samples));
      continue;
    }
  }

  engine.reset();
  return true;
}

} // namespace ardor
```

Update `CMakeLists.txt` so `ardor_audio` compiles `src/audio/EngineLoader.cpp` and links `ardor_preset`.

- [ ] **Step 4: Run test to verify it passes**

Run:

```bash
cmake --build build --target pedal-offline-smoke && ./build/pedal-offline-smoke
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/audio/EngineLoader.h src/audio/EngineLoader.cpp tests/offline_smoke.cpp
git commit -m "feat: load engine from chain plan"
```

---

### Task 4: Bridge UI Presets To Persisted Presets

**Files:**
- Modify: `src/ui/UiModel.h`
- Modify: `src/ui/UiModel.cpp`
- Modify: `tests/ui_model_smoke.cpp`

**Interfaces:**
- Consumes: `UiState`, `UiPreset`, `UiBlock`, `Preset`
- Produces:
  - `Preset activePresetToPreset(const UiState& state);`
  - `void replaceActivePreset(UiState& state, const Preset& preset);`

- [ ] **Step 1: Write the failing test**

In `tests/ui_model_smoke.cpp`, after block insertion/reorder checks:

```cpp
const ardor::Preset savedPreset = ardor::activePresetToPreset(state);
if (require(savedPreset.name == state.bank.presets[state.activePreset].name, "ui preset name maps to preset")) return 1;
if (require(savedPreset.routing == "serial", "ui preset routing is serial")) return 1;
if (require(savedPreset.blocks.size() == state.bank.presets[state.activePreset].blocks.size(), "ui blocks map to preset blocks")) return 1;
if (require(savedPreset.blocks[0].asset == state.bank.presets[state.activePreset].blocks[0].assetPath, "ui block asset maps")) return 1;

ardor::Preset replacement;
replacement.name = "Loaded From Disk";
replacement.blocks.push_back({"loaded-1", "cab", true, "irs/loaded.wav", nlohmann::json::object()});
ardor::replaceActivePreset(state, replacement);
if (require(state.bank.presets[state.activePreset].name == "Loaded From Disk", "preset load updates ui name")) return 1;
if (require(state.bank.presets[state.activePreset].blocks.size() == 1, "preset load updates ui block count")) return 1;
if (require(state.bank.presets[state.activePreset].blocks[0].assetPath == "irs/loaded.wav", "preset load updates asset path")) return 1;
if (require(!state.dirty, "loading preset clears dirty flag")) return 1;
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
cmake --build build --target pedal-ui-model-smoke
```

Expected: compile failure for missing conversion functions.

- [ ] **Step 3: Write minimal implementation**

Update `src/ui/UiModel.h`:

```cpp
#include "preset/Preset.h"

Preset activePresetToPreset(const UiState& state);
void replaceActivePreset(UiState& state, const Preset& preset);
```

Update `src/ui/UiModel.cpp`:

```cpp
Preset activePresetToPreset(const UiState& state)
{
  Preset preset;
  const auto& uiPreset = state.bank.presets[state.activePreset];
  preset.name = uiPreset.name;
  preset.routing = "serial";
  preset.global.outputGainDb = 0.0f;
  for (const auto& block : uiPreset.blocks) {
    preset.blocks.push_back({block.id, block.type, block.enabled, block.assetPath, nlohmann::json::object()});
  }
  return preset;
}

void replaceActivePreset(UiState& state, const Preset& preset)
{
  auto& uiPreset = state.bank.presets[state.activePreset];
  uiPreset.name = preset.name;
  uiPreset.blocks.clear();
  for (const auto& block : preset.blocks) {
    uiPreset.blocks.push_back({block.id, block.type, block.type == "nam" ? "Neural Amp" : block.type == "cab" ? "Cab" : block.type, block.asset, block.asset, block.enabled});
  }
  state.selectedBlock = 0;
  state.dirty = false;
  state.paramDrawerOpen = false;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run:

```bash
cmake --build build --target pedal-ui-model-smoke && ./build/pedal-ui-model-smoke
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/ui/UiModel.h src/ui/UiModel.cpp tests/ui_model_smoke.cpp
git commit -m "feat: map ui presets to saved presets"
```

---

### Task 5: Surface Realtime Safety State

**Files:**
- Modify: `src/preset/RuntimeState.h`
- Modify: `src/preset/RuntimeState.cpp`
- Modify: `tests/preset_smoke.cpp`
- Modify: `apps/pedal-poc/main.cpp`

**Interfaces:**
- Consumes: `MiniaudioBackend::stats()`, `RuntimeState`
- Produces:
  - `void RuntimeState::observeRealtimeStats(uint64_t previousOverBudget, uint64_t currentOverBudget);`
  - CLI uses `RuntimeState` to latch engine bypass after repeated overload growth.

- [ ] **Step 1: Write the failing test**

In the existing `RuntimeState` section of `tests/preset_smoke.cpp`, add:

```cpp
ardor::RuntimeState statsRuntime;
statsRuntime.observeRealtimeStats(0, 1);
require(!statsRuntime.effectsBypassed(), "first stats overload does not bypass");
statsRuntime.observeRealtimeStats(1, 2);
require(!statsRuntime.effectsBypassed(), "second stats overload does not bypass");
statsRuntime.observeRealtimeStats(2, 3);
require(statsRuntime.effectsBypassed(), "third stats overload bypasses");
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
cmake --build build --target pedal-preset-smoke
```

Expected: compile failure for missing `observeRealtimeStats`.

- [ ] **Step 3: Write minimal implementation**

Update `src/preset/RuntimeState.h`:

```cpp
#include <cstdint>

void observeRealtimeStats(uint64_t previousOverBudget, uint64_t currentOverBudget);
```

Update `src/preset/RuntimeState.cpp`:

```cpp
void RuntimeState::observeRealtimeStats(uint64_t previousOverBudget, uint64_t currentOverBudget)
{
  if (currentOverBudget > previousOverBudget) {
    reportOverload();
  } else {
    reportStableCallback();
  }
}
```

Update the realtime loop in `apps/pedal-poc/main.cpp`:

```cpp
ardor::RuntimeState runtime;
uint64_t previousOverBudget = 0;
for (;;) {
  std::this_thread::sleep_for(std::chrono::seconds(1));
  const auto stats = backend.stats();
  runtime.observeRealtimeStats(previousOverBudget, stats.overBudget);
  previousOverBudget = stats.overBudget;
  engine.setEffectsBypassed(runtime.effectsBypassed());
  std::cerr << " bypassed=" << (runtime.effectsBypassed() ? 1 : 0);
}
```

Print `bypassed=0|1` in the existing stats line.

- [ ] **Step 4: Run test to verify it passes**

Run:

```bash
cmake --build build --target pedal-preset-smoke pedal-poc && ./build/pedal-preset-smoke
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/preset/RuntimeState.h src/preset/RuntimeState.cpp tests/preset_smoke.cpp apps/pedal-poc/main.cpp
git commit -m "feat: latch bypass from realtime overload stats"
```

---

## Final Verification

Run:

```bash
cmake --build build --target pedal-poc pedal-ui-sim pedal-offline-smoke pedal-preset-smoke pedal-engine-contract-smoke pedal-ui-model-smoke
ctest --test-dir build --output-on-failure
python3 - <<'PY'
import os, subprocess, time
env=os.environ.copy()
env['SDL_VIDEODRIVER']='dummy'
p=subprocess.Popen(['./build/pedal-ui-sim'], cwd='.', env=env)
time.sleep(1)
code=p.poll()
if code is not None:
    raise SystemExit(f'pedal-ui-sim exited early with code {code}')
p.terminate()
try:
    p.wait(timeout=3)
except subprocess.TimeoutExpired:
    p.kill()
    p.wait()
print('pedal-ui-sim launch smoke passed')
PY
```

Expected: all tests pass and simulator launch smoke passes.

## Self-Review

- Spec coverage: contract, engine reload, v1 NAM/cab chain, JSON preset bridge, and realtime safety are covered by Tasks 1-5.
- Placeholder scan: no `TBD`, `TODO`, or unfilled implementation slots.
- Type consistency: `ChainPlan`, `EngineLoadOptions`, `applyChainPlan`, `activePresetToPreset`, `replaceActivePreset`, and `observeRealtimeStats` are introduced before use.
- Ponytail check: skipped multi-NAM stereo, arbitrary DSP execution graph, UI theme polish, and hot-swapping active audio objects. Add those only after the single NAM/cab preset path is solid.
