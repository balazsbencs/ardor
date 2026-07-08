# Runtime Preset Switching Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let realtime preset changes apply `Preset -> ChainPlan -> PedalEngine` without loading files or rebuilding DSP inside the realtime callback.

**Architecture:** Keep the audio callback dumb. The control thread stops audio, prepares a new `PedalEngine` from the selected preset slot, swaps ownership outside the callback, restarts audio, and resumes telemetry.

**Tech Stack:** C++20, miniaudio, existing `PresetStore`, existing `buildChainPlan`, existing `applyChainPlan`, current CLI smoke-test style.

## Global Constraints

- Keep `--block-size 64 --ir-samples 8192` as the known-good realtime target.
- Keep `--preset FILE` working.
- Add slot-based realtime loading from `--data-root DIR --bank N --slot N`.
- Never load NAM, WAV, JSON, or allocate a new engine inside the miniaudio callback.
- On switch failure, restart the previous engine and print the error.
- Do not add dependencies.

---

## File Structure

- `src/audio/EngineLoader.h`: expose preset and preset-slot engine loading helpers.
- `src/audio/EngineLoader.cpp`: move JSON/store loading into reusable functions.
- `tests/preset_cli_smoke.cpp`: cover slot-based preset loading in offline mode.
- `apps/pedal-poc/main.cpp`: add `--bank`, `--slot`, and realtime keyboard switching for slots `0` through `3`.
- `README.md`: document the realtime switching command.

---

### Task 1: Reuse Preset-Slot Engine Loading

**Files:**
- Modify: `src/audio/EngineLoader.h`
- Modify: `src/audio/EngineLoader.cpp`
- Modify: `tests/preset_cli_smoke.cpp`

**Interfaces:**
- Consumes:
  - `ardor::Preset`
  - `ardor::PresetStore`
  - `ardor::PresetSlot`
  - `ardor::buildChainPlan(const Preset&, const std::filesystem::path&)`
  - `ardor::applyChainPlan(PedalEngine&, const ChainPlan&, const EngineLoadOptions&, std::string&)`
- Produces:
  - `bool applyPreset(PedalEngine& engine, const Preset& preset, const std::filesystem::path& dataRoot, const EngineLoadOptions& options, std::string& error)`
  - `bool applyPresetSlot(PedalEngine& engine, const PresetStore& store, PresetSlot slot, const std::filesystem::path& dataRoot, const EngineLoadOptions& options, std::string& error)`

- [ ] **Step 1: Write the failing test**

In `tests/preset_cli_smoke.cpp`, after the existing preset-file command passes, create a store slot and run the slot command:

```cpp
    std::filesystem::create_directories(root / "presets/bank-000");
    std::filesystem::copy_file(presetPath, root / "presets/bank-000/preset-0.json",
                               std::filesystem::copy_options::overwrite_existing);
    const auto slotOutPath = root / "slot-wet.wav";
    const std::string slotCommand = "./pedal-poc --offline --data-root " + root.string()
                                  + " --bank 0 --slot 0"
                                  + " --input " + (root / "dry.wav").string()
                                  + " --output " + slotOutPath.string();
    require(std::system(slotCommand.c_str()) == 0, "preset slot cli command");

    const auto slotOutput = readStereoWav(slotOutPath);
    require(slotOutput.size() == 2, "slot stereo frame count");
    require(std::fabs(slotOutput[0] - 0.25f) < 0.0001f, "slot left sample");
    require(std::fabs(slotOutput[1] - 0.25f) < 0.0001f, "slot right sample");
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
cmake --build build --target pedal-preset-cli-smoke && ctest --test-dir build -R pedal-preset-cli-smoke --output-on-failure
```

Expected: test fails because `--bank` and `--slot` are not parsed by `pedal-poc`.

- [ ] **Step 3: Add reusable loader declarations**

In `src/audio/EngineLoader.h`, add includes:

```cpp
#include "preset/PresetStore.h"

#include <filesystem>
```

Add declarations:

```cpp
bool applyPreset(PedalEngine& engine, const Preset& preset, const std::filesystem::path& dataRoot,
                 const EngineLoadOptions& options, std::string& error);
bool applyPresetSlot(PedalEngine& engine, const PresetStore& store, PresetSlot slot,
                     const std::filesystem::path& dataRoot, const EngineLoadOptions& options, std::string& error);
```

- [ ] **Step 4: Implement reusable loaders**

In `src/audio/EngineLoader.cpp`, implement:

```cpp
bool applyPreset(PedalEngine& engine, const Preset& preset, const std::filesystem::path& dataRoot,
                 const EngineLoadOptions& options, std::string& error)
{
  return applyChainPlan(engine, buildChainPlan(preset, dataRoot), options, error);
}

bool applyPresetSlot(PedalEngine& engine, const PresetStore& store, PresetSlot slot,
                     const std::filesystem::path& dataRoot, const EngineLoadOptions& options, std::string& error)
{
  try {
    return applyPreset(engine, store.load(slot), dataRoot, options, error);
  } catch (const std::exception& e) {
    error = e.what();
    return false;
  }
}
```

- [ ] **Step 5: Run the failing test again**

Run:

```bash
cmake --build build --target pedal-preset-cli-smoke && ctest --test-dir build -R pedal-preset-cli-smoke --output-on-failure
```

Expected: still fails because the CLI does not yet call the new helpers.

- [ ] **Step 6: Commit**

```bash
git add src/audio/EngineLoader.h src/audio/EngineLoader.cpp tests/preset_cli_smoke.cpp
git commit -m "feat: expose preset slot engine loading"
```

---

### Task 2: Add Slot-Based Offline And Realtime CLI Loading

**Files:**
- Modify: `apps/pedal-poc/main.cpp`

**Interfaces:**
- Consumes:
  - `applyPreset(PedalEngine&, const Preset&, const std::filesystem::path&, const EngineLoadOptions&, std::string&)`
  - `applyPresetSlot(PedalEngine&, const PresetStore&, PresetSlot, const std::filesystem::path&, const EngineLoadOptions&, std::string&)`
- Produces:
  - `pedal-poc --offline --data-root DIR --bank 0 --slot 0 --input dry.wav --output wet.wav`
  - `pedal-poc --realtime --data-root DIR --bank 0 --slot 0`

- [ ] **Step 1: Add args**

In `Args`, add:

```cpp
bool presetSlotMode = false;
int bank = 0;
int slot = 0;
```

In `parse`, add:

```cpp
    } else if (a == "--bank") {
      const char* v = value();
      if (!v) return false;
      args.bank = std::stoi(v);
      args.presetSlotMode = true;
    } else if (a == "--slot") {
      const char* v = value();
      if (!v) return false;
      args.slot = std::stoi(v);
      args.presetSlotMode = true;
```

After the loop, reject invalid slots:

```cpp
  if (args.presetSlotMode && (args.bank < 0 || args.bank >= 100 || args.slot < 0 || args.slot >= 4)) {
    return false;
  }
```

Update validation:

```cpp
  if (args.offline) {
    if (!args.preset.empty() || args.presetSlotMode) {
      return !args.input.empty() && !args.output.empty();
    }
    return !args.ir.empty() && !args.input.empty() && !args.output.empty() && (args.bypassNam || !args.model.empty());
  }
  if (args.realtime) {
    if (!args.preset.empty() || args.presetSlotMode) {
      return true;
    }
    return !args.ir.empty() && !args.model.empty();
  }
```

- [ ] **Step 2: Route slot mode through `PresetStore`**

Replace the current preset-file load branch in `main` with:

```cpp
    const ardor::EngineLoadOptions loadOptions{
      args.sampleRate,
      args.blockSize,
      args.irSamples == 0 ? size_t{8192} : args.irSamples,
    };

    ardor::PedalEngine engine;
    if (args.presetSlotMode) {
      std::string error;
      ardor::PresetStore store(args.dataRoot);
      if (!ardor::applyPresetSlot(engine, store, {args.bank, args.slot}, args.dataRoot, loadOptions, error)) {
        std::cerr << error << "\n";
        return 1;
      }
    } else if (!args.preset.empty()) {
      std::string error;
      std::ifstream in(args.preset);
      if (!in) {
        std::cerr << "failed to open preset: " << args.preset << "\n";
        return 1;
      }
      nlohmann::json json;
      in >> json;
      if (!ardor::applyPreset(engine, ardor::presetFromJson(json), args.dataRoot, loadOptions, error)) {
        std::cerr << error << "\n";
        return 1;
      }
    } else {
```

Remove the old local `loadPresetIntoEngine` helper after this compiles.

- [ ] **Step 3: Update usage text**

Add:

```cpp
                << "  pedal-poc --offline --data-root data --bank 0 --slot 0 --input dry.wav --output wet.wav\n"
                << "  pedal-poc --realtime --data-root data --bank 0 --slot 0 [--sample-rate 48000] [--block-size 64]\n"
```

- [ ] **Step 4: Run CLI smoke**

Run:

```bash
cmake --build build --target pedal-preset-cli-smoke pedal-poc && ctest --test-dir build -R pedal-preset-cli-smoke --output-on-failure
```

Expected: `pedal-preset-cli-smoke` passes.

- [ ] **Step 5: Commit**

```bash
git add apps/pedal-poc/main.cpp
git commit -m "feat: load pedal poc preset slots"
```

---

### Task 3: Add Realtime Switch Boundary

**Files:**
- Modify: `apps/pedal-poc/main.cpp`

**Interfaces:**
- Consumes:
  - `ardor::MiniaudioBackend::start(PedalEngine&, const RealtimeOptions&)`
  - `ardor::MiniaudioBackend::stop()`
  - `ardor::applyPresetSlot(...)`
- Produces:
  - Keyboard switching in realtime slot mode: type `0`, `1`, `2`, or `3`, then Enter.

- [ ] **Step 1: Add includes**

Add:

```cpp
#include <atomic>
#include <memory>
```

- [ ] **Step 2: Add stdin control thread for realtime slot mode**

In the realtime branch, before telemetry starts:

```cpp
      std::atomic<int> requestedSlot{-1};
      std::thread controlThread;
      if (args.presetSlotMode) {
        controlThread = std::thread([&]() {
          char c = 0;
          while (std::cin >> c) {
            if (c >= '0' && c <= '3') {
              requestedSlot.store(c - '0', std::memory_order_relaxed);
            }
          }
        });
        controlThread.detach();
      }
```

- [ ] **Step 3: Use owned engine pointers in realtime slot mode**

For the realtime preset-slot path, replace the stack engine with:

```cpp
      auto liveEngine = std::make_unique<ardor::PedalEngine>();
      ardor::PresetStore store(args.dataRoot);
      std::string error;
      if (!ardor::applyPresetSlot(*liveEngine, store, {args.bank, args.slot}, args.dataRoot, loadOptions, error)) {
        std::cerr << error << "\n";
        return 1;
      }
```

Start backend with `*liveEngine`.

- [ ] **Step 4: Reload only from the telemetry loop**

Inside the one-second telemetry loop, before reading stats:

```cpp
        if (args.presetSlotMode) {
          const int nextSlot = requestedSlot.exchange(-1, std::memory_order_relaxed);
          if (nextSlot >= 0 && nextSlot != args.slot) {
            auto nextEngine = std::make_unique<ardor::PedalEngine>();
            std::string error;
            if (!ardor::applyPresetSlot(*nextEngine, store, {args.bank, nextSlot}, args.dataRoot, loadOptions, error)) {
              std::cerr << "Preset switch failed: " << error << "\n";
            } else {
              backend.stop();
              liveEngine = std::move(nextEngine);
              liveEngine->reset();
              if (!backend.start(*liveEngine, options)) {
                std::cerr << "Failed to restart realtime audio\n";
                return 1;
              }
              args.slot = nextSlot;
              previousOverBudget = 0;
              std::cerr << "Switched to preset " << args.bank << ":" << args.slot << "\n";
            }
          }
        }
```

Keep the existing non-slot realtime path on the stack engine if that makes the diff smaller.

- [ ] **Step 5: Build**

Run:

```bash
cmake --build build --target pedal-poc pedal-preset-cli-smoke
ctest --test-dir build -R pedal-preset-cli-smoke --output-on-failure
```

Expected: build succeeds and preset CLI smoke passes.

- [ ] **Step 6: Manual realtime check**

Run:

```bash
./build/pedal-poc --realtime --data-root . --bank 0 --slot 0 \
  --capture-device 1 --playback-device 1 --input-channel left --output-channel both \
  --block-size 64 --ir-samples 8192
```

While it runs, type:

```text
1
0
```

Expected: audio pauses briefly, resumes with the selected preset, and telemetry continues printing. No file loading happens in the callback.

**Measure the switch gap:** record (audibly, or by timestamping the stop/start log lines) how long the audio hole is, and write the number into `docs/preset-runtime-testing.md`. This defines "unacceptable" for the deferred lock-free swap instead of leaving it to taste: if the measured gap exceeds ~150 ms, the lock-free swap moves ahead of the Buildroot phase (see roadmap Deferred section).

- [ ] **Step 7: Commit**

```bash
git add apps/pedal-poc/main.cpp
git commit -m "feat: switch realtime preset slots"
```

---

### Task 4: Document Realtime Switching

**Files:**
- Modify: `README.md`

**Interfaces:**
- Consumes:
  - `pedal-poc --realtime --data-root DIR --bank N --slot N`
- Produces:
  - Developer instructions for manual preset switching.

- [ ] **Step 1: Add README section**

Add:

````markdown
### Realtime preset slot switching

Slot-based realtime mode loads presets from `--data-root`:

```sh
./build/pedal-poc --realtime --data-root . --bank 0 --slot 0 \
  --capture-device 1 --playback-device 1 --input-channel left --output-channel both \
  --block-size 64 --ir-samples 8192
```

While it is running, type `0`, `1`, `2`, or `3`, then Enter, to switch presets in the current bank. The app reloads outside the audio callback, restarts the realtime device, and resumes telemetry.
````

- [ ] **Step 2: Verify**

Run:

```bash
cmake --build build --target pedal-poc pedal-preset-cli-smoke && ctest --test-dir build -R pedal-preset-cli-smoke --output-on-failure
```

Expected: build and CLI smoke pass.

- [ ] **Step 3: Commit**

```bash
git add README.md
git commit -m "docs: document realtime preset switching"
```

---

## Skipped For This Phase

- Lock-free double-buffered engine swapping; add when the **measured** stop/start gap (Task 3 Step 6) exceeds ~150 ms.
- UI-to-audio process integration; owned by `docs/superpowers/plans/2026-07-09-ui-audio-integration.md` (roadmap Phase 6) — it reuses this plan's `requestedSlot` boundary, so keep that boundary the single switch path.
- Bank switching while running; add with the physical footswitch combination.
- Deleting the legacy `--model/--ir` flags; flagged for removal once slot mode is the Pi boot path (tracked in the roadmap Deferred section).
