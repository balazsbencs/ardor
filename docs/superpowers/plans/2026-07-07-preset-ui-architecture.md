# Preset UI Architecture Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

> **STATUS: COMPLETE — DO NOT RE-EXECUTE.** All tasks landed on or before 2026-07-08; the unchecked boxes below are historical. Verify against the source tree, not the checkboxes. Listed under "Already Implemented" in the master roadmap.

**Goal:** Build the preset data model, store, engine contract hooks, overload bypass state, and a small HTML mockup path defined by `docs/superpowers/specs/2026-07-07-preset-ui-architecture-design.md`.

**Architecture:** Add a small preset module that owns JSON serialization, preset slots, dirty working state, and chain validation. Keep the existing DSP engine as the realtime processor, adding only the master-volume and dry-bypass hooks needed by the preset contract. Keep the HTML mockup static and schema-driven so it validates layout without becoming production UI.

**Tech Stack:** C++20, CMake, existing assert-style C++ smoke tests, `nlohmann/json` from the already-fetched NeuralAmpModelerCore dependency, existing miniaudio/NAMCore DSP stack, static HTML/CSS/JS for mockups.

## Global Constraints

- The first UI-capable pedal version has one 5 inch touch display, one encoder, and four footswitches.
- Presets are 100 banks with 4 presets per bank, for 400 preset slots.
- Routing is serial only in this phase.
- The preset file format uses JSON with `version`, `name`, `routing`, `global`, and ordered `blocks`.
- `blocks` is an ordered array with no fixed block-count limit in the preset file.
- Block `asset` values are relative paths, such as `models/foo.nam` and `irs/bar.wav`.
- The first implemented block types are `nam` and `cab`.
- Edit mode changes the working preset immediately; Save persists it; Discard reloads the saved preset.
- The encoder controls system master output volume and must not dirty the active preset.
- Unknown block types and missing assets are preserved but bypassed.
- Repeated audio overload latches effects bypass until the user clears it or changes preset.
- Mute is reserved for unsafe audio or engine states.

---

## File Structure

- Create `src/preset/Preset.h`: preset structs and JSON conversion declarations.
- Create `src/preset/Preset.cpp`: JSON parsing and serialization.
- Create `src/preset/PresetStore.h`: preset slot, store, and working session declarations.
- Create `src/preset/PresetStore.cpp`: preset path, atomic save, load, dirty/save/discard.
- Create `src/preset/ChainPlan.h`: chain validation result types.
- Create `src/preset/ChainPlan.cpp`: supported/missing/unsupported block validation.
- Modify `src/dsp/PedalEngine.h`: add master volume and dry bypass controls.
- Modify `src/dsp/PedalEngine.cpp`: apply master volume and bypass path.
- Create `tests/preset_smoke.cpp`: preset model/store/chain/session checks.
- Create `tests/engine_contract_smoke.cpp`: master volume and dry bypass checks.
- Create `mockups/preset-ui/index.html`: static desktop mockup using the same preset shape.
- Modify `CMakeLists.txt`: build `ardor_preset`, add the new tests, and link preset code to tests.
- Modify `README.md`: document the preset data root and mockup entry point.

### Task 1: Preset JSON Model

**Files:**
- Create: `src/preset/Preset.h`
- Create: `src/preset/Preset.cpp`
- Create: `tests/preset_smoke.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  - `namespace ardor`
  - `struct PresetGlobal`
  - `struct PresetBlock`
  - `struct Preset`
  - `nlohmann::json toJson(const Preset& preset)`
  - `Preset presetFromJson(const nlohmann::json& json)`

- [ ] **Step 1: Write the failing JSON round-trip test**

Add `tests/preset_smoke.cpp` with this first test content:

```cpp
#include "preset/Preset.h"

#include <cassert>
#include <string>

int main()
{
  const auto json = nlohmann::json::parse(R"({
    "version": 1,
    "name": "Clean Lead",
    "routing": "serial",
    "global": {
      "inputGainDb": -12.0,
      "outputGainDb": -6.0,
      "safetyLimitDb": -1.0
    },
    "blocks": [
      {
        "id": "block-1",
        "type": "nam",
        "enabled": true,
        "asset": "models/clean.nam",
        "params": { "levelDb": 0.0 }
      },
      {
        "id": "block-2",
        "type": "cab",
        "enabled": false,
        "asset": "irs/open-back.wav",
        "params": { "mix": 1.0, "levelDb": -3.0 }
      }
    ]
  })");

  const ardor::Preset preset = ardor::presetFromJson(json);
  assert(preset.version == 1);
  assert(preset.name == "Clean Lead");
  assert(preset.routing == "serial");
  assert(preset.global.inputGainDb == -12.0f);
  assert(preset.global.outputGainDb == -6.0f);
  assert(preset.global.safetyLimitDb == -1.0f);
  assert(preset.blocks.size() == 2);
  assert(preset.blocks[0].id == "block-1");
  assert(preset.blocks[0].type == "nam");
  assert(preset.blocks[0].enabled);
  assert(preset.blocks[0].asset == "models/clean.nam");
  assert(preset.blocks[1].type == "cab");
  assert(!preset.blocks[1].enabled);
  assert(preset.blocks[1].params.at("mix").get<float>() == 1.0f);

  const ardor::Preset roundTrip = ardor::presetFromJson(ardor::toJson(preset));
  assert(roundTrip.blocks.size() == 2);
  assert(roundTrip.blocks[1].id == "block-2");
  assert(roundTrip.blocks[1].params.at("levelDb").get<float>() == -3.0f);
  return 0;
}
```

- [ ] **Step 2: Register the failing test in CMake**

Add to `CMakeLists.txt` after the existing `ardor_dsp` library:

```cmake
add_library(ardor_preset
  src/preset/Preset.cpp
)
target_include_directories(ardor_preset PUBLIC
  src
  ${neuralampmodelercore_SOURCE_DIR}/Dependencies/nlohmann
)
target_compile_features(ardor_preset PUBLIC cxx_std_20)
```

Add the executable near the existing test executables:

```cmake
add_executable(pedal-preset-smoke tests/preset_smoke.cpp)
target_link_libraries(pedal-preset-smoke PRIVATE ardor_preset)
```

Add the test after `enable_testing()`:

```cmake
add_test(NAME pedal-preset-smoke COMMAND pedal-preset-smoke)
```

- [ ] **Step 3: Run the test to verify it fails**

Run:

```sh
cmake --build build
```

Expected: build fails because `preset/Preset.h` does not exist.

- [ ] **Step 4: Implement the preset structs and JSON conversion**

Create `src/preset/Preset.h`:

```cpp
#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace ardor {

struct PresetGlobal {
  float inputGainDb = 0.0f;
  float outputGainDb = 0.0f;
  float safetyLimitDb = -1.0f;
};

struct PresetBlock {
  std::string id;
  std::string type;
  bool enabled = true;
  std::string asset;
  nlohmann::json params = nlohmann::json::object();
};

struct Preset {
  int version = 1;
  std::string name;
  std::string routing = "serial";
  PresetGlobal global;
  std::vector<PresetBlock> blocks;
};

nlohmann::json toJson(const Preset& preset);
Preset presetFromJson(const nlohmann::json& json);

} // namespace ardor
```

Create `src/preset/Preset.cpp`:

```cpp
#include "Preset.h"

namespace ardor {

nlohmann::json toJson(const Preset& preset)
{
  nlohmann::json blocks = nlohmann::json::array();
  for (const auto& block : preset.blocks) {
    blocks.push_back({
      {"id", block.id},
      {"type", block.type},
      {"enabled", block.enabled},
      {"asset", block.asset},
      {"params", block.params.is_null() ? nlohmann::json::object() : block.params},
    });
  }

  return {
    {"version", preset.version},
    {"name", preset.name},
    {"routing", preset.routing},
    {"global", {
      {"inputGainDb", preset.global.inputGainDb},
      {"outputGainDb", preset.global.outputGainDb},
      {"safetyLimitDb", preset.global.safetyLimitDb},
    }},
    {"blocks", blocks},
  };
}

Preset presetFromJson(const nlohmann::json& json)
{
  Preset preset;
  preset.version = json.at("version").get<int>();
  preset.name = json.value("name", "");
  preset.routing = json.at("routing").get<std::string>();

  const auto& global = json.at("global");
  preset.global.inputGainDb = global.value("inputGainDb", 0.0f);
  preset.global.outputGainDb = global.value("outputGainDb", 0.0f);
  preset.global.safetyLimitDb = global.value("safetyLimitDb", -1.0f);

  for (const auto& blockJson : json.at("blocks")) {
    PresetBlock block;
    block.id = blockJson.at("id").get<std::string>();
    block.type = blockJson.at("type").get<std::string>();
    block.enabled = blockJson.value("enabled", true);
    block.asset = blockJson.value("asset", "");
    block.params = blockJson.value("params", nlohmann::json::object());
    preset.blocks.push_back(std::move(block));
  }

  return preset;
}

} // namespace ardor
```

- [ ] **Step 5: Run the test to verify it passes**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure -R pedal-preset-smoke
```

Expected: `pedal-preset-smoke` passes.

- [ ] **Step 6: Commit**

```sh
git add CMakeLists.txt src/preset/Preset.h src/preset/Preset.cpp tests/preset_smoke.cpp
git commit -m "feat: add preset json model"
```

### Task 2: Preset Store And Working Session

**Files:**
- Create: `src/preset/PresetStore.h`
- Create: `src/preset/PresetStore.cpp`
- Modify: `tests/preset_smoke.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes:
  - `ardor::Preset`
  - `ardor::toJson(const Preset&)`
  - `ardor::presetFromJson(const nlohmann::json&)`
- Produces:
  - `struct PresetSlot { int bank = 0; int preset = 0; }`
  - `class PresetStore`
  - `class PresetSession`
  - `bool samePreset(const Preset& left, const Preset& right)`

- [ ] **Step 1: Add failing store/session tests**

Append to `tests/preset_smoke.cpp`, before `return 0;`:

```cpp
  const auto root = std::filesystem::temp_directory_path() / "ardor-preset-smoke";
  std::filesystem::remove_all(root);
  ardor::PresetStore store(root);
  const ardor::PresetSlot slot{2, 3};

  ardor::Preset saved;
  saved.name = "Bank 2 Slot 3";
  saved.blocks.push_back({"block-a", "nam", true, "models/a.nam", nlohmann::json::object()});
  store.save(slot, saved);
  assert(std::filesystem::exists(root / "presets/bank-002/preset-3.json"));

  ardor::PresetSession session;
  session.load(store, slot);
  assert(!session.isDirty());
  session.working().name = "Edited";
  assert(session.isDirty());
  session.discard();
  assert(session.working().name == "Bank 2 Slot 3");
  assert(!session.isDirty());
  session.working().name = "Saved Edit";
  session.save();
  assert(!session.isDirty());
  assert(store.load(slot).name == "Saved Edit");

  std::filesystem::remove_all(root);
```

Also add includes at the top:

```cpp
#include "preset/PresetStore.h"

#include <filesystem>
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```sh
cmake --build build
```

Expected: build fails because `preset/PresetStore.h` does not exist.

- [ ] **Step 3: Implement store/session**

Create `src/preset/PresetStore.h`:

```cpp
#pragma once

#include "Preset.h"

#include <filesystem>

namespace ardor {

struct PresetSlot {
  int bank = 0;
  int preset = 0;
};

bool samePreset(const Preset& left, const Preset& right);

class PresetStore {
public:
  explicit PresetStore(std::filesystem::path root);

  std::filesystem::path pathFor(PresetSlot slot) const;
  Preset load(PresetSlot slot) const;
  void save(PresetSlot slot, const Preset& preset) const;

private:
  std::filesystem::path root_;
};

class PresetSession {
public:
  void load(const PresetStore& store, PresetSlot slot);
  Preset& working();
  const Preset& working() const;
  const Preset& saved() const;
  bool isDirty() const;
  void save();
  void discard();

private:
  const PresetStore* store_ = nullptr;
  PresetSlot slot_;
  Preset saved_;
  Preset working_;
};

} // namespace ardor
```

Create `src/preset/PresetStore.cpp`:

```cpp
#include "PresetStore.h"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace ardor {

namespace {

std::string bankDir(int bank)
{
  std::ostringstream out;
  out << "bank-" << std::setw(3) << std::setfill('0') << bank;
  return out.str();
}

void validateSlot(PresetSlot slot)
{
  if (slot.bank < 0 || slot.bank >= 100 || slot.preset < 0 || slot.preset >= 4) {
    throw std::out_of_range("preset slot out of range");
  }
}

} // namespace

bool samePreset(const Preset& left, const Preset& right)
{
  return toJson(left) == toJson(right);
}

PresetStore::PresetStore(std::filesystem::path root)
  : root_(std::move(root))
{
}

std::filesystem::path PresetStore::pathFor(PresetSlot slot) const
{
  validateSlot(slot);
  return root_ / "presets" / bankDir(slot.bank) / ("preset-" + std::to_string(slot.preset) + ".json");
}

Preset PresetStore::load(PresetSlot slot) const
{
  const auto path = pathFor(slot);
  std::ifstream in(path);
  if (!in) {
    throw std::runtime_error("failed to open preset: " + path.string());
  }
  nlohmann::json json;
  in >> json;
  return presetFromJson(json);
}

void PresetStore::save(PresetSlot slot, const Preset& preset) const
{
  const auto path = pathFor(slot);
  std::filesystem::create_directories(path.parent_path());
  const auto tmp = path.string() + ".tmp";
  {
    std::ofstream out(tmp, std::ios::trunc);
    if (!out) {
      throw std::runtime_error("failed to write preset: " + tmp);
    }
    out << toJson(preset).dump(2) << '\n';
  }
  std::filesystem::rename(tmp, path);
}

void PresetSession::load(const PresetStore& store, PresetSlot slot)
{
  store_ = &store;
  slot_ = slot;
  saved_ = store.load(slot);
  working_ = saved_;
}

Preset& PresetSession::working()
{
  return working_;
}

const Preset& PresetSession::working() const
{
  return working_;
}

const Preset& PresetSession::saved() const
{
  return saved_;
}

bool PresetSession::isDirty() const
{
  return !samePreset(saved_, working_);
}

void PresetSession::save()
{
  if (!store_) {
    throw std::runtime_error("no preset store loaded");
  }
  store_->save(slot_, working_);
  saved_ = working_;
}

void PresetSession::discard()
{
  working_ = saved_;
}

} // namespace ardor
```

- [ ] **Step 4: Add the store source to CMake**

Change the `ardor_preset` target to:

```cmake
add_library(ardor_preset
  src/preset/Preset.cpp
  src/preset/PresetStore.cpp
)
```

- [ ] **Step 5: Run the test to verify it passes**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure -R pedal-preset-smoke
```

Expected: `pedal-preset-smoke` passes.

- [ ] **Step 6: Commit**

```sh
git add CMakeLists.txt src/preset/PresetStore.h src/preset/PresetStore.cpp tests/preset_smoke.cpp
git commit -m "feat: add preset store session"
```

### Task 3: Chain Validation For Missing And Unsupported Blocks

**Files:**
- Create: `src/preset/ChainPlan.h`
- Create: `src/preset/ChainPlan.cpp`
- Modify: `tests/preset_smoke.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes:
  - `ardor::Preset`
  - `ardor::PresetBlock`
- Produces:
  - `enum class ChainBlockStatus`
  - `struct ChainBlockPlan`
  - `struct ChainPlan`
  - `ChainPlan buildChainPlan(const Preset& preset, const std::filesystem::path& dataRoot)`

- [ ] **Step 1: Add failing chain validation tests**

Append to `tests/preset_smoke.cpp`, before final cleanup/return:

```cpp
  const auto dataRoot = std::filesystem::temp_directory_path() / "ardor-chain-smoke";
  std::filesystem::remove_all(dataRoot);
  std::filesystem::create_directories(dataRoot / "models");
  std::ofstream(dataRoot / "models/ok.nam").put('\n');

  ardor::Preset chainPreset;
  chainPreset.blocks.push_back({"ready", "nam", true, "models/ok.nam", nlohmann::json::object()});
  chainPreset.blocks.push_back({"missing", "cab", true, "irs/missing.wav", nlohmann::json::object()});
  chainPreset.blocks.push_back({"future", "delay", true, "", nlohmann::json::object()});

  const ardor::ChainPlan plan = ardor::buildChainPlan(chainPreset, dataRoot);
  assert(plan.blocks.size() == 3);
  assert(plan.blocks[0].status == ardor::ChainBlockStatus::Ready);
  assert(plan.blocks[1].status == ardor::ChainBlockStatus::MissingAsset);
  assert(plan.blocks[2].status == ardor::ChainBlockStatus::Unsupported);
  assert(plan.runnableBlockCount == 1);

  std::filesystem::remove_all(dataRoot);
```

Also add includes:

```cpp
#include "preset/ChainPlan.h"

#include <fstream>
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```sh
cmake --build build
```

Expected: build fails because `preset/ChainPlan.h` does not exist.

- [ ] **Step 3: Implement chain validation**

Create `src/preset/ChainPlan.h`:

```cpp
#pragma once

#include "Preset.h"

#include <filesystem>
#include <string>
#include <vector>

namespace ardor {

enum class ChainBlockStatus {
  Ready,
  MissingAsset,
  Unsupported,
  Disabled
};

struct ChainBlockPlan {
  std::string id;
  std::string type;
  ChainBlockStatus status = ChainBlockStatus::Ready;
};

struct ChainPlan {
  std::vector<ChainBlockPlan> blocks;
  size_t runnableBlockCount = 0;
};

ChainPlan buildChainPlan(const Preset& preset, const std::filesystem::path& dataRoot);

} // namespace ardor
```

Create `src/preset/ChainPlan.cpp`:

```cpp
#include "ChainPlan.h"

namespace ardor {

namespace {

bool supportedType(const std::string& type)
{
  return type == "nam" || type == "cab";
}

bool needsAsset(const std::string& type)
{
  return type == "nam" || type == "cab";
}

} // namespace

ChainPlan buildChainPlan(const Preset& preset, const std::filesystem::path& dataRoot)
{
  ChainPlan plan;
  for (const auto& block : preset.blocks) {
    ChainBlockPlan blockPlan;
    blockPlan.id = block.id;
    blockPlan.type = block.type;

    if (!block.enabled) {
      blockPlan.status = ChainBlockStatus::Disabled;
    } else if (!supportedType(block.type)) {
      blockPlan.status = ChainBlockStatus::Unsupported;
    } else if (needsAsset(block.type) && !std::filesystem::exists(dataRoot / block.asset)) {
      blockPlan.status = ChainBlockStatus::MissingAsset;
    } else {
      blockPlan.status = ChainBlockStatus::Ready;
      ++plan.runnableBlockCount;
    }

    plan.blocks.push_back(std::move(blockPlan));
  }
  return plan;
}

} // namespace ardor
```

- [ ] **Step 4: Add the source to CMake**

Change the `ardor_preset` target to:

```cmake
add_library(ardor_preset
  src/preset/Preset.cpp
  src/preset/PresetStore.cpp
  src/preset/ChainPlan.cpp
)
```

- [ ] **Step 5: Run the test to verify it passes**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure -R pedal-preset-smoke
```

Expected: `pedal-preset-smoke` passes.

- [ ] **Step 6: Commit**

```sh
git add CMakeLists.txt src/preset/ChainPlan.h src/preset/ChainPlan.cpp tests/preset_smoke.cpp
git commit -m "feat: validate preset chains"
```

### Task 4: Master Volume And Dry Bypass Engine Hooks

**Files:**
- Modify: `src/dsp/PedalEngine.h`
- Modify: `src/dsp/PedalEngine.cpp`
- Create: `tests/engine_contract_smoke.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes:
  - `ardor::PedalEngine`
- Produces:
  - `void PedalEngine::setMasterVolume(float gain)`
  - `void PedalEngine::setEffectsBypassed(bool bypassed)`
  - Master volume applied after chain or dry bypass and before safety limit.

- [ ] **Step 1: Write the failing engine contract test**

Create `tests/engine_contract_smoke.cpp`:

```cpp
#include "dsp/PedalEngine.h"

#include <cassert>
#include <cmath>
#include <vector>

namespace {

bool near(float left, float right)
{
  return std::fabs(left - right) < 0.0001f;
}

} // namespace

int main()
{
  ardor::PedalEngine engine;
  engine.loadIr({1.0f});
  engine.setInputGain(1.0f);
  engine.setOutputGain(1.0f);
  engine.setMasterVolume(0.5f);

  auto [left, right] = engine.process(0.25f);
  assert(near(left, 0.125f));
  assert(near(right, 0.125f));

  engine.setEffectsBypassed(true);
  auto [dryLeft, dryRight] = engine.process(0.25f);
  assert(near(dryLeft, 0.125f));
  assert(near(dryRight, 0.125f));

  engine.setSafetyLimit(0.1f);
  auto [limitedLeft, limitedRight] = engine.process(1.0f);
  assert(near(limitedLeft, 0.1f));
  assert(near(limitedRight, 0.1f));

  std::vector<float> input{0.25f, -0.25f, 1.0f, -1.0f};
  std::vector<float> leftBlock(input.size(), 0.0f);
  std::vector<float> rightBlock(input.size(), 0.0f);
  engine.processBlock(input.data(), leftBlock.data(), rightBlock.data(), input.size());
  assert(near(leftBlock[0], 0.1f));
  assert(near(rightBlock[1], -0.1f));
  return 0;
}
```

- [ ] **Step 2: Register and run the failing test**

Add to `CMakeLists.txt`:

```cmake
add_executable(pedal-engine-contract-smoke tests/engine_contract_smoke.cpp)
target_link_libraries(pedal-engine-contract-smoke PRIVATE ardor_dsp)
```

Add the test after `enable_testing()`:

```cmake
add_test(NAME pedal-engine-contract-smoke COMMAND pedal-engine-contract-smoke)
```

Run:

```sh
cmake --build build
```

Expected: build fails because `PedalEngine::setMasterVolume` and `PedalEngine::setEffectsBypassed` do not exist.

- [ ] **Step 3: Implement the minimal engine hooks**

In `src/dsp/PedalEngine.h`, add public methods:

```cpp
  void setMasterVolume(float gain);
  void setEffectsBypassed(bool bypassed);
```

Add private fields:

```cpp
  float masterVolume_ = 1.0f;
  bool effectsBypassed_ = false;
```

In `src/dsp/PedalEngine.cpp`, add:

```cpp
void PedalEngine::setMasterVolume(float gain)
{
  masterVolume_ = std::max(0.0f, gain);
}

void PedalEngine::setEffectsBypassed(bool bypassed)
{
  effectsBypassed_ = bypassed;
}
```

Change `process` so the dry bypass path and normal chain both pass through master volume and safety:

```cpp
std::pair<float, float> PedalEngine::process(float input)
{
  if (effectsBypassed_) {
    const float dry = applySafety(input * masterVolume_);
    return {dry, dry};
  }

  const float afterGain = input * inputGain_;
  const float afterNam = nam_.process(afterGain);
  const float wet = applySafety(ir_.processSample(afterNam) * outputGain_ * masterVolume_);
  return {wet, wet};
}
```

Change the final loop in `processBlock`:

```cpp
  if (effectsBypassed_) {
    for (size_t i = 0; i < frames; ++i) {
      const float dry = applySafety(input[i] * masterVolume_);
      left[i] = dry;
      right[i] = dry;
    }
    return;
  }
```

and:

```cpp
    const float wet = applySafety(irBlock_[i] * outputGain_ * masterVolume_);
```

- [ ] **Step 4: Run the test to verify it passes**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure -R pedal-engine-contract-smoke
```

Expected: `pedal-engine-contract-smoke` passes.

- [ ] **Step 5: Run all tests**

Run:

```sh
ctest --test-dir build --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 6: Commit**

```sh
git add CMakeLists.txt src/dsp/PedalEngine.h src/dsp/PedalEngine.cpp tests/engine_contract_smoke.cpp
git commit -m "feat: add engine bypass volume hooks"
```

### Task 5: Overload Bypass Latch State

**Files:**
- Create: `src/preset/RuntimeState.h`
- Create: `src/preset/RuntimeState.cpp`
- Modify: `tests/preset_smoke.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  - `class RuntimeState`
  - `void reportOverload()`
  - `void clearEffectsBypass()`
  - `void changePreset()`
  - `bool effectsBypassed() const`

- [ ] **Step 1: Add failing runtime latch tests**

Append to `tests/preset_smoke.cpp`, before `return 0;`:

```cpp
  ardor::RuntimeState runtime;
  assert(!runtime.effectsBypassed());
  runtime.reportOverload();
  assert(runtime.effectsBypassed());
  runtime.reportStableCallback();
  assert(runtime.effectsBypassed());
  runtime.clearEffectsBypass();
  assert(!runtime.effectsBypassed());
  runtime.reportOverload();
  assert(runtime.effectsBypassed());
  runtime.changePreset();
  assert(!runtime.effectsBypassed());
```

Add include:

```cpp
#include "preset/RuntimeState.h"
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```sh
cmake --build build
```

Expected: build fails because `preset/RuntimeState.h` does not exist.

- [ ] **Step 3: Implement the latch**

Create `src/preset/RuntimeState.h`:

```cpp
#pragma once

namespace ardor {

class RuntimeState {
public:
  void reportOverload();
  void reportStableCallback();
  void clearEffectsBypass();
  void changePreset();
  bool effectsBypassed() const;

private:
  bool effectsBypassed_ = false;
};

} // namespace ardor
```

Create `src/preset/RuntimeState.cpp`:

```cpp
#include "RuntimeState.h"

namespace ardor {

void RuntimeState::reportOverload()
{
  effectsBypassed_ = true;
}

void RuntimeState::reportStableCallback()
{
}

void RuntimeState::clearEffectsBypass()
{
  effectsBypassed_ = false;
}

void RuntimeState::changePreset()
{
  effectsBypassed_ = false;
}

bool RuntimeState::effectsBypassed() const
{
  return effectsBypassed_;
}

} // namespace ardor
```

- [ ] **Step 4: Add the source to CMake**

Change the `ardor_preset` target to:

```cmake
add_library(ardor_preset
  src/preset/Preset.cpp
  src/preset/PresetStore.cpp
  src/preset/ChainPlan.cpp
  src/preset/RuntimeState.cpp
)
```

- [ ] **Step 5: Run the test to verify it passes**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure -R pedal-preset-smoke
```

Expected: `pedal-preset-smoke` passes.

- [ ] **Step 6: Commit**

```sh
git add CMakeLists.txt src/preset/RuntimeState.h src/preset/RuntimeState.cpp tests/preset_smoke.cpp
git commit -m "feat: add effects bypass latch"
```

### Task 6: Static HTML Mockup

**Files:**
- Create: `mockups/preset-ui/index.html`
- Modify: `README.md`

**Interfaces:**
- Consumes:
  - Preset JSON shape from `docs/superpowers/specs/2026-07-07-preset-ui-architecture-design.md`
- Produces:
  - A static browser-openable mockup showing preset mode, edit mode, block drawer, parameter drawer, dirty state, master volume, and overload bypass state.

- [ ] **Step 1: Create the static mockup**

Create `mockups/preset-ui/index.html` as a single file with inline CSS and JS. Use this content as the starting implementation:

```html
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Ardor Pedal Preset UI Mockup</title>
  <style>
    :root {
      color-scheme: dark;
      font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      background: #111;
      color: #f4f4f4;
    }
    body { margin: 0; background: #111; }
    .screen { width: min(100vw, 900px); margin: 0 auto; min-height: 100vh; display: grid; grid-template-rows: auto 1fr auto; }
    header, footer { display: flex; align-items: center; justify-content: space-between; gap: 12px; padding: 12px 16px; background: #1b1b1b; }
    button { border: 1px solid #666; background: #2b2b2b; color: #fff; padding: 8px 12px; border-radius: 6px; }
    button.active { border-color: #6ee7b7; color: #6ee7b7; }
    main { display: grid; grid-template-columns: 190px 1fr 230px; min-height: 420px; }
    aside { background: #181818; border-right: 1px solid #333; padding: 12px; }
    .params { border-left: 1px solid #333; border-right: 0; }
    .chain { padding: 18px; display: grid; grid-template-rows: 1fr 1fr; gap: 16px; }
    .lane { display: flex; align-items: center; gap: 10px; border: 1px solid #333; padding: 12px; min-height: 96px; }
    .block, .io { min-width: 82px; padding: 12px; border: 1px solid #555; border-radius: 6px; text-align: center; background: #242424; }
    .block.selected { border-color: #60a5fa; }
    .warn { color: #fca5a5; }
    .drawer-item { width: 100%; margin-bottom: 8px; text-align: left; }
    .presets { display: grid; grid-template-columns: repeat(4, 1fr); gap: 8px; }
    .status { display: flex; gap: 14px; font-size: 14px; }
    input[type="range"] { width: 100%; }
  </style>
</head>
<body>
  <div class="screen">
    <header>
      <strong id="mode-title">Edit Mode</strong>
      <div class="status">
        <span>Bank 000</span>
        <span id="dirty">DIRTY</span>
        <span>CPU 38%</span>
        <span>Master <span id="master-volume">82</span>%</span>
        <span id="overload" class="warn"></span>
      </div>
    </header>
    <main>
      <aside>
        <h3>Blocks</h3>
        <button class="drawer-item" data-add="nam">Neural Amp</button>
        <button class="drawer-item" data-add="cab">Cab</button>
      </aside>
      <section class="chain">
        <div class="lane" id="lane-top"></div>
        <div class="lane" id="lane-bottom"></div>
      </section>
      <aside class="params">
        <h3 id="param-title">Block</h3>
        <label>Level <input type="range" min="-24" max="12" value="0"></label>
        <p id="param-asset"></p>
        <button id="save">Save</button>
        <button id="discard">Discard</button>
        <button id="bypass">Clear Bypass</button>
      </aside>
    </main>
    <footer class="presets">
      <button class="active">Clean Lead</button>
      <button>Crunch</button>
      <button>Ambient</button>
      <button>Solo</button>
    </footer>
  </div>
  <script>
    const preset = {
      name: "Clean Lead",
      blocks: [
        { id: "block-1", type: "nam", enabled: true, asset: "models/clean.nam", params: { levelDb: 0 } },
        { id: "block-2", type: "cab", enabled: true, asset: "irs/open-back.wav", params: { mix: 1, levelDb: 0 } }
      ]
    };
    let selected = preset.blocks[0];
    let bypassed = false;

    function render() {
      const top = document.getElementById("lane-top");
      const bottom = document.getElementById("lane-bottom");
      top.innerHTML = '<div class="io">Input</div>';
      bottom.innerHTML = '';
      preset.blocks.forEach((block, index) => {
        const el = document.createElement("button");
        el.className = "block" + (block === selected ? " selected" : "");
        el.textContent = block.type.toUpperCase();
        el.onclick = () => { selected = block; render(); };
        (index < 3 ? top : bottom).appendChild(el);
      });
      bottom.insertAdjacentHTML("beforeend", '<div class="io">Output</div>');
      document.getElementById("param-title").textContent = selected.type.toUpperCase();
      document.getElementById("param-asset").textContent = selected.asset;
      document.getElementById("overload").textContent = bypassed ? "OVERLOAD - EFFECTS BYPASSED" : "";
    }

    document.querySelectorAll("[data-add]").forEach(button => {
      button.onclick = () => {
        const type = button.dataset.add;
        preset.blocks.push({ id: crypto.randomUUID(), type, enabled: true, asset: type === "nam" ? "models/new.nam" : "irs/new.wav", params: {} });
        selected = preset.blocks[preset.blocks.length - 1];
        render();
      };
    });
    document.getElementById("save").onclick = () => document.getElementById("dirty").textContent = "SAVED";
    document.getElementById("discard").onclick = () => document.getElementById("dirty").textContent = "SAVED";
    document.getElementById("bypass").onclick = () => { bypassed = false; render(); };
    render();
  </script>
</body>
</html>
```

- [ ] **Step 2: Update README**

Add a short section:

````markdown
## UI Mockup

The first UI mockup is static HTML:

```sh
open mockups/preset-ui/index.html
```

It uses the same preset shape as `docs/superpowers/specs/2026-07-07-preset-ui-architecture-design.md`.
````

- [ ] **Step 3: Verify the mockup file exists and is readable**

Run:

```sh
test -f mockups/preset-ui/index.html
```

Expected: command exits with status `0`.

- [ ] **Step 4: Commit**

```sh
git add README.md mockups/preset-ui/index.html
git commit -m "docs: add preset ui mockup"
```

### Task 7: Final Verification

**Files:**
- No new files.

**Interfaces:**
- Consumes all prior tasks.
- Produces a branch ready for review.

- [ ] **Step 1: Run full build and tests**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 2: Check git status**

Run:

```sh
git status --short --branch
```

Expected: clean working tree on the feature branch.

- [ ] **Step 3: Summarize results**

Report:

```text
Implemented preset model/store, chain validation, engine master-volume and bypass hooks, overload latch, and static UI mockup.
Tests: cmake --build build; ctest --test-dir build --output-on-failure
```

## Self-Review Notes

- Spec coverage: preset JSON, 100x4 slots, serial ordered blocks, relative asset paths, manual save/discard, live working preset, master volume, missing/unsupported blocks, overload latch, and HTML mockup all have implementation tasks.
- Scope check: LVGL rendering, touch tuning, split routing, stereo dual chains, asset catalog, and full DSP effects stay out of this plan.
- Type consistency: `Preset`, `PresetStore`, `PresetSession`, `ChainPlan`, `RuntimeState`, and `PedalEngine` method names are defined before use.
