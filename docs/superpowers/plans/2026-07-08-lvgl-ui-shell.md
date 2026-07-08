# LVGL UI Shell Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a desktop-testable LVGL UI shell that matches the approved HTML mockup for preset mode and edit mode without wiring hardware controls or realtime audio.

**Architecture:** Add a small `ardor_ui` module that renders LVGL screens from simple preset/view-model data. Add a `pedal-ui-sim` executable that opens an LVGL SDL window at the target display size so the UI can be tested on macOS before the Raspberry Pi arrives. Keep audio and UI separate in this phase.

**Tech Stack:** C++20, CMake, LVGL 9.x with built-in SDL support for desktop simulation, existing `ardor_preset` model/store code, existing assert-style C++ smoke tests.

## Global Constraints

- The first UI-capable pedal version has one 5 inch touch display, one encoder, and four footswitches.
- Preset mode shows bank name and four large preset blocks in a 2x2 grid.
- Active preset has a distinctive color.
- Edit mode shows one serial chain wrapped over two rows.
- The block drawer floats over the chain and can be closed.
- The block drawer groups assets into categories and has quick filter buttons so the player can show one category at a time.
- The parameter drawer floats over the chain at the bottom, can be closed, and is hidden when no block is selected.
- Assets have a human-readable name for UI lists, separate from the file path.
- The encoder controls system master output volume and must not dirty the active preset.
- Routing is serial only in this phase.
- No footswitch, encoder GPIO, Codec Zero, or realtime audio wiring in this UI shell phase.
- No LVGL production DRM/fbdev target in this phase; desktop SDL simulation comes first.

---

## File Structure

- Create `src/ui/UiModel.h`: small UI-facing structs for banks, presets, blocks, assets, drawers, and active state.
- Create `src/ui/UiModel.cpp`: helper functions for sample UI data and dirty-state helpers.
- Create `src/ui/LvglUi.h`: LVGL UI shell public interface.
- Create `src/ui/LvglUi.cpp`: creates preset mode, edit mode, floating block drawer, and floating parameter drawer.
- Create `apps/pedal-ui-sim/main.cpp`: desktop LVGL SDL simulator entry point.
- Create `tests/ui_model_smoke.cpp`: non-LVGL checks for UI model and state transitions.
- Modify `CMakeLists.txt`: add LVGL FetchContent option, `ardor_ui`, `pedal-ui-sim`, and `pedal-ui-model-smoke`.
- Modify `README.md`: document how to build and run the desktop UI simulator.

### Task 1: UI Model

**Files:**
- Create: `src/ui/UiModel.h`
- Create: `src/ui/UiModel.cpp`
- Create: `tests/ui_model_smoke.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  - `struct UiAsset { std::string name; std::string path; std::string type; }`
  - `struct UiBlock { std::string id; std::string type; std::string label; std::string assetName; std::string assetPath; bool enabled = true; }`
  - `struct UiPreset { std::string name; std::vector<UiBlock> blocks; }`
  - `struct UiBank { std::string name; std::array<UiPreset, 4> presets; }`
  - `enum class UiMode { Preset, Edit }`
  - `struct UiState`
  - `UiState makeDemoUiState()`
  - `void selectPreset(UiState& state, std::size_t index)`
  - `void selectBlock(UiState& state, std::size_t blockIndex)`
  - `void closeBlockDrawer(UiState& state)`
  - `void closeParamDrawer(UiState& state)`
  - `void setCategoryFilter(UiState& state, std::string filter)`

- [ ] **Step 1: Write the failing model smoke test**

Create `tests/ui_model_smoke.cpp`:

```cpp
#include "ui/UiModel.h"

#include <cstdlib>
#include <iostream>

namespace {

int require(bool ok, const char* message)
{
  if (!ok) {
    std::cerr << message << "\n";
    return 1;
  }
  return 0;
}

} // namespace

int main()
{
  auto state = ardor::makeDemoUiState();
  if (require(state.bank.presets.size() == 4, "expected four presets")) return 1;
  if (require(state.bank.name == "Bank 000 - Core Sounds", "expected demo bank name")) return 1;
  if (require(state.activePreset == 0, "expected first preset active")) return 1;
  if (require(state.mode == ardor::UiMode::Preset, "expected preset mode")) return 1;

  ardor::selectPreset(state, 2);
  if (require(state.activePreset == 2, "preset selection failed")) return 1;
  if (require(state.mode == ardor::UiMode::Preset, "preset selection should return to preset mode")) return 1;
  if (require(!state.blockDrawerOpen, "preset selection should close block drawer")) return 1;
  if (require(!state.paramDrawerOpen, "preset selection should close parameter drawer")) return 1;

  state.mode = ardor::UiMode::Edit;
  state.blockDrawerOpen = true;
  ardor::setCategoryFilter(state, "modulation");
  if (require(state.categoryFilter == "modulation", "category filter failed")) return 1;
  ardor::closeBlockDrawer(state);
  if (require(!state.blockDrawerOpen, "block drawer close failed")) return 1;

  ardor::selectBlock(state, 0);
  if (require(state.paramDrawerOpen, "block selection should open parameter drawer")) return 1;
  if (require(state.selectedBlock == 0, "selected block index failed")) return 1;
  ardor::closeParamDrawer(state);
  if (require(!state.paramDrawerOpen, "parameter drawer close failed")) return 1;

  return 0;
}
```

- [ ] **Step 2: Register the failing target**

Add to `CMakeLists.txt` near the existing libraries:

```cmake
add_library(ardor_ui
  src/ui/UiModel.cpp
)
target_include_directories(ardor_ui PUBLIC src)
target_compile_features(ardor_ui PUBLIC cxx_std_20)
target_link_libraries(ardor_ui PUBLIC ardor_preset)
```

Add the test near the other tests:

```cmake
add_executable(pedal-ui-model-smoke tests/ui_model_smoke.cpp)
target_link_libraries(pedal-ui-model-smoke PRIVATE ardor_ui)
add_test(NAME pedal-ui-model-smoke COMMAND pedal-ui-model-smoke)
```

- [ ] **Step 3: Verify the test fails**

Run:

```sh
cmake --build build
```

Expected: build fails because `ui/UiModel.h` does not exist.

- [ ] **Step 4: Implement the model**

Create `src/ui/UiModel.h`:

```cpp
#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace ardor {

struct UiAsset {
  std::string name;
  std::string path;
  std::string type;
};

struct UiBlock {
  std::string id;
  std::string type;
  std::string label;
  std::string assetName;
  std::string assetPath;
  bool enabled = true;
};

struct UiPreset {
  std::string name;
  std::vector<UiBlock> blocks;
};

struct UiBank {
  std::string name;
  std::array<UiPreset, 4> presets;
};

enum class UiMode {
  Preset,
  Edit
};

struct UiState {
  UiBank bank;
  std::vector<UiAsset> assets;
  std::size_t activePreset = 0;
  std::size_t selectedBlock = 0;
  UiMode mode = UiMode::Preset;
  bool dirty = false;
  bool blockDrawerOpen = false;
  bool paramDrawerOpen = false;
  bool effectsBypassed = false;
  int masterVolume = 82;
  std::string categoryFilter = "all";
};

UiState makeDemoUiState();
void selectPreset(UiState& state, std::size_t index);
void selectBlock(UiState& state, std::size_t blockIndex);
void closeBlockDrawer(UiState& state);
void closeParamDrawer(UiState& state);
void setCategoryFilter(UiState& state, std::string filter);

} // namespace ardor
```

Create `src/ui/UiModel.cpp`:

```cpp
#include "UiModel.h"

#include <algorithm>

namespace ardor {

UiState makeDemoUiState()
{
  UiState state;
  state.bank.name = "Bank 000 - Core Sounds";
  state.bank.presets = {
    UiPreset{"Clean Lead", {{"block-1", "nam", "Neural Amp", "Clean Twin", "models/clean.nam", true},
                            {"block-2", "cab", "Cab", "Open Back 2x12", "irs/open-back.wav", true}}},
    UiPreset{"Crunch", {{"block-3", "nam", "Neural Amp", "British Crunch", "models/crunch.nam", true},
                        {"block-4", "cab", "Cab", "Vintage 4x12", "irs/vintage.wav", true}}},
    UiPreset{"Ambient", {{"block-5", "cab", "Cab", "Open Back 2x12", "irs/open-back.wav", true},
                         {"block-6", "nam", "Neural Amp", "Ambient Glass", "models/ambient.nam", true}}},
    UiPreset{"Solo", {{"block-7", "nam", "Neural Amp", "Focused Lead", "models/solo.nam", true},
                      {"block-8", "cab", "Cab", "Focused 1x12", "irs/focus.wav", true}}},
  };
  state.assets = {
    {"Clean Twin", "models/clean.nam", "amps"},
    {"British Crunch", "models/crunch.nam", "amps"},
    {"Ambient Glass", "models/ambient.nam", "amps"},
    {"Focused Lead", "models/solo.nam", "amps"},
    {"Open Back 2x12", "irs/open-back.wav", "cabs"},
    {"Vintage 4x12", "irs/vintage.wav", "cabs"},
    {"Focused 1x12", "irs/focus.wav", "cabs"},
  };
  return state;
}

void selectPreset(UiState& state, std::size_t index)
{
  if (index >= state.bank.presets.size()) return;
  state.activePreset = index;
  state.selectedBlock = 0;
  state.mode = UiMode::Preset;
  state.dirty = false;
  state.blockDrawerOpen = false;
  state.paramDrawerOpen = false;
  state.effectsBypassed = false;
}

void selectBlock(UiState& state, std::size_t blockIndex)
{
  const auto& blocks = state.bank.presets[state.activePreset].blocks;
  if (blockIndex >= blocks.size()) return;
  state.selectedBlock = blockIndex;
  state.paramDrawerOpen = true;
}

void closeBlockDrawer(UiState& state)
{
  state.blockDrawerOpen = false;
}

void closeParamDrawer(UiState& state)
{
  state.paramDrawerOpen = false;
}

void setCategoryFilter(UiState& state, std::string filter)
{
  static constexpr std::array<const char*, 6> valid = {"all", "amps", "cabs", "dynamics", "modulation", "time"};
  const auto found = std::find(valid.begin(), valid.end(), filter);
  state.categoryFilter = found == valid.end() ? "all" : std::move(filter);
}

} // namespace ardor
```

- [ ] **Step 5: Verify the test passes**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure -R pedal-ui-model-smoke
```

Expected: `pedal-ui-model-smoke` passes.

- [ ] **Step 6: Commit**

```sh
git add CMakeLists.txt src/ui/UiModel.h src/ui/UiModel.cpp tests/ui_model_smoke.cpp
git commit -m "feat: add lvgl ui model"
```

### Task 2: LVGL Dependency And Simulator App

**Files:**
- Create: `apps/pedal-ui-sim/main.cpp`
- Create: `src/ui/LvglUi.h`
- Create: `src/ui/LvglUi.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes:
  - `ardor::UiState`
  - `ardor::makeDemoUiState()`
- Produces:
  - `class LvglUi`
  - `void LvglUi::build(lv_obj_t* root, UiState& state)`
  - executable `pedal-ui-sim`

- [ ] **Step 1: Add LVGL FetchContent and a failing simulator build**

Add to `CMakeLists.txt` after existing `FetchContent_Declare` blocks:

```cmake
option(ARDOR_ENABLE_LVGL_UI "Build LVGL desktop UI simulator" ON)

if(ARDOR_ENABLE_LVGL_UI)
  FetchContent_Declare(
    lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG v9.5.0
  )
  FetchContent_MakeAvailable(lvgl)
endif()
```

Add after `ardor_ui`:

```cmake
if(ARDOR_ENABLE_LVGL_UI)
  target_sources(ardor_ui PRIVATE src/ui/LvglUi.cpp)
  target_link_libraries(ardor_ui PUBLIC lvgl)

  add_executable(pedal-ui-sim apps/pedal-ui-sim/main.cpp)
  target_link_libraries(pedal-ui-sim PRIVATE ardor_ui lvgl)
endif()
```

Create `apps/pedal-ui-sim/main.cpp`:

```cpp
#include "ui/LvglUi.h"
#include "ui/UiModel.h"

#include <lvgl.h>

int main()
{
  lv_init();
  ardor::UiState state = ardor::makeDemoUiState();
  ardor::LvglUi ui;
  ui.build(lv_screen_active(), state);
  return 0;
}
```

- [ ] **Step 2: Verify the build fails**

Run:

```sh
cmake --build build
```

Expected: build fails because `ui/LvglUi.h` does not exist.

- [ ] **Step 3: Add the minimal LVGL UI class**

Create `src/ui/LvglUi.h`:

```cpp
#pragma once

#include "UiModel.h"

#include <lvgl.h>

namespace ardor {

class LvglUi {
public:
  void build(lv_obj_t* root, UiState& state);
};

} // namespace ardor
```

Create `src/ui/LvglUi.cpp`:

```cpp
#include "LvglUi.h"

namespace ardor {

void LvglUi::build(lv_obj_t* root, UiState& state)
{
  lv_obj_clean(root);
  lv_obj_set_style_bg_color(root, lv_color_hex(0x101214), 0);

  lv_obj_t* title = lv_label_create(root);
  lv_label_set_text(title, state.bank.name.c_str());
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);
}

} // namespace ardor
```

- [ ] **Step 4: Verify the app builds**

Run:

```sh
cmake --build build --target pedal-ui-sim
```

Expected: `pedal-ui-sim` builds.

- [ ] **Step 5: Commit**

```sh
git add CMakeLists.txt apps/pedal-ui-sim/main.cpp src/ui/LvglUi.h src/ui/LvglUi.cpp
git commit -m "feat: add lvgl ui simulator shell"
```

### Task 3: Preset Mode Screen

**Files:**
- Modify: `src/ui/LvglUi.cpp`
- Modify: `tests/ui_model_smoke.cpp`

**Interfaces:**
- Consumes:
  - `UiState::bank`
  - `UiState::activePreset`
  - `selectPreset(UiState&, std::size_t)`
- Produces:
  - A preset mode screen with bank title and 2x2 preset buttons.

- [ ] **Step 1: Add a model assertion for preset selection**

Append to `tests/ui_model_smoke.cpp` before `return 0;`:

```cpp
  ardor::selectPreset(state, 99);
  if (require(state.activePreset == 2, "invalid preset selection should be ignored")) return 1;
```

- [ ] **Step 2: Run the focused test**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure -R pedal-ui-model-smoke
```

Expected: test passes; this is a guard before UI rendering work.

- [ ] **Step 3: Render preset mode in LVGL**

Replace `LvglUi::build` in `src/ui/LvglUi.cpp` with a simple 800x480-style screen:

```cpp
#include "LvglUi.h"

#include <array>
#include <string>

namespace ardor {

namespace {

lv_obj_t* makeButton(lv_obj_t* parent, const char* text)
{
  lv_obj_t* button = lv_button_create(parent);
  lv_obj_t* label = lv_label_create(button);
  lv_label_set_text(label, text);
  lv_obj_center(label);
  return button;
}

} // namespace

void LvglUi::build(lv_obj_t* root, UiState& state)
{
  lv_obj_clean(root);
  lv_obj_set_style_bg_color(root, lv_color_hex(0x101214), 0);

  lv_obj_t* title = lv_label_create(root);
  lv_label_set_text(title, state.bank.name.c_str());
  lv_obj_set_style_text_color(title, lv_color_hex(0xedf2f7), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

  lv_obj_t* grid = lv_obj_create(root);
  lv_obj_set_size(grid, 760, 360);
  lv_obj_align(grid, LV_ALIGN_BOTTOM_MID, 0, -18);
  lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(grid, 0, 0);
  lv_obj_set_layout(grid, LV_LAYOUT_GRID);

  static int32_t cols[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static int32_t rows[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(grid, cols, rows);

  for (std::size_t i = 0; i < state.bank.presets.size(); ++i) {
    lv_obj_t* button = makeButton(grid, state.bank.presets[i].name.c_str());
    lv_obj_set_grid_cell(button, LV_GRID_ALIGN_STRETCH, static_cast<int32_t>(i % 2), 1,
                         LV_GRID_ALIGN_STRETCH, static_cast<int32_t>(i / 2), 1);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(i == state.activePreset ? 0x143a32 : 0x212733), 0);
    lv_obj_set_style_border_color(button, lv_color_hex(i == state.activePreset ? 0x6ee7b7 : 0x303744), 0);
  }
}

} // namespace ardor
```

- [ ] **Step 4: Verify build**

Run:

```sh
cmake --build build --target pedal-ui-sim
```

Expected: `pedal-ui-sim` builds.

- [ ] **Step 5: Commit**

```sh
git add src/ui/LvglUi.cpp tests/ui_model_smoke.cpp
git commit -m "feat: render lvgl preset mode"
```

### Task 4: Edit Mode Chain And Floating Drawers

**Files:**
- Modify: `src/ui/LvglUi.cpp`
- Modify: `src/ui/UiModel.h`
- Modify: `src/ui/UiModel.cpp`
- Modify: `tests/ui_model_smoke.cpp`

**Interfaces:**
- Consumes:
  - `UiMode::Edit`
  - `UiState::blockDrawerOpen`
  - `UiState::paramDrawerOpen`
  - `UiState::categoryFilter`
- Produces:
  - Edit mode screen with two chain rows, floating block drawer, bottom parameter drawer, close behavior modeled in `UiState`.

- [ ] **Step 1: Add model assertions for drawer behavior**

Append to `tests/ui_model_smoke.cpp` before `return 0;`:

```cpp
  ardor::enterEditMode(state);
  if (require(state.mode == ardor::UiMode::Edit, "edit mode should be active")) return 1;
  ardor::openBlockDrawer(state);
  if (require(state.blockDrawerOpen, "block drawer should open")) return 1;
  ardor::closeBlockDrawer(state);
  if (require(!state.blockDrawerOpen, "block drawer should close")) return 1;
  ardor::setCategoryFilter(state, "bogus");
  if (require(state.categoryFilter == "all", "bad filter should fall back to all")) return 1;
```

- [ ] **Step 2: Add mode and drawer state helpers**

Add declarations to `src/ui/UiModel.h`:

```cpp
void enterPresetMode(UiState& state);
void enterEditMode(UiState& state);
void openBlockDrawer(UiState& state);
```

Add implementations to `src/ui/UiModel.cpp`:

```cpp
void enterPresetMode(UiState& state)
{
  state.mode = UiMode::Preset;
  state.blockDrawerOpen = false;
  state.paramDrawerOpen = false;
}

void enterEditMode(UiState& state)
{
  state.mode = UiMode::Edit;
}

void openBlockDrawer(UiState& state)
{
  state.blockDrawerOpen = true;
}
```

- [ ] **Step 3: Run the focused test**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure -R pedal-ui-model-smoke
```

Expected: test passes.

- [ ] **Step 4: Add render helpers and callbacks**

In `src/ui/LvglUi.h`, keep `build` public and add these private render helpers:

```cpp
void renderPresetMode(lv_obj_t* root, UiState& state);
void renderEditMode(lv_obj_t* root, UiState& state);
void renderBlockDrawer(lv_obj_t* root, UiState& state);
void renderParamDrawer(lv_obj_t* root, UiState& state);
```

Add `#include <vector>` to `src/ui/LvglUi.h`, then add this callback context type before `class LvglUi`:

```cpp
class LvglUi;

struct UiContext {
  LvglUi* ui = nullptr;
  UiState* state = nullptr;
  std::size_t index = 0;
  const char* filter = "all";
};
```

Add this private member to `class LvglUi`:

```cpp
std::vector<UiContext> contexts_;
```

Use these static callback helpers in `src/ui/LvglUi.cpp`:

```cpp
namespace {

void redraw(UiContext* context)
{
  context->ui->build(lv_screen_active(), *context->state);
}

void onBlockClicked(lv_event_t* event)
{
  auto* context = static_cast<UiContext*>(lv_event_get_user_data(event));
  selectBlock(*context->state, context->index);
  redraw(context);
}

void onPresetModeClicked(lv_event_t* event)
{
  auto* context = static_cast<UiContext*>(lv_event_get_user_data(event));
  enterPresetMode(*context->state);
  redraw(context);
}

void onEditModeClicked(lv_event_t* event)
{
  auto* context = static_cast<UiContext*>(lv_event_get_user_data(event));
  enterEditMode(*context->state);
  redraw(context);
}

void onOpenBlockDrawer(lv_event_t* event)
{
  auto* context = static_cast<UiContext*>(lv_event_get_user_data(event));
  openBlockDrawer(*context->state);
  redraw(context);
}

void onCloseBlockDrawer(lv_event_t* event)
{
  auto* context = static_cast<UiContext*>(lv_event_get_user_data(event));
  closeBlockDrawer(*context->state);
  redraw(context);
}

void onCloseParamDrawer(lv_event_t* event)
{
  auto* context = static_cast<UiContext*>(lv_event_get_user_data(event));
  closeParamDrawer(*context->state);
  redraw(context);
}

void onFilterClicked(lv_event_t* event)
{
  auto* context = static_cast<UiContext*>(lv_event_get_user_data(event));
  setCategoryFilter(*context->state, context->filter);
  redraw(context);
}

} // namespace
```

At the start of `LvglUi::build`, call `contexts_.clear();` before creating widgets.

Move the preset-mode code from Task 3 into `LvglUi::renderPresetMode`, then add an edit-mode button at the top right:

```cpp
lv_obj_t* editButton = lv_button_create(root);
lv_obj_align(editButton, LV_ALIGN_TOP_RIGHT, -18, 16);
lv_obj_t* editLabel = lv_label_create(editButton);
lv_label_set_text(editLabel, "Edit");
lv_obj_center(editLabel);
contexts_.push_back({this, &state, 0, "all"});
lv_obj_add_event_cb(editButton, onEditModeClicked, LV_EVENT_CLICKED, &contexts_.back());
```

Implement `LvglUi::renderEditMode` with two horizontal containers:

```cpp
void LvglUi::renderEditMode(lv_obj_t* root, UiState& state)
{
  const auto& blocks = state.bank.presets[state.activePreset].blocks;
  lv_obj_t* presetButton = lv_button_create(root);
  lv_obj_align(presetButton, LV_ALIGN_TOP_LEFT, 18, 16);
  lv_obj_t* presetLabel = lv_label_create(presetButton);
  lv_label_set_text(presetLabel, "Presets");
  lv_obj_center(presetLabel);
  contexts_.push_back({this, &state, 0, "all"});
  lv_obj_add_event_cb(presetButton, onPresetModeClicked, LV_EVENT_CLICKED, &contexts_.back());

  lv_obj_t* addButton = lv_button_create(root);
  lv_obj_align(addButton, LV_ALIGN_TOP_RIGHT, -18, 16);
  lv_obj_t* addLabel = lv_label_create(addButton);
  lv_label_set_text(addLabel, "Blocks");
  lv_obj_center(addLabel);
  contexts_.push_back({this, &state, 0, "all"});
  lv_obj_add_event_cb(addButton, onOpenBlockDrawer, LV_EVENT_CLICKED, &contexts_.back());

  lv_obj_t* top = lv_obj_create(root);
  lv_obj_t* bottom = lv_obj_create(root);
  lv_obj_set_size(top, 760, 120);
  lv_obj_set_size(bottom, 760, 120);
  lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 72);
  lv_obj_align(bottom, LV_ALIGN_TOP_MID, 0, 226);
  lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_flow(bottom, LV_FLEX_FLOW_ROW);

  for (std::size_t i = 0; i < blocks.size(); ++i) {
    lv_obj_t* row = i < 4 ? top : bottom;
    lv_obj_t* button = lv_button_create(row);
    lv_obj_set_size(button, 160, 84);
    lv_obj_t* label = lv_label_create(button);
    std::string text = blocks[i].label + "\n" + blocks[i].assetName;
    lv_label_set_text(label, text.c_str());
    lv_obj_center(label);
    contexts_.push_back({this, &state, i, "all"});
    lv_obj_add_event_cb(button, onBlockClicked, LV_EVENT_CLICKED, &contexts_.back());
  }

  if (state.blockDrawerOpen) renderBlockDrawer(root, state);
  if (state.paramDrawerOpen) renderParamDrawer(root, state);
}
```

Implement `LvglUi::renderBlockDrawer` as a left floating panel:

```cpp
void LvglUi::renderBlockDrawer(lv_obj_t* root, UiState& state)
{
  lv_obj_t* panel = lv_obj_create(root);
  lv_obj_set_size(panel, 260, 420);
  lv_obj_align(panel, LV_ALIGN_LEFT_MID, 14, 0);
  lv_obj_set_style_bg_color(panel, lv_color_hex(0x171b22), 0);
  lv_obj_set_style_radius(panel, 8, 0);

  lv_obj_t* close = lv_button_create(panel);
  lv_obj_align(close, LV_ALIGN_TOP_RIGHT, -10, 10);
  lv_obj_t* closeLabel = lv_label_create(close);
  lv_label_set_text(closeLabel, "X");
  lv_obj_center(closeLabel);
  contexts_.push_back({this, &state, 0, "all"});
  lv_obj_add_event_cb(close, onCloseBlockDrawer, LV_EVENT_CLICKED, &contexts_.back());

  static constexpr std::array filters = {
    std::pair{"All", "all"}, std::pair{"Amps", "amps"}, std::pair{"Cabs", "cabs"},
    std::pair{"Dynamics", "dynamics"}, std::pair{"Mod", "modulation"}, std::pair{"Time", "time"},
  };

  for (const auto& [labelText, filter] : filters) {
    lv_obj_t* button = lv_button_create(panel);
    lv_obj_t* label = lv_label_create(button);
    lv_label_set_text(label, labelText);
    lv_obj_center(label);
    contexts_.push_back({this, &state, 0, filter});
    lv_obj_add_event_cb(button, onFilterClicked, LV_EVENT_CLICKED, &contexts_.back());
  }

  for (const auto& asset : state.assets) {
    if (state.categoryFilter != "all" && asset.type != state.categoryFilter) continue;
    lv_obj_t* item = lv_label_create(panel);
    lv_label_set_text(item, asset.name.c_str());
  }
}
```

Implement `LvglUi::renderParamDrawer` as a bottom floating panel:

```cpp
void LvglUi::renderParamDrawer(lv_obj_t* root, UiState& state)
{
  const auto& block = state.bank.presets[state.activePreset].blocks[state.selectedBlock];
  lv_obj_t* panel = lv_obj_create(root);
  lv_obj_set_size(panel, 720, 126);
  lv_obj_align(panel, LV_ALIGN_BOTTOM_MID, 0, -14);
  lv_obj_set_style_bg_color(panel, lv_color_hex(0x1b2028), 0);
  lv_obj_set_style_radius(panel, 8, 0);

  lv_obj_t* close = lv_button_create(panel);
  lv_obj_align(close, LV_ALIGN_TOP_RIGHT, -10, 10);
  lv_obj_t* closeLabel = lv_label_create(close);
  lv_label_set_text(closeLabel, "X");
  lv_obj_center(closeLabel);
  contexts_.push_back({this, &state, 0, "all"});
  lv_obj_add_event_cb(close, onCloseParamDrawer, LV_EVENT_CLICKED, &contexts_.back());

  lv_obj_t* title = lv_label_create(panel);
  const std::string text = block.label + " - " + block.assetName;
  lv_label_set_text(title, text.c_str());
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 16, 12);
}
```

Update `LvglUi::build` so it routes by mode:

```cpp
if (state.mode == UiMode::Preset) {
  renderPresetMode(root, state);
} else {
  renderEditMode(root, state);
}
```

- [ ] **Step 5: Verify build**

Run:

```sh
cmake --build build --target pedal-ui-sim
ctest --test-dir build --output-on-failure -R pedal-ui-model-smoke
```

Expected: simulator target builds and UI model smoke passes.

- [ ] **Step 6: Commit**

```sh
git add src/ui/LvglUi.cpp src/ui/UiModel.h src/ui/UiModel.cpp tests/ui_model_smoke.cpp
git commit -m "feat: render lvgl edit mode shell"
```

### Task 5: README And Final Verification

**Files:**
- Modify: `README.md`

**Interfaces:**
- Consumes all prior UI shell tasks.
- Produces documented commands for running the simulator.

- [ ] **Step 1: Update README**

Add:

````markdown
## LVGL UI Simulator

The first LVGL UI target is a desktop simulator:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target pedal-ui-sim
./build/pedal-ui-sim
```

This shell is for validating screen layout and UI state only. It does not wire footswitch GPIO, the encoder, Codec Zero, or realtime audio yet.
````

- [ ] **Step 2: Run final verification**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
test -x build/pedal-ui-sim
git status --short --branch
```

Expected:

- build succeeds
- all tests pass
- `build/pedal-ui-sim` exists and is executable
- working tree is clean after commit

- [ ] **Step 3: Commit**

```sh
git add README.md
git commit -m "docs: document lvgl ui simulator"
```

## Self-Review Notes

- Spec coverage: plan covers preset mode 2x2 grid, active preset color, edit chain, floating closable block drawer, category filters, bottom floating parameter drawer, asset names, and desktop-first LVGL shell.
- Scope check: no GPIO, encoder driver, touch driver, audio engine wiring, Pi DRM/fbdev, or Buildroot LVGL packaging in this phase.
- Type consistency: `UiState`, `UiMode`, `UiBank`, `UiPreset`, `UiBlock`, `UiAsset`, and `LvglUi::build(lv_obj_t*, UiState&)` are defined before use.

## References

- LVGL 9.5 docs: https://docs.lvgl.io/9.5/
- LVGL Linux port: https://github.com/lvgl/lv_port_linux
- LVGL drivers note: https://github.com/lvgl/lv_drivers
