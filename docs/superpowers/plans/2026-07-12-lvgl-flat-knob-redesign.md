# LVGL Flat Knob Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the existing 800×480 LVGL shell with the approved 1280×720 Open Sans, flat charcoal, six-knob pedal editor while preserving preset and audio behaviour.

**Architecture:** Keep `UiModel` as the source of truth and rebuild only the LVGL renderer. Add a small parameter-control descriptor layer in `LvglUi.cpp`; it resolves every displayed knob to the existing global or block setter, formats the value, and supplies drag/encoder metadata. Renderers consume that layer for both global and selected-effect panels.

**Tech Stack:** C++20, LVGL 9.5, SDL simulator, Linux fbdev target, bundled Open Sans, CMake/CTest.

## Global Constraints

- Author the LVGL canvas at exactly 1280×720 while preserving the current uniform scaling and inverse touch transform.
- Use Open Sans Regular and Semibold assets bundled with the application; do not rely on host system fonts.
- Canvas is black; all block surfaces are borderless `#242424` with a 5px radius and no shadows or gradients.
- Acid green `#43F05A` is functional-only: value arcs, focused labels, selection indicator, save/attention status.
- Continuous values use vertical-drag virtual knobs and encoder fine adjustment; no parameter `+`/`−` controls remain.
- A parameter page contains at most six bottom-aligned compact knobs; Bypass is an explicit switch beside page status.
- Preserve preset JSON, audio processing, block reorder/insertion, save, telemetry, and existing validated `UiModel` setters.

---

## File structure

- Modify `lv_conf.h`: remove Montserrat configuration once no renderer source references it.
- Create `assets/fonts/OpenSans-Regular.ttf`, `assets/fonts/OpenSans-SemiBold.ttf`: tracked font source assets under the SIL Open Font License.
- Create `src/ui/fonts/OpenSansRegular.h`, `src/ui/fonts/OpenSansSemibold.h`, `src/ui/fonts/OpenSansRegular.c`, `src/ui/fonts/OpenSansSemibold.c`: generated LVGL font declarations and glyph data for the UI character set.
- Modify `CMakeLists.txt`: compile generated font sources into `ardor_ui`.
- Create `src/ui/ParameterControls.h` and `src/ui/ParameterControls.cpp`: UI-independent parameter descriptors, pages, value formatting, and clamped writes.
- Modify `src/ui/LvglUi.h`: store focused-parameter and page interaction state in `UiEventContext`/`LvglUi`.
- Modify `src/ui/UiModel.h` and `src/ui/UiModel.cpp`: add `setSelectedBlockEnabled(UiState&, bool)` for the bypass switch.
- Modify `src/ui/LvglUi.cpp`: render the new shell, knob primitives, page selection, switch, drag handling, and encoder target integration.
- Modify `apps/pedal-ui-sim/main.cpp`: open the simulator at 1280×720.
- Modify `apps/pedal-poc/main.cpp`: route physical encoder deltas to a focused UI parameter before falling back to master volume.
- Modify `tests/ui_model_smoke.cpp` and create `tests/lvgl_ui_smoke.cpp`: retain model coverage and add renderer/control smoke coverage.
- Modify `CMakeLists.txt`: build and register `pedal-lvgl-ui-smoke` only when an LVGL backend is enabled.

### Task 1: Bundle and wire Open Sans

**Files:**
- Create: `assets/fonts/OpenSans-Regular.ttf`, `assets/fonts/OpenSans-SemiBold.ttf`
- Create: `src/ui/fonts/OpenSansRegular.h`, `src/ui/fonts/OpenSansSemibold.h`, `src/ui/fonts/OpenSansRegular.c`, `src/ui/fonts/OpenSansSemibold.c`
- Modify: `lv_conf.h`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `extern const lv_font_t ardor_font_open_sans_regular_18;`, `extern const lv_font_t ardor_font_open_sans_semibold_22;`, and `extern const lv_font_t ardor_font_open_sans_semibold_28;`.

- [ ] **Step 1: Add the failing compile reference**

Replace the default font argument in `src/ui/LvglUi.cpp` with `&ardor_font_open_sans_regular_18` and include `ui/fonts/OpenSansRegular.h`; configure/build and expect a missing-header error.

Run: `cmake --build build --target ardor_ui`

Expected: FAIL reporting that `ui/fonts/OpenSansRegular.h` does not exist.

- [ ] **Step 2: Generate and add the font sources**

Use LVGL's font converter against the two bundled TTF files for the exact UI ranges `0x20-0x7E,0xB1,0xD7,0x2212`; produce regular 18 and semibold 22/28 sources, with the declarations above. Keep the generated C files checked in so target builds do not require Node or the converter.

- [ ] **Step 3: Compile fonts into the UI target**

Add these exact sources to `ardor_ui`:

```cmake
target_sources(ardor_ui PRIVATE
  src/ui/fonts/OpenSansRegular.c
  src/ui/fonts/OpenSansSemibold.c
)
```

Remove `LV_FONT_MONTSERRAT_14`, `_18`, `_22`, and `_28` from `lv_conf.h` after all renderer references have changed.

- [ ] **Step 4: Build and commit**

Run: `cmake --build build --target pedal-ui-sim`

Expected: PASS and no `lv_font_montserrat` references from `rg -n 'lv_font_montserrat' src lv_conf.h`.

Commit: `git add assets/fonts src/ui/fonts lv_conf.h CMakeLists.txt src/ui/LvglUi.cpp && git commit -m "feat: bundle Open Sans for LVGL"`

### Task 2: Add parameter metadata, bypass setter, and control tests

**Files:**
- Create: `src/ui/ParameterControls.h`
- Create: `src/ui/ParameterControls.cpp`
- Modify: `src/ui/UiModel.h`
- Modify: `src/ui/UiModel.cpp`
- Modify: `src/ui/LvglUi.h`
- Create: `tests/lvgl_ui_smoke.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `setActiveInputGainDb(UiState&, float)`, `setActiveOutputGainDb(UiState&, float)`, and `setSelectedBlockParam(UiState&, const std::string&, float)`.
- Produces: `ParameterControl { std::string key; std::string label; float minimum; float maximum; float step; float value; std::string formatted; }`, `std::vector<ParameterControl> parameterPage(const UiState&, std::size_t)`, `std::size_t parameterPageCount(const UiState&)`, `bool applyParameterDelta(UiState&, const ParameterControl&, int)`, and `setSelectedBlockEnabled(UiState&, bool)`.

- [ ] **Step 1: Write failing metadata tests**

Create `tests/lvgl_ui_smoke.cpp` including `ui/ParameterControls.h`, with assertions that a cab page contains `levelDb` and `mix`, a Daisy page has no more than six controls, and a seven-parameter Daisy descriptor produces two pages.

```cpp
require(ardor::parameterPage(state, 0).size() <= 6, "page must contain <= six knobs");
require(ardor::parameterPageCount(state) == 2, "seven params require two pages");
```

- [ ] **Step 2: Register and run the failing test**

Add `pedal-lvgl-ui-smoke` linked to `ardor_ui` and register it only inside `if(NOT ARDOR_UI_BACKEND STREQUAL "none")`.

Add `src/ui/ParameterControls.cpp` to the existing `add_library(ardor_ui ...)` source list so both the renderer and smoke target link the descriptor implementation.

Run: `cmake --build build --target pedal-lvgl-ui-smoke && ctest --test-dir build -R pedal-lvgl-ui-smoke --output-on-failure`

Expected: FAIL until `parameterPage` and `parameterPageCount` are implemented.

- [ ] **Step 3: Implement descriptors and focused-control state**

Define `ParameterControl` and descriptor conversion in `ParameterControls.cpp`. Cab controls use `levelDb: -60..12, step 1, dB` and `mix: 0..1, step .05, %`; Daisy controls use their catalog key/default and `0..1, step .05, %`; globals use their existing setter ranges. `applyParameterDelta` clamps and delegates only to existing setters. Add `setSelectedBlockEnabled` in `UiModel.cpp` to set the selected block's `enabled` field and `dirty = true`. Add `focusedKey_` and `parameterPage_` to `LvglUi`, reset the page when a block/global target changes, and expose a method used by the app loop to apply one encoder delta through `applyParameterDelta`.

- [ ] **Step 4: Run tests and commit**

Run: `cmake --build build --target pedal-lvgl-ui-smoke pedal-ui-model-smoke && ctest --test-dir build -R 'pedal-(lvgl-ui|ui-model)-smoke' --output-on-failure`

Expected: PASS.

Commit: `git add src/ui/ParameterControls.* src/ui/UiModel.* src/ui/LvglUi.h tests/lvgl_ui_smoke.cpp CMakeLists.txt && git commit -m "feat: model LVGL knob parameter pages"`

### Task 3: Replace the LVGL shell and parameter drawer with the approved editor

**Files:**
- Modify: `src/ui/LvglUi.cpp`
- Modify: `apps/pedal-ui-sim/main.cpp`

**Interfaces:**
- Consumes: `parameterPage`, `ParameterControl`, focused control state from Task 2.
- Produces: reusable `styleSurface`, `createKnob`, `renderParameterPanel`, `renderBypassSwitch`, and `renderPageNavigation` helpers.

- [ ] **Step 1: Update fixed geometry and simulator size**

Set `kDesignWidth = 1280`, `kDesignHeight = 720`, and change `lv_sdl_window_create(800, 480)` to `lv_sdl_window_create(1280, 720)`.

- [ ] **Step 2: Replace old panel/button styles**

Implement the shared rules: black root, charcoal `#242424` blocks, `lv_obj_set_style_border_width(object, 0, 0)`, `lv_obj_set_style_radius(object, 5, 0)`, no shadow width, Open Sans label styles, and acid green only for functional state.

- [ ] **Step 3: Render Studio Chain and six-knob panel**

Render the left-to-right chain in one row, with charcoal modules and no border. Mark selection with green category text plus a small green indicator. Render the selected effect title/page/switch at the panel top and the six compact 270° knobs against the panel bottom. Draw each knob from a dark 270° track, thin green value arc, black-rimmed charcoal centre, white radial pointer, and name/value below.

- [ ] **Step 4: Replace `+`/`−` handlers with knob gestures and switch/page events**

On press, store the canvas-space Y coordinate and focus the clicked control. On pressing, convert `pressY - currentY` into `step` changes, call `applyParameterDelta`, and rebuild only when the value changed. Add explicit previous/next page hit targets plus horizontal-swipe handling. Render Bypass as an `lv_switch` whose `LV_EVENT_VALUE_CHANGED` callback calls `setSelectedBlockEnabled(*context->state, lv_obj_has_state(switchObject, LV_STATE_CHECKED))`.

- [ ] **Step 5: Manual simulator verification and commit**

Run: `cmake --build build --target pedal-ui-sim && ./build/pedal-ui-sim --data-root .`

Expected: a 1280×720 Open Sans screen with borderless 5px charcoal modules, a six-knob bottom row, vertical knob changes, page navigation, and an on/off bypass switch.

Commit: `git add src/ui/LvglUi.cpp apps/pedal-ui-sim/main.cpp && git commit -m "feat: render flat six-knob LVGL editor"`

### Task 4: Route encoder focus, preserve workflows, and verify

**Files:**
- Modify: `apps/pedal-poc/main.cpp`
- Modify: `tests/lvgl_ui_smoke.cpp`
- Modify: `tests/ui_model_smoke.cpp`

**Interfaces:**
- Consumes: `LvglUi::applyFocusedEncoderDelta(UiState&, int)` from Task 2.
- Produces: encoder priority: focused UI control, otherwise existing `ControlState::masterVolume`.

- [ ] **Step 1: Add focused-encoder tests**

Add tests asserting one encoder tick changes the focused cab/Daisy/global parameter by its descriptor step, clamps at its limit, marks the preset dirty, and leaves master volume unchanged. Add a no-focus test asserting the existing master-volume path still applies.

- [ ] **Step 2: Run tests to verify failure**

Run: `cmake --build build --target pedal-lvgl-ui-smoke pedal-control-smoke && ctest --test-dir build -R 'pedal-(lvgl-ui|control)-smoke' --output-on-failure`

Expected: FAIL until the app loop gives focused control priority.

- [ ] **Step 3: Implement app-loop routing**

At the current relative-encoder handling point in `apps/pedal-poc/main.cpp`, call `ui->applyFocusedEncoderDelta(uiState, event.delta)` first. Update the live engine/global runtime state only when that call reports it consumed the delta; otherwise retain the current master-volume handling unchanged.

- [ ] **Step 4: Run full verification and commit**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`

Expected: PASS. Then run `rg -n 'lv_font_montserrat|button\(.*["'"'][-+]["'"']' src/ui` and expect no parameter increment/decrement controls.

Commit: `git add apps/pedal-poc/main.cpp tests/lvgl_ui_smoke.cpp tests/ui_model_smoke.cpp && git commit -m "feat: tune focused knob with hardware encoder"`
