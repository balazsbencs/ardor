# Task 5 Report: Overload Bypass Latch State

Implemented the minimal runtime latch state in `src/preset/RuntimeState.h` and `src/preset/RuntimeState.cpp`.

Behavior:
- `reportOverload()` sets the effects bypass latch.
- `reportStableCallback()` leaves the latch unchanged.
- `clearEffectsBypass()` clears the latch.
- `changePreset()` clears the latch.
- `effectsBypassed()` reports the current latch state.

Updated `tests/preset_smoke.cpp` with the requested runtime latch assertions and added `RuntimeState.cpp` to `ardor_preset` in `CMakeLists.txt`.

Verification:
- `cmake --build build`
- `ctest --test-dir build --output-on-failure -R pedal-preset-smoke`
- `ctest --test-dir build --output-on-failure`

Notes:
- I did not wire the latch into the realtime backend yet, per the task brief.
- The focused preset smoke test initially raced when run in parallel with the full suite because both runs use the same temp path; rerunning it serially passed.
