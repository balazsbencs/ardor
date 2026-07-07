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

## TDD Evidence

RED:

- Command: `cmake --build build`
- Observed failure: `CMake Error at CMakeLists.txt:53 (add_library): Cannot find source file: src/preset/Preset.cpp`
- Note: I did not capture the brief's expected header-missing failure before the CMake target was added; the actual red step I observed failed earlier because the new library referenced a missing source file.

GREEN:

- Command: `cmake --build build`
- Result: build completed successfully and produced `pedal-preset-smoke`
- Command: `ctest --test-dir build --output-on-failure -R pedal-preset-smoke`
- Result: `1/1 Test #2: pedal-preset-smoke ... Passed`
- Command: `ctest --test-dir build --output-on-failure`
- Result: `3/3 tests passed, 0 tests failed`

## Fix Follow-up

- Added a serial-only guard in `presetFromJson()` so non-`"serial"` routing now throws `std::invalid_argument`.
- Added a smoke-test assertion that `"routing": "parallel"` is rejected.
- Verification rerun:
  - `cmake --build build`
  - `ctest --test-dir build --output-on-failure -R pedal-preset-smoke`
  - `ctest --test-dir build --output-on-failure`
- Result: all commands passed.

## Write-Path Fix Follow-up

- Added the same serial-only guard to `toJson()` so invalid public `routing` values cannot be written back out.
- Added a smoke-test assertion that `toJson()` rejects a preset with `routing = "parallel"`.
- Verification rerun:
  - `cmake --build build`
  - `ctest --test-dir build --output-on-failure -R pedal-preset-smoke`
  - `ctest --test-dir build --output-on-failure`
- Result: all commands passed again.
