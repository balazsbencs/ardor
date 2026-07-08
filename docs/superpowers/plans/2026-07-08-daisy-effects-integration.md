# Daisy Effects Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reuse the good-sounding Daisy Seed multi-fx DSP as editable pedal blocks in the Pi pedal platform, with saved parameters, UI drawer controls, and realtime audio processing.

**Architecture:** Vendor only the portable Daisy DSP, params, and selected modes. Do not import the Daisy hardware audio engine, controls, display, MIDI, QSPI preset store, or pin map. Add a small hosted adapter that turns Daisy modes into normal preset blocks: `mod`, `delay`, and `reverb`.

**Tech Stack:** C++20, CMake, existing preset JSON, LVGL simulator, current `PedalEngine`, and copied MIT-licensed Daisy multi-fx source from `/Users/bbalazs/daisy/multi-fx/src`.

## Global Constraints

- Keep one serial chain for v1.
- Effect blocks remain freely reorderable in the saved preset chain.
- Store Daisy effect parameters as normalized `0.0..1.0` values in preset JSON.
- Keep NAM and cab asset paths unchanged.
- Daisy `mod`, `delay`, and `reverb` blocks do not use asset files.
- Do not allocate, parse JSON, load assets, or create effect objects in the realtime callback.
- Keep realtime baseline at `48000 Hz`, `--block-size 64`, `--ir-samples 8192`.
- Keep unsupported or overloaded chains latched into bypass as already planned.
- Do not bring in JUCE, libDaisy, DaisySP, CMSIS, or Daisy hardware code.
- Do not import q-based poly-octave code in the first slice.

---

## Execution Position

**Deferred until after Pi validation.** The master roadmap (`docs/superpowers/plans/2026-07-08-master-implementation-roadmap.md` § Deferred Until After Pi Validation) is authoritative and supersedes the earlier version of this section, which asked to run before the Buildroot target. Rationale: this plan is a large CPU unknown (three new DSP block types), and landing it before the NAM baseline is validated on the Cortex-A72 would make Pi overruns undiagnosable — is it NAM, the convolver, or Daisy?

When this plan does run:

1. All roadmap phases 0–7 are complete and the Pi baseline is measured.
2. Task 8's headroom rule (>30%) is measured **on the Pi 4B with the `performance` governor**, not on macOS. macOS numbers do not transfer (roughly 5–10× per-core difference).
3. The v1 image ships without these effects; they arrive in a post-v1 image.

---

## Daisy Source Findings

- `/Users/bbalazs/daisy/multi-fx/src/audio/audio_engine.*` is Daisy hardware-bound. Leave it out.
- `/Users/bbalazs/daisy/multi-fx/src/audio/stereo_frame.h` is portable and useful.
- `/Users/bbalazs/daisy/multi-fx/src/params/*` is portable and already defines parameter sets and mappings.
- `/Users/bbalazs/daisy/multi-fx/src/modes/*` contains the wanted modulation, delay, and reverb modes.
- `/Users/bbalazs/daisy/multi-fx/src/dsp/*` is mostly portable. Some files include `daisy_seed.h` only for `DSY_SDRAM_BSS`.
- `/Users/bbalazs/daisy/multi-fx/juce_plugin/Source/daisy_seed.h` already proves the hosted shim approach with `#define DSY_SDRAM_BSS`.
- `/Users/bbalazs/daisy/multi-fx/README.md` states application code in `src/` follows MIT terms unless stated otherwise.

---

## Preset JSON Contract

NAM and cab blocks stay as they are:

```json
{
  "id": "amp-1",
  "type": "nam",
  "enabled": true,
  "asset": "models/clean.nam",
  "params": {}
}
```

Daisy blocks use no asset path and store normalized parameters:

```json
{
  "id": "mod-1",
  "type": "mod",
  "enabled": true,
  "asset": "",
  "params": {
    "mode": "vintage_trem",
    "speed": 0.35,
    "depth": 0.70,
    "mix": 1.0,
    "tone": 0.50,
    "p1": 0.0,
    "p2": 0.0,
    "level": 0.50
  }
}
```

Delay:

```json
{
  "id": "delay-1",
  "type": "delay",
  "enabled": true,
  "asset": "",
  "params": {
    "mode": "digital",
    "time": 0.25,
    "repeats": 0.35,
    "mix": 0.25,
    "filter": 0.50,
    "grit": 0.0,
    "mod_spd": 0.0,
    "mod_dep": 0.0
  }
}
```

Reverb:

```json
{
  "id": "reverb-1",
  "type": "reverb",
  "enabled": true,
  "asset": "",
  "params": {
    "mode": "room",
    "decay": 0.45,
    "pre_delay": 0.15,
    "mix": 0.25,
    "tone": 0.50,
    "mod": 0.0,
    "param1": 0.50,
    "param2": 0.50
  }
}
```

---

## File Structure

- `third_party/daisy-multi-fx-hosted/LICENSE`: copied upstream license.
- `third_party/daisy-multi-fx-hosted/README.md`: short note describing the source path, import date, and files copied.
- `third_party/daisy-multi-fx-hosted/compat/daisy_seed.h`: hosted shim with `#define DSY_SDRAM_BSS`.
- `third_party/daisy-multi-fx-hosted/src/`: copied portable Daisy source tree.
- `src/daisyfx/DaisyFxCatalog.h/.cpp`: block descriptors, mode names, parameter schemas, and defaults.
- `src/daisyfx/DaisyFxProcessor.h/.cpp`: Ardor-owned adapter around Daisy modes.
- `src/dsp/RuntimeChain.h/.cpp`: small serial block runner used by `PedalEngine`.
- `src/preset/ChainPlan.h/.cpp`: recognize `mod`, `delay`, and `reverb` as supported no-asset blocks.
- `src/audio/EngineLoader.cpp`: create NAM, cab, and Daisy runtime blocks from `ChainPlan`.
- `src/ui/UiModel.h/.cpp`: keep block params and expose catalog descriptors to the UI.
- `src/ui/LvglUi.cpp`: populate block drawer from the catalog and render schema-driven param controls.
- `tests/daisy_fx_smoke.cpp`: DSP adapter smoke tests.
- `tests/runtime_chain_smoke.cpp`: chain ordering and bypass smoke tests.
- `tests/ui_model_smoke.cpp`: Daisy params save/load round-trip.
- `README.md`: developer notes for enabling Daisy effects.

---

## Task 1: Vendor The Minimal Hosted Daisy Source

**Files:**
- Create: `third_party/daisy-multi-fx-hosted/LICENSE`
- Create: `third_party/daisy-multi-fx-hosted/README.md`
- Create: `third_party/daisy-multi-fx-hosted/compat/daisy_seed.h`
- Copy selected files under: `third_party/daisy-multi-fx-hosted/src/`
- Modify: `CMakeLists.txt`

**Imported first-slice files:**
- `audio/stereo_frame.h`
- `config/constants.h`
- `config/mod_mode_id.h`
- `params/param_range.h`
- `params/mod_param_id.h`
- `params/mod_param_set.h`
- `params/mod_param_set.cpp`
- `params/mod_param_map.h`
- `params/mod_param_map.cpp`
- `dsp/fast_math.h`
- `dsp/lfo.h`
- `dsp/lfo.cpp`
- `modes/mod_mode.h`
- `modes/vintage_trem_mode.h`
- `modes/vintage_trem_mode.cpp`

**Hosted shim:**

```cpp
#pragma once

#define DSY_SDRAM_BSS
```

- [ ] Copy the upstream MIT license into `third_party/daisy-multi-fx-hosted/LICENSE`.
- [ ] Add `third_party/daisy-multi-fx-hosted/README.md` with the source path and copied file list.
- [ ] Copy the first-slice source files without functional edits.
- [ ] Add the compat include directory before the copied `src` include directory in CMake.
- [ ] Create an `ardor_daisyfx_vendor` library target with only the first-slice `.cpp` files.
- [ ] Build `ardor_daisyfx_vendor` on macOS.
- [ ] Commit with:

```bash
git add third_party/daisy-multi-fx-hosted CMakeLists.txt
git commit -m "build: vendor hosted daisy fx slice"
```

**Gate:**

```bash
cmake --build build --target ardor_daisyfx_vendor
git diff --check
```

Expected: the vendor library builds without JUCE, libDaisy, DaisySP, CMSIS, or q include paths.

---

## Task 2: Add The Daisy Effect Catalog

**Files:**
- Create: `src/daisyfx/DaisyFxCatalog.h`
- Create: `src/daisyfx/DaisyFxCatalog.cpp`
- Create: `tests/daisy_fx_catalog_smoke.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

```cpp
namespace ardor {

enum class DaisyFxKind {
  Mod,
  Delay,
  Reverb
};

struct DaisyFxParamDescriptor {
  std::string key;
  std::string label;
  float defaultValue = 0.0f;
};

struct DaisyFxDescriptor {
  DaisyFxKind kind = DaisyFxKind::Mod;
  std::string blockType;
  std::string mode;
  std::string name;
  std::vector<DaisyFxParamDescriptor> params;
};

const std::vector<DaisyFxDescriptor>& daisyFxCatalog();
const DaisyFxDescriptor* findDaisyFxDescriptor(std::string_view blockType, std::string_view mode);
nlohmann::json defaultDaisyFxParams(const DaisyFxDescriptor& descriptor);

} // namespace ardor
```

**First catalog entry:**

- Type: `mod`
- Mode: `vintage_trem`
- Name: `Vintage Trem`
- Params: `speed`, `depth`, `mix`, `tone`, `p1`, `p2`, `level`

- [ ] Add the catalog with the first `Vintage Trem` descriptor.
- [ ] Add defaults that match a useful audible but not overloaded preset.
- [ ] Add a smoke test that verifies descriptor lookup and JSON defaults.
- [ ] Commit with:

```bash
git add src/daisyfx tests/daisy_fx_catalog_smoke.cpp CMakeLists.txt
git commit -m "feat: add daisy fx catalog"
```

**Gate:**

```bash
cmake --build build --target pedal-daisy-fx-catalog-smoke
./build/pedal-daisy-fx-catalog-smoke
git diff --check
```

Expected: catalog lookup returns `Vintage Trem`, and default params contain every required mod key.

---

## Task 3: Add A Hosted Daisy Processor Adapter

**Files:**
- Create: `src/daisyfx/DaisyFxProcessor.h`
- Create: `src/daisyfx/DaisyFxProcessor.cpp`
- Create: `tests/daisy_fx_smoke.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

```cpp
namespace ardor {

struct StereoSample {
  float left = 0.0f;
  float right = 0.0f;
};

class DaisyFxProcessor {
public:
  bool configure(const std::string& blockType,
                 const nlohmann::json& params,
                 float sampleRate,
                 std::string& error);
  void reset();
  StereoSample process(StereoSample input);

private:
  // owns one selected Daisy mode
};

} // namespace ardor
```

- [ ] Implement `mod/vintage_trem` only.
- [ ] Convert JSON params into `mod_fx::ParamSet`.
- [ ] Clamp missing or invalid numeric params to descriptor defaults.
- [ ] Reject unknown `blockType` or unknown `mode` with a clear error string.
- [ ] Add a smoke test proving `Vintage Trem` changes a steady stereo signal when depth is high.
- [ ] Add a reset determinism check: same input after reset produces the same first block of output.
- [ ] Commit with:

```bash
git add src/daisyfx tests/daisy_fx_smoke.cpp CMakeLists.txt
git commit -m "feat: host daisy modulation processor"
```

**Gate:**

```bash
cmake --build build --target pedal-daisy-fx-smoke
./build/pedal-daisy-fx-smoke
git diff --check
```

Expected: the hosted processor produces finite stereo output, does not clip normal input by default, and rejects unknown modes cleanly.

---

## Task 4: Make ChainPlan Accept Daisy No-Asset Blocks

**Files:**
- Modify: `src/preset/ChainPlan.cpp`
- Modify: `tests/preset_smoke.cpp`

**Behavior:**

- `nam` and `cab` still require valid asset paths.
- `mod`, `delay`, and `reverb` are supported block types.
- `mod`, `delay`, and `reverb` with empty asset paths are valid.
- `mod`, `delay`, and `reverb` must have a supported `params.mode`.
- Unsupported Daisy modes produce `ChainBlockStatus::Unsupported`.

- [ ] Add failing preset smoke coverage for a ready `mod/vintage_trem` block with no asset.
- [ ] Add failing preset smoke coverage for an unsupported Daisy mode.
- [ ] Update supported block validation to use `DaisyFxCatalog`.
- [ ] Keep disabled Daisy blocks as `Disabled`.
- [ ] Commit with:

```bash
git add src/preset/ChainPlan.cpp tests/preset_smoke.cpp
git commit -m "feat: accept daisy preset blocks"
```

**Gate:**

```bash
cmake --build build --target pedal-preset-smoke
./build/pedal-preset-smoke
git diff --check
```

Expected: Daisy blocks are first-class chain-plan entries without weakening asset validation for NAM and cab blocks.

---

## Task 5: Add A Minimal Runtime Chain Runner

**Files:**
- Create: `src/dsp/RuntimeChain.h`
- Create: `src/dsp/RuntimeChain.cpp`
- Modify: `src/dsp/PedalEngine.h`
- Modify: `src/dsp/PedalEngine.cpp`
- Modify: `src/audio/EngineLoader.cpp`
- Create: `tests/runtime_chain_smoke.cpp`
- Modify: `CMakeLists.txt`

**Architecture:**

Use a small serial runner owned by `PedalEngine`.

Runtime block kinds:

- `NamBlock`: wraps the existing `NamProcessor`.
- `CabBlock`: wraps the existing `IrConvolver`.
- `DaisyBlock`: wraps `DaisyFxProcessor`.

Processing rules:

- Input starts mono and becomes `{mono, mono}`.
- NAM consumes the left channel and writes mono back to both channels.
- Cab consumes the left channel in v1 and writes mono back to both channels.
- Daisy blocks process stereo.
- Global input gain is applied before the first block.
- Global output gain, master volume, and safety limit are applied after the last block.
- If no runtime blocks are ready, output is dry through global/master/safety.

- [ ] Add a chain specification type built by `EngineLoader` outside realtime.
- [ ] Build NAM, cab, and Daisy block objects before swapping them into `PedalEngine`.
- [ ] Replace the fixed `NAM -> IR` processing path with the serial runner.
- [ ] Keep existing public `loadNam()` and `loadIr()` compatibility functions for offline smoke tests.
- [ ] Add a smoke test proving chain order changes output for `mod -> nam` versus `nam -> mod`.
- [ ] Add a smoke test proving bypass returns dry stereo and resets block state.
- [ ] Commit with:

```bash
git add src/dsp src/audio/EngineLoader.cpp tests/runtime_chain_smoke.cpp CMakeLists.txt
git commit -m "feat: process preset blocks in serial order"
```

**Gate:**

```bash
cmake --build build --target pedal-runtime-chain-smoke pedal-engine-contract-smoke pedal-offline-smoke pedal-poc
ctest --test-dir build -R "pedal-runtime-chain-smoke|pedal-engine-contract-smoke|pedal-offline-smoke" --output-on-failure
git diff --check
```

Expected: current NAM/cab behavior still works, and a `mod/vintage_trem` block can run anywhere in the serial chain.

---

## Task 6: Wire Daisy Blocks Into The LVGL UI

**Files:**
- Modify: `src/ui/UiModel.h`
- Modify: `src/ui/UiModel.cpp`
- Modify: `src/ui/LvglUi.cpp`
- Modify: `tests/ui_model_smoke.cpp`

**Behavior:**

- Block drawer categories include `Modulation`, `Delay`, and `Reverb`.
- `Modulation` shows `Vintage Trem` in the first slice.
- Dragging from the drawer creates a block with `type`, empty `asset`, and catalog default params.
- Placing a new Daisy block does not open the parameter drawer.
- Tapping a Daisy block opens the bottom parameter drawer.
- Parameter controls are generated from the catalog descriptor.
- Manual Save writes changed Daisy params back to preset JSON.

- [ ] Add UI model helpers for creating a block from a `DaisyFxDescriptor`.
- [ ] Add smoke coverage for default params and save/load round-trip.
- [ ] Populate the block drawer from `DaisyFxCatalog` instead of hard-coded demo names for Daisy effects.
- [ ] Render normalized sliders for descriptor params in the parameter drawer.
- [ ] Keep NAM and cab UI behavior unchanged.
- [ ] Commit with:

```bash
git add src/ui tests/ui_model_smoke.cpp
git commit -m "feat: edit daisy effect params in ui"
```

**Gate:**

```bash
cmake --build build --target pedal-ui-model-smoke pedal-ui-sim
ctest --test-dir build -R pedal-ui-model-smoke --output-on-failure
SDL_VIDEODRIVER=dummy ./build/pedal-ui-sim --data-root . --bank 0
git diff --check
```

Expected: the simulator can add `Vintage Trem`, edit its params, save the preset, reload it, and show the same values.

---

## Task 7: Import The First Delay And Reverb Modes

**Files:**
- Copy additional files under: `third_party/daisy-multi-fx-hosted/src/`
- Modify: `src/daisyfx/DaisyFxCatalog.cpp`
- Modify: `src/daisyfx/DaisyFxProcessor.cpp`
- Modify: `tests/daisy_fx_smoke.cpp`
- Modify: `CMakeLists.txt`

**Delay first slice:**
- Mode: `digital`
- Files:
  - `config/delay_mode_id.h`
  - `params/delay_param_id.h`
  - `params/delay_param_set.h/.cpp`
  - `params/delay_param_map.h/.cpp`
  - `dsp/delay_line_sdram.h/.cpp`
  - `dsp/tone_filter.h/.cpp`
  - `dsp/dc_blocker.h`
  - `dsp/feedback_limiter.h`
  - `modes/delay_mode.h`
  - `modes/digital_delay.h/.cpp`

**Reverb first slice:**
- Mode: `room`
- Files:
  - `config/reverb_mode_id.h`
  - `params/reverb_param_id.h`
  - `params/reverb_param_set.h/.cpp`
  - `params/reverb_param_map.h/.cpp`
  - `dsp/allpass.h`
  - `dsp/comb_filter.h`
  - `dsp/diffuser.h/.cpp`
  - `dsp/early_reflections.h`
  - `dsp/fdn.h/.cpp`
  - `dsp/tone_filter.h/.cpp`
  - `modes/reverb_mode.h`
  - `modes/room_reverb.h/.cpp`

- [ ] Import the files for `digital` delay.
- [ ] Add `Delay` catalog descriptor and defaults.
- [ ] Add processor support for `delay/digital`.
- [ ] Import the files for `room` reverb.
- [ ] Add `Reverb` catalog descriptor and defaults.
- [ ] Add processor support for `reverb/room`.
- [ ] Add smoke tests for finite output, reset determinism, and wet/dry mix behavior.
- [ ] Commit with:

```bash
git add third_party/daisy-multi-fx-hosted src/daisyfx tests/daisy_fx_smoke.cpp CMakeLists.txt
git commit -m "feat: add hosted daisy delay and reverb"
```

**Gate:**

```bash
cmake --build build --target pedal-daisy-fx-smoke pedal-runtime-chain-smoke pedal-ui-model-smoke pedal-ui-sim pedal-poc
ctest --test-dir build -R "pedal-daisy-fx-smoke|pedal-runtime-chain-smoke|pedal-ui-model-smoke" --output-on-failure
git diff --check
```

Expected: `Digital Delay` and `Room Reverb` can be added, edited, saved, loaded, and processed in realtime.

---

## Task 8: Expand Modes In Small Batches

**Files:**
- Copy more mode files under: `third_party/daisy-multi-fx-hosted/src/`
- Modify: `src/daisyfx/DaisyFxCatalog.cpp`
- Modify: `src/daisyfx/DaisyFxProcessor.cpp`
- Modify: tests and CMake as needed

**Batch order:**

1. More modulation: `chorus`, `flanger`, `rotary`, `vibe`, `phaser`
2. More delay: `tape`, `dual`, `filter`, `lofi`, `dbucket`, `duck`
3. More reverb: `hall`, `plate`, `spring`, `bloom`, `cloud`
4. CPU-heavy or dependency-heavy modes: `shimmer`, `chorale`, `magneto`, `poly_octave`, q-based pitch/multirate modes

- [ ] Add one batch per commit.
- [ ] Add descriptor entries and defaults with every batch.
- [ ] Keep unsupported modes out of the UI until their processor path is compiled and tested.
- [ ] Run realtime smoke after each batch on macOS with the UMC22.
- [ ] Stop expanding when CPU telemetry shows less than 30 percent headroom at `64 / 8192`.

**Gate for each batch:**

```bash
cmake --build build --target pedal-daisy-fx-smoke pedal-runtime-chain-smoke pedal-ui-sim pedal-poc
ctest --test-dir build -R "pedal-daisy-fx-smoke|pedal-runtime-chain-smoke" --output-on-failure
git diff --check
```

Manual realtime check:

```bash
./build/pedal-poc --realtime --data-root . --bank 0 --slot 0 \
  --capture-device 1 --playback-device 1 \
  --input-channel left --output-channel both \
  --block-size 64 --ir-samples 8192
```

Expected: no xruns during normal playing for the imported batch.

---

## Task 9: Document Developer And User Workflow

**Files:**
- Modify: `README.md`
- Modify: `docs/superpowers/plans/2026-07-08-master-implementation-roadmap.md`

- [ ] Document the Daisy vendor folder and import rule: copied source stays unchanged where possible.
- [ ] Document Daisy block JSON examples.
- [ ] Document supported modes and parameter keys.
- [ ] Document the realtime test command with `64 / 8192`.
- [ ] Add this plan to the master roadmap after Effect Parameters V1.
- [ ] Commit with:

```bash
git add README.md docs/superpowers/plans/2026-07-08-master-implementation-roadmap.md
git commit -m "docs: document daisy effects integration"
```

**Gate:**

```bash
git diff --check
```

Expected: developers know where Daisy code came from, how to add a new mode, and how to test it.

---

## Final Phase Gate

Run:

```bash
cmake --build build --target pedal-poc pedal-ui-sim pedal-daisy-fx-smoke pedal-runtime-chain-smoke pedal-ui-model-smoke pedal-preset-smoke pedal-engine-contract-smoke
ctest --test-dir build -R "pedal-daisy-fx-smoke|pedal-runtime-chain-smoke|pedal-ui-model-smoke|pedal-preset-smoke|pedal-engine-contract-smoke" --output-on-failure
git diff --check
```

Manual checks:

```bash
SDL_VIDEODRIVER=dummy ./build/pedal-ui-sim --data-root . --bank 0
```

```bash
./build/pedal-poc --realtime --data-root . --bank 0 --slot 0 \
  --capture-device 1 --playback-device 1 \
  --input-channel left --output-channel both \
  --block-size 64 --ir-samples 8192
```

Expected final state:

- UI can add Daisy modulation, delay, and reverb blocks.
- UI can edit and manually save each block's normalized params.
- Runtime preset switching applies Daisy blocks outside the realtime callback.
- Free block ordering works across NAM, cab, and Daisy effects.
- CPU/overrun telemetry remains usable enough to spot too-heavy chains.
- Unsupported modes are hidden from the UI and rejected by the engine.
