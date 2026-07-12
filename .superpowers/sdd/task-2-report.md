# Task 2 report: parameter metadata, bypass setter, and control tests

## Delivered

- Added UI-independent `ParameterControl` metadata plus paging, formatting, and
  clamped delta application in `src/ui/ParameterControls.*`.
- Cab metadata is `levelDb` (`-60..12`, step `1`, dB) and `mix` (`0..1`, step
  `0.05`, percent). Daisy controls derive key, label, and defaults from the
  catalog and use the normalized percent range. Global controls expose input
  and output gain using their existing setter range.
- Added `setSelectedBlockEnabled`, which updates the selected block and marks
  the preset dirty.
- Added `LvglUi` focus/page state and the public
  `applyFocusedParameterDelta` handoff used by subsequent renderer/app-loop
  work. `resetParameterPage` resets both focus and page when a target changes.
- Registered `pedal-lvgl-ui-smoke` only when an LVGL backend is enabled.

## TDD evidence

The first build of `pedal-lvgl-ui-smoke` failed at link time for the expected
missing `ardor::parameterPage` and `ardor::parameterPageCount` symbols. After
implementation, the target and its smoke test passed.

## Test coverage

`tests/lvgl_ui_smoke.cpp` verifies cab metadata, clamped cab writes, the block
enable setter and dirty state, global range/delegation, six-control paging, and
the two-page seven-parameter Vintage Trem descriptor.

## Verification

- `cmake --build build --target pedal-lvgl-ui-smoke pedal-ui-model-smoke`
- `ctest --test-dir build -R 'pedal-(lvgl-ui|ui-model)-smoke' --output-on-failure`
- `cmake --build build -j4`
- `ctest --test-dir build --output-on-failure`
- `git diff --check`

The full configured suite passed: 13/13 tests.

## Handoff

Task 3 should call `focusParameter`, `setParameterPage`, and
`resetParameterPage` from its touch/page/target handlers. Task 4 can route an
encoder delta through `applyFocusedParameterDelta` before retaining its
master-volume fallback.

## Review fix

### Root cause

- `resetParameterPage()` only existed as a public helper. The LVGL block and
  global target callbacks called the model transitions directly, allowing a
  focused control and nonzero page to survive a target change.
- `applyParameterDelta()` clamped its target locally. `setSelectedBlockParam()`
  clamped Daisy controls but did not enforce the cab `levelDb` or `mix` ranges,
  so direct model callers could write invalid cab parameter values.

### Changes

- Added `LvglUi::selectBlock` and `LvglUi::selectGlobalParams`; both perform
  the model transition and reset focused control/page state. The corresponding
  LVGL callbacks now use these transition methods.
- Removed the local clamp from `applyParameterDelta()`. It computes the raw
  target and writes only through `setActiveInputGainDb`, `setActiveOutputGainDb`,
  or `setSelectedBlockParam`, retaining the existing changed-result and dirty
  behavior.
- Updated `setSelectedBlockParam()` to enforce cab descriptor ranges:
  `levelDb` is `-60..12` and `mix` is `0..1`; Daisy descriptor parameters
  remain `0..1`.
- Extended `pedal-lvgl-ui-smoke` to construct `LvglUi` and verify block/global
  target changes clear focus and reset the selected page. It also verifies cab
  and Daisy setter clamping, formatted values, and dirty behavior at a limit.

### Commands and results

- `cmake --build build --target pedal-lvgl-ui-smoke` (red): failed as expected
  before implementation because `LvglUi::selectBlock` and
  `LvglUi::selectGlobalParams` did not exist.
- `cmake --build build --target pedal-lvgl-ui-smoke && ctest --test-dir build -R pedal-lvgl-ui-smoke --output-on-failure`: passed, 1/1.
- `cmake --build build --target pedal-lvgl-ui-smoke pedal-ui-model-smoke && ctest --test-dir build -R 'pedal-(lvgl-ui|ui-model)-smoke' --output-on-failure`: passed, 2/2.
- `cmake --build build -j4 && ctest --test-dir build --output-on-failure`: passed, 13/13.
- `git diff --check`: passed.
