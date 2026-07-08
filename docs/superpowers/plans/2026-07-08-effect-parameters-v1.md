# Effect Parameters V1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the UI edit and save real preset parameters, starting with global input/output/safety settings and minimal cab params.

**Architecture:** Keep params in the existing preset JSON shape. Add only the fields the current UI and engine can use: preset globals on `UiPreset`, arbitrary JSON params on `UiBlock`, and first-cab `levelDb`/`mix` support in the existing engine path.

**Tech Stack:** C++20, LVGL v9, nlohmann/json, existing `Preset`, `UiModel`, `ChainPlan`, `PedalEngine`, and smoke-test binaries.

## Global Constraints

- Store editable params per block.
- Start with globals: input gain, output gain, safety limit.
- Cab V1 supports `levelDb` and `mix` for the first ready cab block.
- Skip modulation, delay, and reverb params until the save/load path is solid.
- Keep one serial chain for v1.
- Do not add dependencies.

---

## File Structure

- `src/ui/UiModel.h`: add global preset fields, block params, and small setter functions.
- `src/ui/UiModel.cpp`: round-trip globals/block params and clamp edited values.
- `src/ui/LvglUi.cpp`: add simple edit controls for globals and cab params in the existing bottom drawer.
- `src/preset/ChainPlan.h`: add cab level/mix fields to `ChainBlockPlan`.
- `src/preset/ChainPlan.cpp`: parse cab params from preset JSON.
- `src/dsp/PedalEngine.h`: add cab level/mix setters.
- `src/dsp/PedalEngine.cpp`: mix cab wet/dry inside the existing chain.
- `src/audio/EngineLoader.cpp`: apply cab level/mix from the first ready cab block.
- `tests/ui_model_smoke.cpp`: verify UI params round-trip and dirty state.
- `tests/engine_contract_smoke.cpp`: verify cab level/mix changes output.
- `README.md`: document the supported V1 params.

---

### Task 1: Round-Trip UI Globals And Block Params

**Files:**
- Modify: `src/ui/UiModel.h`
- Modify: `src/ui/UiModel.cpp`
- Modify: `tests/ui_model_smoke.cpp`

**Interfaces:**
- Consumes:
  - `ardor::PresetGlobal`
  - `ardor::PresetBlock::params`
  - `ardor::activePresetToPreset(const UiState&)`
  - `ardor::replaceActivePreset(UiState&, const Preset&)`
- Produces:
  - `UiPreset::global`
  - `UiBlock::params`
  - `void setActiveInputGainDb(UiState& state, float db)`
  - `void setActiveOutputGainDb(UiState& state, float db)`
  - `void setActiveSafetyLimitDb(UiState& state, float db)`
  - `void setSelectedBlockParam(UiState& state, const std::string& key, float value)`

- [ ] **Step 1: Write the failing test**

Add this block in `tests/ui_model_smoke.cpp` after the existing `replaceActivePreset` assertions:

```cpp
  replacement.global.inputGainDb = -6.0f;
  replacement.global.outputGainDb = -3.0f;
  replacement.global.safetyLimitDb = -1.5f;
  replacement.blocks[0].params = {{"levelDb", -4.0f}, {"mix", 0.75f}};
  ardor::replaceActivePreset(state, replacement);
  if (require(state.bank.presets[state.activePreset].global.inputGainDb == -6.0f, "input global should load")) return 1;
  if (require(state.bank.presets[state.activePreset].global.outputGainDb == -3.0f, "output global should load")) return 1;
  if (require(state.bank.presets[state.activePreset].global.safetyLimitDb == -1.5f, "safety global should load")) return 1;
  if (require(state.bank.presets[state.activePreset].blocks[0].params.value("levelDb", 0.0f) == -4.0f,
              "block params should load")) return 1;
  ardor::selectGlobalParams(state);
  if (require(state.paramTarget == ardor::UiParamTarget::Globals, "global param drawer target")) return 1;
  if (require(state.paramDrawerOpen, "global params should open drawer")) return 1;

  ardor::setActiveInputGainDb(state, 20.0f);
  ardor::setActiveOutputGainDb(state, -80.0f);
  ardor::setActiveSafetyLimitDb(state, 6.0f);
  if (require(state.bank.presets[state.activePreset].global.inputGainDb == 12.0f, "input gain should clamp high")) return 1;
  if (require(state.bank.presets[state.activePreset].global.outputGainDb == -60.0f, "output gain should clamp low")) return 1;
  if (require(state.bank.presets[state.activePreset].global.safetyLimitDb == 0.0f, "safety limit should clamp high")) return 1;
  if (require(state.dirty, "global edit should dirty preset")) return 1;

  state.dirty = false;
  ardor::setSelectedBlockParam(state, "mix", 0.25f);
  const auto editedPreset = ardor::activePresetToPreset(state);
  if (require(editedPreset.global.inputGainDb == 12.0f, "saved input global should round-trip")) return 1;
  if (require(editedPreset.blocks[0].params.value("mix", 0.0f) == 0.25f, "saved block params should round-trip")) return 1;
  if (require(state.dirty, "block param edit should dirty preset")) return 1;
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
cmake --build build --target pedal-ui-model-smoke && ctest --test-dir build -R pedal-ui-model-smoke --output-on-failure
```

Expected: compile fails because `UiPreset::global`, `UiBlock::params`, and the setter functions do not exist.

- [ ] **Step 3: Add model fields and declarations**

In `src/ui/UiModel.h`, add `params` to `UiBlock`:

```cpp
nlohmann::json params = nlohmann::json::object();
```

Add `global` to `UiPreset`:

```cpp
PresetGlobal global;
```

Add the drawer target enum near `UiMode`:

```cpp
enum class UiParamTarget {
  Block,
  Globals
};
```

Add to `UiState`:

```cpp
UiParamTarget paramTarget = UiParamTarget::Block;
```

Add declarations:

```cpp
void selectGlobalParams(UiState& state);
void setActiveInputGainDb(UiState& state, float db);
void setActiveOutputGainDb(UiState& state, float db);
void setActiveSafetyLimitDb(UiState& state, float db);
void setSelectedBlockParam(UiState& state, const std::string& key, float value);
```

- [ ] **Step 4: Implement round-trip and setters**

In `src/ui/UiModel.cpp`, add this helper in the anonymous namespace:

```cpp
float clampFloat(float value, float low, float high)
{
  return std::clamp(value, low, high);
}
```

In `activePresetToPreset`, add:

```cpp
  preset.global = uiPreset.global;
```

Change the block push to keep params:

```cpp
    preset.blocks.push_back({block.id, block.type, block.enabled, block.assetPath,
                             block.params.is_null() ? nlohmann::json::object() : block.params});
```

In `replaceActivePreset`, set:

```cpp
  uiPreset.global = preset.global;
```

And add params to the pushed UI block:

```cpp
                               block.enabled,
                               block.params.is_null() ? nlohmann::json::object() : block.params});
```

Add setter implementations:

```cpp
void selectGlobalParams(UiState& state)
{
  state.paramTarget = UiParamTarget::Globals;
  state.paramDrawerOpen = true;
}

void setActiveInputGainDb(UiState& state, float db)
{
  state.bank.presets[state.activePreset].global.inputGainDb = clampFloat(db, -60.0f, 12.0f);
  state.dirty = true;
}

void setActiveOutputGainDb(UiState& state, float db)
{
  state.bank.presets[state.activePreset].global.outputGainDb = clampFloat(db, -60.0f, 12.0f);
  state.dirty = true;
}

void setActiveSafetyLimitDb(UiState& state, float db)
{
  state.bank.presets[state.activePreset].global.safetyLimitDb = clampFloat(db, -24.0f, 0.0f);
  state.dirty = true;
}

void setSelectedBlockParam(UiState& state, const std::string& key, float value)
{
  auto& blocks = state.bank.presets[state.activePreset].blocks;
  if (state.selectedBlock >= blocks.size()) {
    return;
  }
  blocks[state.selectedBlock].params[key] = value;
  state.dirty = true;
}
```

In `selectBlock`, set the target before opening the drawer:

```cpp
  state.paramTarget = UiParamTarget::Block;
```

- [ ] **Step 5: Run the UI model smoke**

Run:

```bash
cmake --build build --target pedal-ui-model-smoke && ctest --test-dir build -R pedal-ui-model-smoke --output-on-failure
```

Expected: `pedal-ui-model-smoke` passes.

- [ ] **Step 6: Commit**

```bash
git add src/ui/UiModel.h src/ui/UiModel.cpp tests/ui_model_smoke.cpp
git commit -m "feat: round trip ui effect params"
```

---

### Task 2: Add LVGL Global Param Drawer

**Files:**
- Modify: `src/ui/LvglUi.cpp`

**Interfaces:**
- Consumes:
  - `setActiveInputGainDb(UiState&, float)`
  - `setActiveOutputGainDb(UiState&, float)`
  - `setActiveSafetyLimitDb(UiState&, float)`
- Produces:
  - A Global button that opens the bottom drawer for input gain, output gain, and safety limit.

- [ ] **Step 1: Add parameter-step callbacks**

In `src/ui/LvglUi.cpp`, add helpers near the other event handlers:

```cpp
void onGlobalParamsClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  selectGlobalParams(*context->state);
  redraw(context);
}

enum class GlobalParam {
  Input,
  Output,
  Safety
};

void stepGlobalParam(UiState& state, GlobalParam param, float delta)
{
  auto& global = state.bank.presets[state.activePreset].global;
  if (param == GlobalParam::Input) {
    setActiveInputGainDb(state, global.inputGainDb + delta);
  } else if (param == GlobalParam::Output) {
    setActiveOutputGainDb(state, global.outputGainDb + delta);
  } else {
    setActiveSafetyLimitDb(state, global.safetyLimitDb + delta);
  }
}

void onInputGainDown(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  stepGlobalParam(*context->state, GlobalParam::Input, -1.0f);
  redraw(context);
}

void onInputGainUp(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  stepGlobalParam(*context->state, GlobalParam::Input, 1.0f);
  redraw(context);
}

void onOutputGainDown(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  stepGlobalParam(*context->state, GlobalParam::Output, -1.0f);
  redraw(context);
}

void onOutputGainUp(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  stepGlobalParam(*context->state, GlobalParam::Output, 1.0f);
  redraw(context);
}

void onSafetyLimitDown(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  stepGlobalParam(*context->state, GlobalParam::Safety, -1.0f);
  redraw(context);
}

void onSafetyLimitUp(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  stepGlobalParam(*context->state, GlobalParam::Safety, 1.0f);
  redraw(context);
}
```

- [ ] **Step 2: Add a Global button**

In `renderEditMode`, between the Presets and Blocks buttons, add:

```cpp
  lv_obj_t* globalButton = button(root, "Global");
  lv_obj_set_size(globalButton, 112, 44);
  lv_obj_align(globalButton, LV_ALIGN_TOP_RIGHT, -140, 14);
  lv_obj_add_event_cb(globalButton, onGlobalParamsClicked, LV_EVENT_CLICKED, remember(state));
```

- [ ] **Step 3: Render global controls in the bottom drawer**

Add this helper:

```cpp
void globalControl(lv_obj_t* parent, UiState& state, const std::string& name, float value, int x,
                   lv_event_cb_t down, lv_event_cb_t up, UiEventContext* context)
{
  label(parent, name + " " + std::to_string(static_cast<int>(value)) + " dB",
        LV_ALIGN_TOP_LEFT, x, 0, &lv_font_montserrat_18, muted);
  lv_obj_t* minus = button(parent, "-");
  lv_obj_set_size(minus, 36, 32);
  lv_obj_align(minus, LV_ALIGN_TOP_LEFT, x, 28);
  lv_obj_add_event_cb(minus, down, LV_EVENT_CLICKED, context);

  lv_obj_t* plus = button(parent, "+");
  lv_obj_set_size(plus, 36, 32);
  lv_obj_align(plus, LV_ALIGN_TOP_LEFT, x + 42, 28);
  lv_obj_add_event_cb(plus, up, LV_EVENT_CLICKED, context);
}
```

Restructure the start of `renderParamDrawer` so the drawer is created before checking the selected block:

```cpp
void LvglUi::renderParamDrawer(lv_obj_t* root, UiState& state)
{
  lv_obj_t* drawer = lv_obj_create(root);
  lv_obj_set_size(drawer, 800, 142);
  lv_obj_align(drawer, LV_ALIGN_BOTTOM_MID, 0, 0);
  stylePanel(drawer, panelAlt);
  lv_obj_set_style_radius(drawer, 0, 0);
  lv_obj_set_style_border_side(drawer, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_pad_all(drawer, 16, 0);

  if (state.paramTarget == UiParamTarget::Globals) {
    const auto& global = state.bank.presets[state.activePreset].global;
    label(drawer, "Global", LV_ALIGN_TOP_LEFT, 0, 0, &lv_font_montserrat_22);
    auto* context = remember(state);
    globalControl(drawer, state, "In", global.inputGainDb, 0, onInputGainDown, onInputGainUp, context);
    globalControl(drawer, state, "Out", global.outputGainDb, 150, onOutputGainDown, onOutputGainUp, context);
    globalControl(drawer, state, "Limit", global.safetyLimitDb, 300, onSafetyLimitDown, onSafetyLimitUp, context);
    lv_obj_t* close = button(drawer, "X");
    lv_obj_set_size(close, 42, 36);
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, 0, -4);
    lv_obj_add_event_cb(close, onCloseParamDrawer, LV_EVENT_CLICKED, remember(state));
    return;
  }

  const auto& blocks = state.bank.presets[state.activePreset].blocks;
  if (state.selectedBlock >= blocks.size()) {
    lv_obj_delete(drawer);
    return;
  }

  const auto& block = blocks[state.selectedBlock];
```

Continue with the existing block drawer body after `const auto& block = blocks[state.selectedBlock];`. Remove the old duplicate drawer creation lines from the block path.

- [ ] **Step 4: Build and smoke the simulator**

Run:

```bash
cmake --build build --target pedal-ui-sim pedal-ui-model-smoke
ctest --test-dir build -R pedal-ui-model-smoke --output-on-failure
SDL_VIDEODRIVER=dummy ./build/pedal-ui-sim
```

Expected: model smoke passes and the simulator stays running until interrupted.

- [ ] **Step 5: Commit**

```bash
git add src/ui/LvglUi.cpp
git commit -m "feat: edit global preset params"
```

---

### Task 3: Apply Cab Level And Mix

**Files:**
- Modify: `src/preset/ChainPlan.h`
- Modify: `src/preset/ChainPlan.cpp`
- Modify: `src/dsp/PedalEngine.h`
- Modify: `src/dsp/PedalEngine.cpp`
- Modify: `src/audio/EngineLoader.cpp`
- Modify: `tests/engine_contract_smoke.cpp`

**Interfaces:**
- Consumes:
  - Cab block params: `levelDb` and `mix`
  - `dbToGain(float)`
- Produces:
  - `ChainBlockPlan::level`
  - `ChainBlockPlan::mix`
  - `PedalEngine::setCabLevel(float gain)`
  - `PedalEngine::setCabMix(float mix)`

- [ ] **Step 1: Write the failing engine test**

Add to `tests/engine_contract_smoke.cpp` after the existing simple gain checks:

```cpp
    ardor::PedalEngine cabMixEngine;
    cabMixEngine.loadIr({1.0f});
    cabMixEngine.setSafetyLimiterEnabled(false);
    cabMixEngine.setCabLevel(0.5f);
    cabMixEngine.setCabMix(0.5f);
    const auto mixed = cabMixEngine.process(1.0f);
    if (require(std::fabs(mixed.first - 0.75f) < 0.0001f, "cab mix should blend dry and wet")) return 1;
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
cmake --build build --target pedal-engine-contract-smoke && ctest --test-dir build -R pedal-engine-contract-smoke --output-on-failure
```

Expected: compile fails because `setCabLevel` and `setCabMix` do not exist.

- [ ] **Step 3: Add engine setters**

In `src/dsp/PedalEngine.h`, add public methods:

```cpp
void setCabLevel(float gain);
void setCabMix(float mix);
```

Add private atomics:

```cpp
std::atomic<float> cabLevel_{1.0f};
std::atomic<float> cabMix_{1.0f};
```

In `src/dsp/PedalEngine.cpp`, add:

```cpp
void PedalEngine::setCabLevel(float gain)
{
  cabLevel_.store(std::max(0.0f, gain), std::memory_order_relaxed);
}

void PedalEngine::setCabMix(float mix)
{
  cabMix_.store(std::clamp(mix, 0.0f, 1.0f), std::memory_order_relaxed);
}
```

In `process`, replace the wet output calculation with:

```cpp
  const float cabLevel = cabLevel_.load(std::memory_order_relaxed);
  const float cabMix = cabMix_.load(std::memory_order_relaxed);
  const float cabWet = ir_.processSample(afterNam) * cabLevel;
  const float wet = applySafety(((cabWet * cabMix) + (afterNam * (1.0f - cabMix)))
                                * outputGain_.load(std::memory_order_relaxed) * masterVolume);
```

In `processBlock`, use the same formula inside the final loop:

```cpp
  const float cabLevel = cabLevel_.load(std::memory_order_relaxed);
  const float cabMix = cabMix_.load(std::memory_order_relaxed);
  for (size_t i = 0; i < frames; ++i) {
    const float cabWet = irBlock_[i] * cabLevel;
    const float wet = applySafety(((cabWet * cabMix) + (namBlock_[i] * (1.0f - cabMix)))
                                  * outputGain * masterVolume);
    left[i] = wet;
    right[i] = wet;
  }
```

- [ ] **Step 4: Add plan params**

In `src/preset/ChainPlan.h`, add to `ChainBlockPlan`:

```cpp
float level = 1.0f;
float mix = 1.0f;
```

In `src/preset/ChainPlan.cpp`, after assigning `blockPlan.params`, add:

```cpp
    if (block.type == "cab") {
      blockPlan.level = dbToGain(blockPlan.params.value("levelDb", 0.0f));
      blockPlan.mix = std::clamp(blockPlan.params.value("mix", 1.0f), 0.0f, 1.0f);
    }
```

- [ ] **Step 5: Apply cab params while loading**

In `src/audio/EngineLoader.cpp`, after `engine.loadIr(std::move(wav.samples));`, add:

```cpp
      engine.setCabLevel(block.level);
      engine.setCabMix(block.mix);
```

- [ ] **Step 6: Run tests**

Run:

```bash
cmake --build build --target pedal-engine-contract-smoke pedal-preset-smoke pedal-preset-cli-smoke
ctest --test-dir build -R "pedal-engine-contract-smoke|pedal-preset-smoke|pedal-preset-cli-smoke" --output-on-failure
```

Expected: selected tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/preset/ChainPlan.h src/preset/ChainPlan.cpp src/dsp/PedalEngine.h src/dsp/PedalEngine.cpp src/audio/EngineLoader.cpp tests/engine_contract_smoke.cpp
git commit -m "feat: apply cab level and mix params"
```

---

### Task 4: Edit Cab Params In The Drawer

**Files:**
- Modify: `src/ui/LvglUi.cpp`
- Modify: `tests/ui_model_smoke.cpp`

**Interfaces:**
- Consumes:
  - `setSelectedBlockParam(UiState&, const std::string&, float)`
  - `UiBlock::params`
- Produces:
  - Cab drawer controls for `levelDb` and `mix`.

- [ ] **Step 1: Add model assertions for cab param edits**

Add to `tests/ui_model_smoke.cpp` after the `setSelectedBlockParam` assertion:

```cpp
  ardor::setSelectedBlockParam(state, "levelDb", -2.0f);
  ardor::setSelectedBlockParam(state, "mix", 0.5f);
  const auto cabParamPreset = ardor::activePresetToPreset(state);
  if (require(cabParamPreset.blocks[0].params.value("levelDb", 0.0f) == -2.0f, "cab level should save")) return 1;
  if (require(cabParamPreset.blocks[0].params.value("mix", 0.0f) == 0.5f, "cab mix should save")) return 1;
```

- [ ] **Step 2: Add cab drawer callbacks**

In `src/ui/LvglUi.cpp`, add:

```cpp
float selectedParamValue(const UiState& state, const std::string& key, float fallback)
{
  const auto& block = state.bank.presets[state.activePreset].blocks[state.selectedBlock];
  return block.params.value(key, fallback);
}

void stepSelectedParam(UiState& state, const std::string& key, float delta, float low, float high, float fallback)
{
  setSelectedBlockParam(state, key, std::clamp(selectedParamValue(state, key, fallback) + delta, low, high));
}

void onCabLevelDown(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  stepSelectedParam(*context->state, "levelDb", -1.0f, -60.0f, 12.0f, 0.0f);
  redraw(context);
}

void onCabLevelUp(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  stepSelectedParam(*context->state, "levelDb", 1.0f, -60.0f, 12.0f, 0.0f);
  redraw(context);
}

void onCabMixDown(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  stepSelectedParam(*context->state, "mix", -0.05f, 0.0f, 1.0f, 1.0f);
  redraw(context);
}

void onCabMixUp(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  stepSelectedParam(*context->state, "mix", 0.05f, 0.0f, 1.0f, 1.0f);
  redraw(context);
}
```

- [ ] **Step 3: Render cab controls only for cab blocks**

In `renderParamDrawer`, after the `Enabled` labels, add:

```cpp
  if (block.type == "cab") {
    const float levelDb = block.params.value("levelDb", 0.0f);
    const float mix = block.params.value("mix", 1.0f);
    auto* context = remember(state);
    globalControl(drawer, state, "Level", levelDb, 190, onCabLevelDown, onCabLevelUp, context);
    label(drawer, "Mix " + std::to_string(static_cast<int>(mix * 100.0f)) + "%",
          LV_ALIGN_BOTTOM_LEFT, 330, -8, &lv_font_montserrat_18, muted);
    lv_obj_t* mixMinus = button(drawer, "-");
    lv_obj_set_size(mixMinus, 36, 32);
    lv_obj_align(mixMinus, LV_ALIGN_BOTTOM_LEFT, 430, -4);
    lv_obj_add_event_cb(mixMinus, onCabMixDown, LV_EVENT_CLICKED, context);
    lv_obj_t* mixPlus = button(drawer, "+");
    lv_obj_set_size(mixPlus, 36, 32);
    lv_obj_align(mixPlus, LV_ALIGN_BOTTOM_LEFT, 472, -4);
    lv_obj_add_event_cb(mixPlus, onCabMixUp, LV_EVENT_CLICKED, context);
  }
```

- [ ] **Step 4: Build and smoke**

Run:

```bash
cmake --build build --target pedal-ui-sim pedal-ui-model-smoke pedal-engine-contract-smoke
ctest --test-dir build -R "pedal-ui-model-smoke|pedal-engine-contract-smoke" --output-on-failure
SDL_VIDEODRIVER=dummy ./build/pedal-ui-sim
```

Expected: tests pass and simulator starts.

- [ ] **Step 5: Commit**

```bash
git add src/ui/LvglUi.cpp tests/ui_model_smoke.cpp
git commit -m "feat: edit cab params in ui"
```

---

### Task 5: Document V1 Params

**Files:**
- Modify: `README.md`

**Interfaces:**
- Consumes:
  - Preset globals: `inputGainDb`, `outputGainDb`, `safetyLimitDb`
  - Cab params: `levelDb`, `mix`
- Produces:
  - Developer-facing preset parameter docs.

- [ ] **Step 1: Add README section**

Add under `## Preset Storage`:

````markdown
### Supported V1 Parameters

Preset globals:

- `global.inputGainDb`: input gain before NAM.
- `global.outputGainDb`: output gain after cab.
- `global.safetyLimitDb`: limiter ceiling, where `-1.0` is the default.

Cab block params:

- `params.levelDb`: cab level before output gain.
- `params.mix`: `0.0` dry after-NAM signal, `1.0` full cab signal.

Other block params may be stored in JSON, but modulation, delay, and reverb are not processed yet.
````

- [ ] **Step 2: Verify**

Run:

```bash
cmake --build build --target pedal-ui-model-smoke pedal-engine-contract-smoke && ctest --test-dir build -R "pedal-ui-model-smoke|pedal-engine-contract-smoke" --output-on-failure
```

Expected: selected tests pass.

- [ ] **Step 3: Commit**

```bash
git add README.md
git commit -m "docs: document effect params v1"
```

---

## Skipped For This Phase

- NAM-specific model params; NAM files are static models in v1.
- Modulation, delay, and reverb DSP.
- Arbitrary typed parameter schemas; JSON storage plus two cab params is enough for this phase.
