# Final review fixes

## What changed

- Hardened `PresetStore::save()` to keep the existing temp-then-rename flow while writing the temp file in the destination directory, checking write state, flushing, closing, and only renaming after success.
- Made `PedalEngine` control-facing values read by audio processing atomic, and moved bypass-triggered DSP reset into audio processing via an atomic reset request checked at the start of `process()` and `processBlock()`.
- Added preset asset validation so non-empty block assets must stay relative to the data root, rejecting absolute paths and `..` traversal during JSON load/save, while `ChainPlan` now treats invalid programmatic assets as `MissingAsset`.
- Changed `RuntimeState` overload latching to require 3 consecutive overload reports; stable callbacks reset the pre-latch streak only, while `clearEffectsBypass()` and `changePreset()` clear both the latch and the streak.
- Expanded smoke coverage for invalid asset paths, disabled chain blocks, and the three-strike overload policy; also converted the touched smoke tests away from `assert(...)` so they still execute in this `Release` build.
- Added a short preset storage section to `README.md` and made the mockup’s top-bar `Clear Bypass` button clear only, without toggling bypass back on.

## Verification

### `cmake --build build`

```text
[100%] Built target pedal-engine-contract-smoke
```

### `ctest --test-dir build --output-on-failure`

```text
100% tests passed, 0 tests failed out of 4
```

### `test -f mockups/preset-ui/index.html`

```text
exit 0
```

### `git status --short --branch`

```text
## codex/preset-ui-architecture
 M README.md
 M mockups/preset-ui/index.html
 M src/dsp/PedalEngine.cpp
 M src/dsp/PedalEngine.h
 M src/preset/ChainPlan.cpp
 M src/preset/Preset.cpp
 M src/preset/Preset.h
 M src/preset/PresetStore.cpp
 M src/preset/RuntimeState.cpp
 M src/preset/RuntimeState.h
 M tests/engine_contract_smoke.cpp
 M tests/preset_smoke.cpp
```
