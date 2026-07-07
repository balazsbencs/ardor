# Task 4 Report: Master Volume And Dry Bypass Engine Hooks

## Scope

Implemented the minimal `PedalEngine` hooks requested in the task brief:

- Added `setMasterVolume(float gain)`
- Added `setEffectsBypassed(bool bypassed)`
- Applied master volume after either the wet chain or dry bypass path
- Kept safety limiting as the final stage in both sample and block processing
- Added and registered the engine contract smoke test

No realtime backend behavior was changed.

## TDD Evidence

Red:

- Added `tests/engine_contract_smoke.cpp`
- Registered `pedal-engine-contract-smoke` in `CMakeLists.txt`
- Ran `cmake --build build`
- Build failed as expected because `PedalEngine::setMasterVolume` and `PedalEngine::setEffectsBypassed` did not exist

Green:

- Added the two public engine hooks and backing fields
- Updated `process()` to route both wet and dry-bypass output through master volume, then safety limiting
- Updated `processBlock()` to do the same for bypassed and non-bypassed block paths

## Files Changed

- `CMakeLists.txt`
- `src/dsp/PedalEngine.h`
- `src/dsp/PedalEngine.cpp`
- `tests/engine_contract_smoke.cpp`

## Verification

Focused test:

```sh
ctest --test-dir build --output-on-failure -R pedal-engine-contract-smoke
```

Result:

- `pedal-engine-contract-smoke` passed

Full suite:

```sh
ctest --test-dir build --output-on-failure
```

Result:

- 4/4 tests passed:
  - `pedal-offline-smoke`
  - `pedal-preset-smoke`
  - `pedal-engine-contract-smoke`
  - `pedal-devices-indexed`

## Notes

- The build emitted an existing linker warning about `/usr/local/opt/llvm/lib` not being found on this machine, but the build completed successfully and all tests passed.
