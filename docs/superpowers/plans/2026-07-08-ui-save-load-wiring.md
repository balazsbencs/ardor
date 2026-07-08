# UI Save/Load Wiring Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the LVGL simulator load, edit, select, and manually save real preset JSON files instead of demo-only state.

**Architecture:** Reuse `PresetStore`, `PresetSlot`, `activePresetToPreset`, and `replaceActivePreset`. Keep persistence owned by the simulator app and expose only small UI callbacks from `LvglUi`; the UI model remains plain data.

**Tech Stack:** C++20, LVGL v9, SDL simulator, existing CMake smoke-test style, nlohmann/json through the preset library.

## Global Constraints

- Keep one current bank visible with four preset slots.
- Keep preset files under `presets/bank-000/preset-0.json` through `preset-3.json`.
- Keep asset paths relative to `--data-root`.
- Scan only `models/*.nam` and `irs/*.wav` for v1 assets.
- Manual save only; edits must dirty the UI and not auto-write files.
- Do not add dependencies.

---

## File Structure

- `src/ui/UiModel.h`: add small disk-facing helpers for loading assets, loading a bank, loading a slot, and saving the active slot.
- `src/ui/UiModel.cpp`: implement helpers using `PresetStore` and `std::filesystem`.
- `src/ui/LvglUi.h`: add optional callbacks for preset select and save.
- `src/ui/LvglUi.cpp`: call callbacks from preset buttons and a new edit-mode Save button.
- `apps/pedal-ui-sim/main.cpp`: parse `--data-root` and `--bank`, load real files, and wire callbacks.
- `tests/ui_model_smoke.cpp`: extend the smoke test to cover file-backed load/save.
- `README.md`: document simulator launch with `--data-root`.

---

### Task 1: Add UI File Helpers

**Files:**
- Modify: `src/ui/UiModel.h`
- Modify: `src/ui/UiModel.cpp`
- Modify: `tests/ui_model_smoke.cpp`

**Interfaces:**
- Consumes:
  - `ardor::PresetStore`
  - `ardor::PresetSlot`
  - `ardor::PresetStore::load(PresetSlot)`
  - `ardor::PresetStore::save(PresetSlot, const Preset&)`
- Produces:
  - `void loadAssetsFromDataRoot(UiState& state, const std::filesystem::path& dataRoot)`
  - `void loadBankFromStore(UiState& state, const PresetStore& store, int bank)`
  - `bool loadPresetSlotFromStore(UiState& state, const PresetStore& store, PresetSlot slot, std::string& error)`
  - `bool saveActivePresetToStore(UiState& state, const PresetStore& store, int bank, std::string& error)`

- [ ] **Step 1: Write the failing test**

Append this block near the end of `tests/ui_model_smoke.cpp`, before `return 0;`:

```cpp
  const auto root = std::filesystem::temp_directory_path() / "ardor-ui-model-smoke";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "models");
  std::filesystem::create_directories(root / "irs");
  {
    std::ofstream(root / "models/clean.nam").put('\n');
    std::ofstream(root / "irs/open.wav").put('\n');
  }

  ardor::PresetStore store(root);
  ardor::Preset diskPreset;
  diskPreset.name = "Disk Clean";
  diskPreset.blocks.push_back({"amp-1", "nam", true, "models/clean.nam", nlohmann::json::object()});
  store.save({0, 0}, diskPreset);

  auto diskState = ardor::makeDemoUiState();
  ardor::loadAssetsFromDataRoot(diskState, root);
  if (require(diskState.assets.size() >= 2, "data root assets should load")) return 1;
  if (require(diskState.assets[0].name == "clean", "model asset should use file stem")) return 1;

  ardor::loadBankFromStore(diskState, store, 0);
  if (require(diskState.bank.name == "Bank 000", "disk bank name")) return 1;
  if (require(diskState.bank.presets[0].name == "Disk Clean", "bank slot should load preset")) return 1;
  if (require(diskState.bank.presets[1].name == "Empty 2", "missing slot should become empty")) return 1;

  ardor::appendAssetBlock(diskState, 1);
  std::string ioError;
  if (require(ardor::saveActivePresetToStore(diskState, store, 0, ioError), "active preset save should succeed")) return 1;
  if (require(!diskState.dirty, "saving should clear dirty flag")) return 1;
  const auto saved = store.load({0, 0});
  if (require(saved.blocks.size() == 2, "saved preset should include edited chain")) return 1;
  std::filesystem::remove_all(root);
```

Add includes:

```cpp
#include "preset/PresetStore.h"

#include <filesystem>
#include <fstream>
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
cmake --build build --target pedal-ui-model-smoke && ctest --test-dir build -R pedal-ui-model-smoke --output-on-failure
```

Expected: compile fails because the new UI file helper declarations do not exist.

- [ ] **Step 3: Add declarations**

In `src/ui/UiModel.h`, add includes:

```cpp
#include "preset/PresetStore.h"

#include <filesystem>
```

Add declarations after `replaceActivePreset`:

```cpp
void loadAssetsFromDataRoot(UiState& state, const std::filesystem::path& dataRoot);
void loadBankFromStore(UiState& state, const PresetStore& store, int bank);
bool loadPresetSlotFromStore(UiState& state, const PresetStore& store, PresetSlot slot, std::string& error);
bool saveActivePresetToStore(UiState& state, const PresetStore& store, int bank, std::string& error);
```

- [ ] **Step 4: Implement the helpers**

In `src/ui/UiModel.cpp`, add includes:

```cpp
#include <filesystem>
#include <iomanip>
#include <sstream>
```

Add these helpers inside the anonymous namespace:

```cpp
std::string bankName(int bank)
{
  std::ostringstream out;
  out << "Bank " << std::setw(3) << std::setfill('0') << bank;
  return out.str();
}

UiPreset emptyPreset(std::size_t index)
{
  return {"Empty " + std::to_string(index + 1), {}};
}

void appendAssetsFrom(UiState& state, const std::filesystem::path& dir, const std::string& ext, const std::string& type)
{
  if (!std::filesystem::exists(dir)) {
    return;
  }

  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (!entry.is_regular_file() || entry.path().extension() != ext) {
      continue;
    }
    const auto relative = std::filesystem::relative(entry.path(), dir.parent_path()).generic_string();
    state.assets.push_back({entry.path().stem().string(), relative, type});
  }
}
```

Add the public functions near the bottom of `src/ui/UiModel.cpp`:

```cpp
void loadAssetsFromDataRoot(UiState& state, const std::filesystem::path& dataRoot)
{
  state.assets.clear();
  appendAssetsFrom(state, dataRoot / "models", ".nam", "amps");
  appendAssetsFrom(state, dataRoot / "irs", ".wav", "cabs");
  state.assets.push_back({"Compressor", "", "dynamics"});
  state.assets.push_back({"Wide Chorus", "", "modulation"});
  state.assets.push_back({"Tape Delay", "", "time"});
}

bool loadPresetSlotFromStore(UiState& state, const PresetStore& store, PresetSlot slot, std::string& error)
{
  try {
    state.activePreset = static_cast<std::size_t>(slot.preset);
    replaceActivePreset(state, store.load(slot));
    return true;
  } catch (const std::exception& e) {
    error = e.what();
    return false;
  }
}

void loadBankFromStore(UiState& state, const PresetStore& store, int bank)
{
  state.bank.name = bankName(bank);
  const auto previous = state.activePreset;
  for (std::size_t i = 0; i < state.bank.presets.size(); ++i) {
    std::string error;
    if (!loadPresetSlotFromStore(state, store, {bank, static_cast<int>(i)}, error)) {
      state.bank.presets[i] = emptyPreset(i);
    }
  }
  state.activePreset = std::min(previous, state.bank.presets.size() - 1);
  state.selectedBlock = 0;
  state.dirty = false;
  state.paramDrawerOpen = false;
}

bool saveActivePresetToStore(UiState& state, const PresetStore& store, int bank, std::string& error)
{
  try {
    store.save({bank, static_cast<int>(state.activePreset)}, activePresetToPreset(state));
    state.dirty = false;
    return true;
  } catch (const std::exception& e) {
    error = e.what();
    return false;
  }
}
```

- [ ] **Step 5: Run the model smoke test**

Run:

```bash
cmake --build build --target pedal-ui-model-smoke && ctest --test-dir build -R pedal-ui-model-smoke --output-on-failure
```

Expected: `pedal-ui-model-smoke` passes.

- [ ] **Step 6: Commit**

```bash
git add src/ui/UiModel.h src/ui/UiModel.cpp tests/ui_model_smoke.cpp
git commit -m "feat: load ui presets from disk"
```

---

### Task 2: Wire LVGL Actions To The Store

**Files:**
- Modify: `src/ui/LvglUi.h`
- Modify: `src/ui/LvglUi.cpp`
- Modify: `apps/pedal-ui-sim/main.cpp`

**Interfaces:**
- Consumes:
  - `loadAssetsFromDataRoot(UiState&, const std::filesystem::path&)`
  - `loadBankFromStore(UiState&, const PresetStore&, int)`
  - `loadPresetSlotFromStore(UiState&, const PresetStore&, PresetSlot, std::string&)`
  - `saveActivePresetToStore(UiState&, const PresetStore&, int, std::string&)`
- Produces:
  - `pedal-ui-sim --data-root DIR --bank N`
  - `LvglUi::LvglUi(UiActions actions = {})`
  - Manual Save button in edit mode.

- [ ] **Step 1: Add LVGL callback plumbing**

In `src/ui/LvglUi.h`, add:

```cpp
#include <functional>
```

Add before `class LvglUi`:

```cpp
struct UiActions {
  std::function<void(std::size_t)> selectPreset;
  std::function<void()> savePreset;
};
```

Change the public API:

```cpp
explicit LvglUi(UiActions actions = {});
void build(lv_obj_t* root, UiState& state);
```

Add a member:

```cpp
UiActions actions_;
```

Add a small accessor so LVGL C callbacks can stay as free functions:

```cpp
const UiActions& actions() const { return actions_; }
```

In `src/ui/LvglUi.cpp`, add:

```cpp
LvglUi::LvglUi(UiActions actions)
  : actions_(std::move(actions))
{
}
```

Add include:

```cpp
#include <utility>
```

- [ ] **Step 2: Use the preset callback**

Change `onPresetClicked` in `src/ui/LvglUi.cpp`:

```cpp
void onPresetClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (context->ui->actions().selectPreset) {
    context->ui->actions().selectPreset(context->index);
  } else {
    selectPreset(*context->state, context->index);
  }
  redraw(context);
}
```

- [ ] **Step 3: Add the Save button**

Add handler in `src/ui/LvglUi.cpp`:

```cpp
void onSaveClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (context->ui->actions().savePreset) {
    context->ui->actions().savePreset();
  }
  redraw(context);
}
```

In `renderEditMode`, between the Presets and Blocks buttons:

```cpp
  lv_obj_t* save = button(root, state.dirty ? "Save*" : "Save");
  lv_obj_set_size(save, 96, 44);
  lv_obj_align(save, LV_ALIGN_TOP_RIGHT, -140, 14);
  lv_obj_set_style_bg_color(save, lv_color_hex(state.dirty ? 0x25634f : 0x2b3442), 0);
  lv_obj_add_event_cb(save, onSaveClicked, LV_EVENT_CLICKED, remember(state));
```

- [ ] **Step 4: Parse simulator arguments and load real files**

Replace `apps/pedal-ui-sim/main.cpp` with an argument-aware version:

```cpp
#define SDL_MAIN_HANDLED

#include "preset/PresetStore.h"
#include "ui/LvglUi.h"
#include "ui/UiModel.h"

#include <filesystem>
#include <iostream>
#include <string>

#include <lvgl.h>

namespace {

struct Args {
  std::filesystem::path dataRoot = ".";
  int bank = 0;
};

bool parse(int argc, char** argv, Args& args)
{
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--data-root" && i + 1 < argc) {
      args.dataRoot = argv[++i];
    } else if (a == "--bank" && i + 1 < argc) {
      args.bank = std::stoi(argv[++i]);
    } else {
      return false;
    }
  }
  return args.bank >= 0 && args.bank < 100;
}

} // namespace

int main(int argc, char** argv)
{
  Args args;
  if (!parse(argc, argv, args)) {
    std::cerr << "Usage: pedal-ui-sim [--data-root DIR] [--bank 0]\n";
    return 2;
  }

  lv_init();
  lv_sdl_window_create(800, 480);
  lv_sdl_mouse_create();
  lv_sdl_mousewheel_create();
  lv_sdl_keyboard_create();

  ardor::PresetStore store(args.dataRoot);
  ardor::UiState state = ardor::makeDemoUiState();
  ardor::loadAssetsFromDataRoot(state, args.dataRoot);
  ardor::loadBankFromStore(state, store, args.bank);

  ardor::LvglUi ui({
    [&](std::size_t index) {
      ardor::selectPreset(state, index);
      std::string error;
      if (!ardor::loadPresetSlotFromStore(state, store, {args.bank, static_cast<int>(index)}, error)) {
        std::cerr << error << "\n";
      }
    },
    [&]() {
      std::string error;
      if (!ardor::saveActivePresetToStore(state, store, args.bank, error)) {
        std::cerr << error << "\n";
      }
    },
  });
  ui.build(lv_screen_active(), state);

  while (true) {
    lv_timer_handler();
    lv_delay_ms(5);
  }
}
```

- [ ] **Step 5: Build and run the simulator smoke**

Run:

```bash
cmake --build build --target pedal-ui-sim pedal-ui-model-smoke
ctest --test-dir build -R pedal-ui-model-smoke --output-on-failure
SDL_VIDEODRIVER=dummy ./build/pedal-ui-sim --data-root . --bank 0
```

Expected: test passes; simulator starts and stays running until interrupted.

- [ ] **Step 6: Commit**

```bash
git add src/ui/LvglUi.h src/ui/LvglUi.cpp apps/pedal-ui-sim/main.cpp
git commit -m "feat: wire simulator preset save load"
```

---

### Task 3: Document Simulator Preset Files

**Files:**
- Modify: `README.md`

**Interfaces:**
- Consumes:
  - `pedal-ui-sim --data-root DIR --bank N`
- Produces:
  - Developer-facing launch instructions.

- [ ] **Step 1: Add README section**

Add:

````markdown
### LVGL simulator with preset files

The simulator can load the same preset files used by `pedal-poc`:

```sh
./build/pedal-ui-sim --data-root . --bank 0
```

It reads:

- `presets/bank-000/preset-0.json`
- `presets/bank-000/preset-1.json`
- `presets/bank-000/preset-2.json`
- `presets/bank-000/preset-3.json`

Assets are discovered from `models/*.nam` and `irs/*.wav`. Editing the chain only changes memory until the Save button is pressed.
````

- [ ] **Step 2: Run docs-adjacent verification**

Run:

```bash
cmake --build build --target pedal-ui-sim pedal-ui-model-smoke && ctest --test-dir build -R pedal-ui-model-smoke --output-on-failure
```

Expected: build and UI model smoke pass.

- [ ] **Step 3: Commit**

```bash
git add README.md
git commit -m "docs: document ui preset files"
```

---

## Skipped For This Phase

- Bank up/down UI controls; add when the physical footswitch bank combination is designed.
- Asset metadata files; filename stems are enough until real NAM/IR libraries need friendly aliases.
- Runtime audio reload from the simulator; this phase only makes UI state own real preset files.
