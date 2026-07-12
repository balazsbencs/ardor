# Final review fixes

## Scope

Addressed every Important finding from the whole-branch review on
`feat/lvgl-flat-knob-redesign`.

## Changes

- Replaced the renderer's functional accent with exact `#43F05A`.
- Kept `PAGE n / total` visible for every parameter panel, including
  single-page global and cab panels; page arrows render only when more than
  one page exists.
- Made focused knobs persist visibly: their labels render acid green, and a
  focus change schedules an immediate UI rebuild.
- Centralized preset selection in `LvglUi::selectPreset`, which always clears
  focused parameter/page state after either the default model selection or a
  custom `UiActions::selectPreset` callback.
- Replaced chain slot-width division with bounded, proportional position
  mapping. Rendering uses at least one pixel per slot and clamps overflowed
  visual positions; reorder and insertion map the chain span proportionally,
  so counts larger than the available pixels remain deterministic and cannot
  divide by zero.
- Added a thin acid-green bar to the active preset tile in addition to its
  green text.
- Removed the extra blank line at EOF from both generated Open Sans C sources.

## Regression coverage

`pedal-lvgl-ui-smoke` now verifies:

- focused knob label color;
- `PAGE 1 / 1` for global and cab controls;
- reset of focus/page through a custom preset-selection action;
- presence of the active-preset indicator;
- safe first/last/reorder/insertion mapping and bounded indicator placement
  for a 10,000-block chain.

## Verification

- `cmake --build build --target pedal-lvgl-ui-smoke pedal-ui-model-smoke pedal-control-smoke`
- `ctest --test-dir build -R 'pedal-(lvgl-ui|ui-model|control)-smoke' --output-on-failure` — 3/3 passed.
- `cmake --build build -j4`
- `ctest --test-dir build --output-on-failure` — 13/13 passed.
- `git diff --check` — passed.
- `rg -n '0xb6ff00|b6ff00' src tests apps` — no matches.

The build emits pre-existing linker warnings about duplicate LVGL libraries and
an absent `/usr/local/opt/llvm/lib` search path, but exits successfully and
all tests pass.
