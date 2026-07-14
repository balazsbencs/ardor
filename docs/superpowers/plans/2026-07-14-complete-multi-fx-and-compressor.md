# Complete Multi-FX and Compressor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Do not dispatch subagents: the repository instruction requires explicit user permission.

**Goal:** Ship all 35 portable Daisy multi-fx modes and a fully parameterized native compressor as preset-editable, realtime-safe Ardor effect blocks.

**Architecture:** Copy the complete portable Daisy source closure into the hosted vendor directory and retain it without functional edits. Replace the three-mode adapter with an Ardor-owned registry whose descriptors drive preset defaults, physical UI controls, hosted DSP dispatch, and validation; add the compressor as a separate Ardor processor and expose it through the same UI catalog.

**Tech Stack:** C++20, CMake, nlohmann/json, existing `RuntimeChain`/`PedalEngine`, LVGL UI model, vendored Daisy DSP.

---

## File structure

- `third_party/daisy-multi-fx-hosted/src/`: complete portable upstream mode and DSP dependency closure; no Ardor behavior.
- `src/daisyfx/DaisyFxCatalog.h/.cpp`: all Daisy descriptor data, parameter metadata, and normalized defaults.
- `src/daisyfx/DaisyFxProcessor.h/.cpp`: registry-dispatched Daisy mode construction, mapping, reset, and sample processing.
- `src/dynamics/CompressorProcessor.h/.cpp`: native compressor configuration and sample processing.
- `src/dsp/RuntimeChain.h/.cpp`, `src/dsp/PedalEngine.h/.cpp`: compressor block support alongside existing Daisy blocks.
- `src/preset/ChainPlan.cpp`, `src/audio/EngineLoader.cpp`: validate and build `dynamics/compressor` blocks.
- `src/ui/ParameterControls.h/.cpp`, `src/ui/UiModel.h/.cpp`: schema-driven continuous, choice, and toggle controls; catalog-backed asset list/default migration.
- `tests/daisy_fx_catalog_smoke.cpp`, `tests/daisy_fx_smoke.cpp`, `tests/runtime_chain_smoke.cpp`, `tests/ui_model_smoke.cpp`: full registry, DSP, chain, and UI coverage.
- `tests/compressor_smoke.cpp`, `tests/dsp_bench.cpp`: compressor behavior and full-effect realtime measurement.

### Task 1: Establish the test surface and import the portable source closure

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `third_party/daisy-multi-fx-hosted/README.md`
- Create/Copy: all missing portable files below `third_party/daisy-multi-fx-hosted/src/`
- Modify: `tests/daisy_fx_catalog_smoke.cpp`

- [ ] **Step 1: Write the failing complete-catalog test**

  Replace the three-entry expectation with the final count and stable identifier checks:

  ```cpp
  require(ardor::daisyFxCatalog().size() == 35, "all Daisy modes are cataloged");
  for (const auto& effect : ardor::daisyFxCatalog()) {
    require(!effect.blockType.empty() && !effect.mode.empty(), "effect has identifier");
    require(!effect.name.empty() && !effect.params.empty(), "effect has editable schema");
    require(ardor::findDaisyFxDescriptor(effect.blockType, effect.mode) == &effect,
            "catalog identifier resolves");
  }
  ```

- [ ] **Step 2: Run the test and verify that it fails for the current three-mode catalog**

  Run: `cmake --build build --target pedal-daisy-fx-catalog-smoke && ./build/pedal-daisy-fx-catalog-smoke`

  Expected: nonzero exit with `all Daisy modes are cataloged`.

- [ ] **Step 3: Copy the upstream portable dependency closure without functional edits**

  Copy all `.h`/`.cpp` files required by the 35 upstream mode headers: every file in
  `src/modes/`, the three parameter families, `audio/stereo_frame.h`, mode-id/config
  files, and the required portable DSP files (`allpass_filter`, `band_shifter`,
  `bbd_emulator`, `envelope_follower`, `fast_sqrt`, `formant_filter`,
  `hilbert_transform`, `multirate`, `octave_generator`, `pattern_sequencer`,
  `pitch_shifter`, `saturation`, `svf`, `waveshaper`, and existing dependencies).
  Do not copy hardware, display, MIDI, tempo, QSPI preset, or application files.

- [ ] **Step 4: Extend the vendor library source list**

  Add every copied `.cpp` to `ardor_daisyfx_vendor`. Keep the hosted compatibility
  include before the copied source include:

  ```cmake
  target_include_directories(ardor_daisyfx_vendor PUBLIC
    third_party/daisy-multi-fx-hosted/compat
    third_party/daisy-multi-fx-hosted/src
  )
  ```

  Update the vendor README with the import date and exact complete file list.

- [ ] **Step 5: Build the vendor target**

  Run: `cmake --build build --target ardor_daisyfx_vendor`

  Expected: exit 0 with no JUCE, libDaisy, DaisySP, CMSIS, or q dependency.

- [ ] **Step 6: Commit the isolated vendor change**

  ```bash
  git add CMakeLists.txt third_party/daisy-multi-fx-hosted tests/daisy_fx_catalog_smoke.cpp
  git commit -m "build: import complete hosted daisy fx source"
  ```

### Task 2: Define complete Daisy parameter schemas and defaults

**Files:**
- Modify: `src/daisyfx/DaisyFxCatalog.h`
- Modify: `src/daisyfx/DaisyFxCatalog.cpp`
- Modify: `tests/daisy_fx_catalog_smoke.cpp`

- [ ] **Step 1: Write failing schema assertions for semantic labels and physical display data**

  Add assertions such as:

  ```cpp
  const auto* shimmer = ardor::findDaisyFxDescriptor("reverb", "shimmer");
  require(shimmer != nullptr, "shimmer is cataloged");
  require(shimmer->params[5].label == "Pitch 1", "shimmer param one is semantic");
  require(shimmer->params[5].unit == ardor::DaisyFxParamUnit::Semitones,
          "shimmer pitch has semitone display");
  require(shimmer->params[5].step > 0.0f, "parameter has a usable UI step");
  ```

- [ ] **Step 2: Verify RED**

  Run: `cmake --build build --target pedal-daisy-fx-catalog-smoke && ./build/pedal-daisy-fx-catalog-smoke`

  Expected: compile failure because `unit` and the new display metadata do not yet exist.

- [ ] **Step 3: Extend the descriptor API minimally**

  Add physical display metadata while preserving normalized JSON values:

  ```cpp
  enum class DaisyFxParamUnit { Percent, Hertz, Milliseconds, Seconds, Decibels, Semitones, Plain };

  struct DaisyFxParamDescriptor {
    std::string key;
    std::string label;
    float defaultValue = 0.0f;
    float step = 0.05f;
    DaisyFxParamUnit unit = DaisyFxParamUnit::Percent;
  };
  ```

  Add entries for every mode ID in the upstream enums. Use the upstream
  `get_param_range` and reverb algorithm descriptor tables to assign meaningful
  labels for `p1`/`p2` and to choose appropriate display units. Keep the seven
  normalized fields in each category so existing upstream parameter mapping
  remains authoritative.

- [ ] **Step 4: Verify GREEN**

  Run: `cmake --build build --target pedal-daisy-fx-catalog-smoke && ./build/pedal-daisy-fx-catalog-smoke`

  Expected: exit 0.

- [ ] **Step 5: Commit the catalog slice**

  ```bash
  git add src/daisyfx/DaisyFxCatalog.h src/daisyfx/DaisyFxCatalog.cpp tests/daisy_fx_catalog_smoke.cpp
  git commit -m "feat: catalog all hosted daisy effects"
  ```

### Task 3: Generalize Daisy processor dispatch for all modulation effects

**Files:**
- Modify: `src/daisyfx/DaisyFxProcessor.cpp`
- Modify: `tests/daisy_fx_smoke.cpp`

- [ ] **Step 1: Write a failing modulation-loop test**

  ```cpp
  for (const auto& descriptor : ardor::daisyFxCatalog()) {
    if (descriptor.blockType != "mod") continue;
    ardor::DaisyFxProcessor processor;
    std::string error;
    require(processor.configure(descriptor.blockType, ardor::defaultDaisyFxParams(descriptor),
                                48000.0f, error), error);
    const auto out = processor.process({0.5f, 0.5f});
    require(std::isfinite(out.left) && std::isfinite(out.right), "mod output finite");
  }
  ```

- [ ] **Step 2: Verify RED**

  Run: `cmake --build build --target pedal-daisy-fx-smoke && ./build/pedal-daisy-fx-smoke`

  Expected: failure on `mod/chorus`, because only Vintage Trem is dispatched.

- [ ] **Step 3: Add mode-ID mapping and modulation dispatch**

  Add one Ardor-owned mapping from catalog mode strings to `pedal::ModModeId` and
  dispatch every mode with its concrete upstream class. For each configured
  mode, map `speed`, `depth`, `mix`, `tone`, `p1`, `p2`, and `level` with
  `pedal::mod_fx::get_param_range(modeId, parameterId)`, call `Init()` and
  `Prepare()` before publishing the processor, call `Process()` per sample, and
  call `Reset()` when the runtime chain resets. Do not use upstream singleton
  registries.

- [ ] **Step 4: Verify GREEN**

  Run: `cmake --build build --target pedal-daisy-fx-smoke && ./build/pedal-daisy-fx-smoke`

  Expected: exit 0 for all 13 modulation modes.

- [ ] **Step 5: Commit**

  ```bash
  git add src/daisyfx/DaisyFxProcessor.cpp tests/daisy_fx_smoke.cpp
  git commit -m "feat: host all daisy modulation modes"
  ```

### Task 4: Add all delay and reverb processor dispatch

**Files:**
- Modify: `src/daisyfx/DaisyFxProcessor.cpp`
- Modify: `tests/daisy_fx_smoke.cpp`

- [ ] **Step 1: Write failing delay/reverb coverage tests**

  Extend the same catalog loop to process every `delay` and `reverb` descriptor.
  Feed an impulse followed by 8192 silent samples and require finite stereo
  output plus at least one nonzero wet sample at `mix = 1.0`.

- [ ] **Step 2: Verify RED**

  Run: `cmake --build build --target pedal-daisy-fx-smoke && ./build/pedal-daisy-fx-smoke`

  Expected: first failure at `delay/tape`, which the current adapter rejects.

- [ ] **Step 3: Implement complete delay and reverb dispatch**

  Map every catalog ID to `DelayModeId` or `ReverbModeId`; map the seven shared
  normalized fields through the matching upstream parameter map; construct,
  initialize, prepare, process, and reset its concrete class. Retain the
  existing dry/wet convention and never allocate or inspect JSON in `process`.
  Unsupported identifiers must retain the existing `unsupported Daisy effect:
  type/mode` error.

- [ ] **Step 4: Verify GREEN**

  Run: `cmake --build build --target pedal-daisy-fx-smoke && ./build/pedal-daisy-fx-smoke`

  Expected: exit 0 for every 35-mode descriptor.

- [ ] **Step 5: Commit**

  ```bash
  git add src/daisyfx/DaisyFxProcessor.cpp tests/daisy_fx_smoke.cpp
  git commit -m "feat: host all daisy delay and reverb modes"
  ```

### Task 5: Implement the native compressor with safe configuration

**Files:**
- Create: `src/dynamics/CompressorProcessor.h`
- Create: `src/dynamics/CompressorProcessor.cpp`
- Create: `tests/compressor_smoke.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the failing compressor contract test**

  ```cpp
  ardor::CompressorProcessor compressor;
  std::string error;
  require(compressor.configure({{"threshold_db", -24.0f}, {"ratio", 8.0f},
                                {"attack_ms", 1.0f}, {"release_ms", 100.0f},
                                {"knee_db", 6.0f}, {"makeup_db", 0.0f},
                                {"input_gain_db", 0.0f}, {"mix", 1.0f},
                                {"sidechain_hpf_hz", 80.0f}, {"detector", "peak"},
                                {"auto_makeup", false}}, 48000.0f, error), error);
  const float compressed = renderSteady(compressor, 1.0f, 48000);
  require(compressed < 0.7f, "compressor reduces above-threshold steady signal");
  ```

- [ ] **Step 2: Verify RED**

  Run: `cmake --build build --target pedal-compressor-smoke`

  Expected: configure/build failure because the processor and test target do not exist.

- [ ] **Step 3: Add configuration, coefficient preparation, and processing**

  Define a `CompressorProcessor` with `configure(const nlohmann::json&, float,
  std::string&)`, `reset()`, and `process(StereoSample)`. At configure time,
  clamp all continuous values to the design ranges, accept only `"peak"` and
  `"rms"` detector strings, derive attack/release smoothing coefficients and
  the sidechain high-pass coefficient, and reject non-finite sample rates.
  At process time, apply input gain, update the selected detector, calculate
  soft-knee gain reduction, smooth gain, apply makeup/auto-makeup, then combine
  compressed and dry signals by `mix`. Keep independent detector/gain state per
  channel and no heap allocation.

- [ ] **Step 4: Extend the test before adding optional controls**

  Add explicit checks for `ratio=1` near-unity gain, slower attack passing a
  transient more than fast attack, release recovery, wet/dry mix, RMS versus
  peak behavior, sidechain HPF reducing low-frequency pumping, reset
  determinism, malformed detector rejection, and finite output after all
  boundary values.

- [ ] **Step 5: Verify GREEN**

  Run: `cmake --build build --target pedal-compressor-smoke && ./build/pedal-compressor-smoke`

  Expected: exit 0.

- [ ] **Step 6: Commit**

  ```bash
  git add CMakeLists.txt src/dynamics tests/compressor_smoke.cpp
  git commit -m "feat: add parameterized compressor processor"
  ```

### Task 6: Wire the compressor into chain planning and runtime construction

**Files:**
- Modify: `src/dsp/RuntimeChain.h`
- Modify: `src/dsp/RuntimeChain.cpp`
- Modify: `src/dsp/PedalEngine.h`
- Modify: `src/dsp/PedalEngine.cpp`
- Modify: `src/preset/ChainPlan.cpp`
- Modify: `src/audio/EngineLoader.cpp`
- Modify: `tests/preset_smoke.cpp`
- Modify: `tests/runtime_chain_smoke.cpp`

- [ ] **Step 1: Write failing preset/runtime compressor tests**

  Add a preset block and expectations:

  ```cpp
  chainPreset.blocks.push_back({"comp", "dynamics", true, "", {
    {"mode", "compressor"}, {"threshold_db", -24.0f}, {"ratio", 4.0f}
  }});
  require(plan.blocks.back().status == ardor::ChainBlockStatus::Ready, "compressor block ready");
  ```

  Configure a `PedalEngine` compressor and require its steady output changes
  when effects are enabled and returns to dry behavior when effects are bypassed.

- [ ] **Step 2: Verify RED**

  Run: `cmake --build build --target pedal-preset-smoke pedal-runtime-chain-smoke && ./build/pedal-preset-smoke && ./build/pedal-runtime-chain-smoke`

  Expected: the dynamics block is unsupported and the new engine entry point is absent.

- [ ] **Step 3: Add the minimum runtime API**

  Add `RuntimeChain::addCompressor(CompressorProcessor)`, store it as a distinct
  runtime block kind, and dispatch `process`/`reset`. Add
  `PedalEngine::addCompressor(const nlohmann::json&, float, std::string&)`.
  Recognize only `type == "dynamics"` with `mode == "compressor"` in
  `ChainPlan`, and have `EngineLoader` call the new engine method. All other
  dynamics modes remain unsupported with a descriptive validation error.

- [ ] **Step 4: Verify GREEN**

  Run: `cmake --build build --target pedal-preset-smoke pedal-runtime-chain-smoke && ./build/pedal-preset-smoke && ./build/pedal-runtime-chain-smoke`

  Expected: both programs exit 0.

- [ ] **Step 5: Commit**

  ```bash
  git add src/dsp src/preset/ChainPlan.cpp src/audio/EngineLoader.cpp tests/preset_smoke.cpp tests/runtime_chain_smoke.cpp
  git commit -m "feat: add compressor to runtime chains"
  ```

### Task 7: Make the UI catalog and parameter drawer schema-driven for all effects

**Files:**
- Modify: `src/ui/UiModel.h`
- Modify: `src/ui/UiModel.cpp`
- Modify: `src/ui/ParameterControls.h`
- Modify: `src/ui/ParameterControls.cpp`
- Modify: `tests/ui_model_smoke.cpp`

- [ ] **Step 1: Write failing UI/default-migration tests**

  Add tests that locate `Shimmer`, `Tape Delay`, `Auto Swell`, and `Compressor`
  in `state.assets`; insert each; assert it gets a complete default parameter
  object; and load a historical `reverb/room` JSON containing only `mode` to
  confirm all seven fields are defaulted. Assert compressor controls render
  labels `Threshold`, `Ratio`, `Attack`, `Release`, `Knee`, `Makeup`, `Input`,
  `Mix`, `Sidechain HPF`, `Detector`, and `Auto Makeup`.

- [ ] **Step 2: Verify RED**

  Run: `cmake --build build --target pedal-ui-model-smoke && ./build/pedal-ui-model-smoke`

  Expected: failure because only the three existing Daisy assets are available
  and dynamics has no parameters.

- [ ] **Step 3: Add choice/toggle-aware parameter controls**

  Extend `ParameterControl` with a value kind and display choices so the current
  knob interaction can increment a numeric control, cycle `peak`/`rms`, and
  toggle a boolean. Keep `setSelectedBlockParam` for continuous controls and add
  a JSON-value setter for enum/toggle controls. Add a descriptor/default helper
  that merges known defaults into a loaded block without deleting unknown future
  keys.

- [ ] **Step 4: Generate complete assets and semantic controls**

  Build the assets list from all 35 catalog descriptors plus
  `dynamics/compressor`. Format Daisy values from their upstream physical range
  using `unmap/map`-consistent conversion and parameter units; format compressor
  values in native dB, milliseconds, Hz, ratio, percent, or named choices.
  Preserve seven-control paging behavior.

- [ ] **Step 5: Verify GREEN**

  Run: `cmake --build build --target pedal-ui-model-smoke && ./build/pedal-ui-model-smoke`

  Expected: exit 0.

- [ ] **Step 6: Commit**

  ```bash
  git add src/ui tests/ui_model_smoke.cpp
  git commit -m "feat: expose complete effect parameters in ui"
  ```

### Task 8: Add full-registry performance coverage and run final verification

**Files:**
- Modify: `tests/dsp_bench.cpp`
- Modify: `docs/hardware-validation.md`

- [ ] **Step 1: Write a failing benchmark enumeration assertion**

  In the benchmark setup, require the number of measured effects to equal
  `daisyFxCatalog().size() + 1` after adding the compressor. Print each stable
  `type/mode` key, average microseconds per 64-frame block, and percentage of
  the `1333 µs` budget.

- [ ] **Step 2: Verify RED**

  Run: `cmake --build build --target pedal-dsp-bench && ./build/pedal-dsp-bench`

  Expected: failure because the benchmark still only measures NAM/IR components.

- [ ] **Step 3: Add effect benchmark enumeration**

  Configure each descriptor with defaults and render a warm-up followed by a
  fixed number of 64-frame blocks. Measure the compressor with its defaults.
  Keep timing output informational; do not make host-specific thresholds a CTest
  pass/fail condition. Add an empty Pi measurement table to
  `docs/hardware-validation.md` with a row for every effect key.

- [ ] **Step 4: Verify the benchmark**

  Run: `cmake --build build --target pedal-dsp-bench && ./build/pedal-dsp-bench`

  Expected: one timing line for every Daisy mode and `dynamics/compressor`.

- [ ] **Step 5: Run the full desktop verification suite**

  Run: `cmake --build build && ctest --test-dir build --output-on-failure && git diff --check`

  Expected: build exit 0, all CTest tests pass, and no whitespace errors.

- [ ] **Step 6: Run the Pi-oriented configuration verification**

  Run: `cmake -S . -B build-pi-check -DARDOR_UI_BACKEND=fbdev && cmake --build build-pi-check --target pedal-daisy-fx-smoke pedal-compressor-smoke pedal-preset-smoke`

  Expected: all requested targets build. On the hardware, run `pedal-dsp-bench`
  at `48 kHz / 64 frames` under the `performance` governor and record results in
  `docs/hardware-validation.md` before enabling a mode in a production preset.

- [ ] **Step 7: Commit verification support**

  ```bash
  git add tests/dsp_bench.cpp docs/hardware-validation.md
  git commit -m "test: benchmark complete effects registry"
  ```

## Plan self-review

- **Spec coverage:** Tasks 1-4 cover all 35 portable Daisy modes and their
  parameter schemas; Tasks 5-6 cover the native compressor and runtime loading;
  Task 7 covers UI/preset defaults and controls; Task 8 covers desktop and Pi
  validation.
- **No realtime violations:** configuration, mapping, and chain construction
  occur outside `process`; all per-sample code uses precomputed state only.
- **Compatibility:** existing normalized Daisy preset keys stay valid; old
  presets receive missing descriptor defaults; malformed or unknown effect modes
  remain isolated to their own unsupported block.
