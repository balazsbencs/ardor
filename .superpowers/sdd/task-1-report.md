# Task 1 Report: Preset JSON Model

Implemented the preset JSON model library and the smoke test from the task brief.

## Files changed

- `src/preset/Preset.h`
- `src/preset/Preset.cpp`
- `tests/preset_smoke.cpp`
- `CMakeLists.txt`

## What changed

- Added `ardor::PresetGlobal`, `ardor::PresetBlock`, and `ardor::Preset`.
- Added `ardor::toJson(const Preset&)` and `ardor::presetFromJson(const nlohmann::json&)`.
- Added the preset smoke test covering parse and round-trip behavior.
- Registered the new `ardor_preset` library and `pedal-preset-smoke` test in CMake.

## Verification

- `cmake --build build`
- `ctest --test-dir build --output-on-failure -R pedal-preset-smoke`
- `ctest --test-dir build --output-on-failure`

All checks passed.
