# Realtime Robustness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make realtime behavior easier to trust by stress-testing preset reloads and showing CPU/overrun/bypass status in the UI.

**Architecture:** Add one tiny runtime telemetry snapshot shared by CLI/UI. Keep audio callback behavior unchanged; telemetry is sampled on the control/UI thread and rendered as state.

**Tech Stack:** C++20, miniaudio stats already exposed by `MiniaudioBackend`, existing `RuntimeState`, existing LVGL UI model.

## Global Constraints

- Preset reload stress test.
- CPU/overrun telemetry visible in UI.
- Latched bypass state shown clearly.
- Keep `64 / 8192` as the known-good baseline.
- Do not load presets or assets inside the realtime audio callback.
- Do not add dependencies.

---

## File Structure

- `src/preset/RuntimeState.h`: add a small telemetry snapshot struct and formatter helpers.
- `src/preset/RuntimeState.cpp`: compute overrun percent and bypass display data.
- `tests/preset_smoke.cpp`: test telemetry snapshot and latched bypass behavior.
- `src/ui/UiModel.h`: add telemetry fields to `UiState`.
- `src/ui/UiModel.cpp`: add `updateRealtimeTelemetry`.
- `src/ui/LvglUi.cpp`: render telemetry and bypass state on preset/edit screens.
- `apps/pedal-poc/main.cpp`: use the shared formatter for CLI logs.
- `tests/preset_reload_stress.cpp`: offline stress test that repeatedly applies preset slots to a `PedalEngine`.
- `CMakeLists.txt`: build/register stress test.
- `docs/preset-runtime-testing.md`: add realtime stress procedure.

---

## Dependency

Implement this after `docs/superpowers/plans/2026-07-08-runtime-preset-switching.md` Task 1, because the reload stress test uses `applyPresetSlot`.

---

### Task 1: Add Shared Runtime Telemetry Snapshot

**Files:**
- Modify: `src/preset/RuntimeState.h`
- Modify: `src/preset/RuntimeState.cpp`
- Modify: `tests/preset_smoke.cpp`
- Modify: `apps/pedal-poc/main.cpp`

**Interfaces:**
- Consumes:
  - `ardor::RuntimeState::effectsBypassed() const`
  - `ardor::RealtimeStats` fields by value, not the type itself.
- Produces:
  - `struct RuntimeTelemetry`
  - `RuntimeTelemetry makeRuntimeTelemetry(uint64_t callbacks, uint64_t overBudget, double maxMs, double averageMs, double budgetMs, bool bypassed)`
  - `std::string formatRuntimeTelemetry(const RuntimeTelemetry& telemetry)`

- [ ] **Step 1: Write the failing test**

Add to `tests/preset_smoke.cpp` after existing `statsRuntime` assertions:

```cpp
    const auto telemetry = ardor::makeRuntimeTelemetry(100, 5, 0.8, 0.2, 1.33, true);
    require(telemetry.callbacks == 100, "telemetry callbacks");
    require(telemetry.overBudget == 5, "telemetry over budget");
    require(std::fabs(telemetry.overBudgetPercent - 5.0) < 0.0001, "telemetry over percent");
    require(telemetry.bypassed, "telemetry bypassed");
    const auto line = ardor::formatRuntimeTelemetry(telemetry);
    require(line.find("callbacks=100") != std::string::npos, "formatted callbacks");
    require(line.find("over%=5.00") != std::string::npos, "formatted over percent");
    require(line.find("bypassed=1") != std::string::npos, "formatted bypass");
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
cmake --build build --target pedal-preset-smoke && ctest --test-dir build -R pedal-preset-smoke --output-on-failure
```

Expected: compile fails because telemetry helpers do not exist.

- [ ] **Step 3: Add declarations**

In `src/preset/RuntimeState.h`, add:

```cpp
#include <string>
```

Then add:

```cpp
struct RuntimeTelemetry {
  uint64_t callbacks = 0;
  uint64_t overBudget = 0;
  double overBudgetPercent = 0.0;
  double maxMs = 0.0;
  double averageMs = 0.0;
  double budgetMs = 0.0;
  bool bypassed = false;
};

RuntimeTelemetry makeRuntimeTelemetry(uint64_t callbacks, uint64_t overBudget, double maxMs,
                                      double averageMs, double budgetMs, bool bypassed);
std::string formatRuntimeTelemetry(const RuntimeTelemetry& telemetry);
```

- [ ] **Step 4: Implement helpers**

In `src/preset/RuntimeState.cpp`, add includes:

```cpp
#include <iomanip>
#include <sstream>
```

Add:

```cpp
RuntimeTelemetry makeRuntimeTelemetry(uint64_t callbacks, uint64_t overBudget, double maxMs,
                                      double averageMs, double budgetMs, bool bypassed)
{
  RuntimeTelemetry telemetry;
  telemetry.callbacks = callbacks;
  telemetry.overBudget = overBudget;
  telemetry.overBudgetPercent = callbacks == 0 ? 0.0 : static_cast<double>(overBudget) * 100.0 / static_cast<double>(callbacks);
  telemetry.maxMs = maxMs;
  telemetry.averageMs = averageMs;
  telemetry.budgetMs = budgetMs;
  telemetry.bypassed = bypassed;
  return telemetry;
}

std::string formatRuntimeTelemetry(const RuntimeTelemetry& telemetry)
{
  std::ostringstream out;
  out << std::fixed << std::setprecision(2)
      << "callbacks=" << telemetry.callbacks
      << " over=" << telemetry.overBudget
      << " over%=" << telemetry.overBudgetPercent
      << " max=" << telemetry.maxMs << "ms"
      << " avg=" << telemetry.averageMs << "ms"
      << " budget=" << telemetry.budgetMs << "ms"
      << " bypassed=" << (telemetry.bypassed ? 1 : 0);
  return out.str();
}
```

- [ ] **Step 5: Use formatter in CLI**

Replace the manual telemetry formatting in `apps/pedal-poc/main.cpp`:

```cpp
        const auto telemetry = ardor::makeRuntimeTelemetry(stats.callbacks, stats.overBudget, stats.maxMs,
                                                           stats.averageMs, stats.budgetMs,
                                                           runtime.effectsBypassed());
        std::cerr << ardor::formatRuntimeTelemetry(telemetry) << "\n";
```

Remove now-unused `<iomanip>` if the compiler reports it unused is not needed; C++ does not care.

- [ ] **Step 6: Run test**

Run:

```bash
cmake --build build --target pedal-preset-smoke pedal-poc && ctest --test-dir build -R pedal-preset-smoke --output-on-failure
```

Expected: test passes and `pedal-poc` builds.

- [ ] **Step 7: Commit**

```bash
git add src/preset/RuntimeState.h src/preset/RuntimeState.cpp tests/preset_smoke.cpp apps/pedal-poc/main.cpp
git commit -m "feat: share runtime telemetry formatting"
```

---

### Task 2: Show Telemetry In LVGL UI State

**Files:**
- Modify: `src/ui/UiModel.h`
- Modify: `src/ui/UiModel.cpp`
- Modify: `src/ui/LvglUi.cpp`
- Modify: `tests/ui_model_smoke.cpp`

**Interfaces:**
- Consumes:
  - `RuntimeTelemetry`
  - `UiState`
- Produces:
  - `UiState::telemetry`
  - `void updateRealtimeTelemetry(UiState& state, const RuntimeTelemetry& telemetry)`
  - Visible CPU/overrun/bypass labels.

- [ ] **Step 1: Write failing model assertions**

Add to `tests/ui_model_smoke.cpp` before `return 0;`:

```cpp
  ardor::RuntimeTelemetry telemetry = ardor::makeRuntimeTelemetry(1000, 2, 0.9, 0.3, 1.33, true);
  ardor::updateRealtimeTelemetry(state, telemetry);
  if (require(state.telemetry.callbacks == 1000, "ui telemetry callbacks")) return 1;
  if (require(state.effectsBypassed, "ui bypass follows telemetry")) return 1;
```

Add include:

```cpp
#include "preset/RuntimeState.h"
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
cmake --build build --target pedal-ui-model-smoke && ctest --test-dir build -R pedal-ui-model-smoke --output-on-failure
```

Expected: compile fails because `UiState::telemetry` and `updateRealtimeTelemetry` do not exist.

- [ ] **Step 3: Add UI state field**

In `src/ui/UiModel.h`, include:

```cpp
#include "preset/RuntimeState.h"
```

Add to `UiState`:

```cpp
RuntimeTelemetry telemetry;
```

Add declaration:

```cpp
void updateRealtimeTelemetry(UiState& state, const RuntimeTelemetry& telemetry);
```

- [ ] **Step 4: Implement update helper**

In `src/ui/UiModel.cpp`, add:

```cpp
void updateRealtimeTelemetry(UiState& state, const RuntimeTelemetry& telemetry)
{
  state.telemetry = telemetry;
  state.effectsBypassed = telemetry.bypassed;
}
```

- [ ] **Step 5: Render telemetry labels**

In `src/ui/LvglUi.cpp`, add helper:

```cpp
void telemetryLine(lv_obj_t* root, const RuntimeTelemetry& telemetry, bool bypassed)
{
  const std::string status = bypassed ? "BYPASS" : "LIVE";
  const int color = bypassed ? 0xf97373 : muted;
  label(root,
        status + "  over " + std::to_string(telemetry.overBudget) + "  max "
          + std::to_string(static_cast<int>(telemetry.maxMs * 100.0) / 100.0) + "ms",
        LV_ALIGN_BOTTOM_LEFT, 18, -14, &lv_font_montserrat_18, color);
}
```

Call it at the end of both `renderPresetMode` and `renderEditMode`:

```cpp
  telemetryLine(root, state.telemetry, state.effectsBypassed);
```

- [ ] **Step 6: Build and smoke**

Run:

```bash
cmake --build build --target pedal-ui-model-smoke pedal-ui-sim
ctest --test-dir build -R pedal-ui-model-smoke --output-on-failure
SDL_VIDEODRIVER=dummy ./build/pedal-ui-sim
```

Expected: model smoke passes and simulator starts.

- [ ] **Step 7: Commit**

```bash
git add src/ui/UiModel.h src/ui/UiModel.cpp src/ui/LvglUi.cpp tests/ui_model_smoke.cpp
git commit -m "feat: show realtime telemetry in ui"
```

---

### Task 3: Add Preset Reload Stress Test

**Files:**
- Create: `tests/preset_reload_stress.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes:
  - `PresetStore`
  - `buildChainPlan`
  - `applyChainPlan`
  - `PedalEngine`
- Produces:
  - `pedal-preset-reload-stress` test.

- [ ] **Step 1: Write the stress test**

Create `tests/preset_reload_stress.cpp`:

```cpp
#include "audio/EngineLoader.h"
#include "audio/WavIo.h"
#include "dsp/PedalEngine.h"
#include "preset/PresetStore.h"

#include "miniaudio.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool ok, const char* message)
{
  if (!ok) {
    throw std::runtime_error(message);
  }
}

void writeMonoWav(const std::filesystem::path& path, const std::vector<float>& samples)
{
  ma_encoder_config cfg = ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32, 1, 48000);
  ma_encoder encoder;
  require(ma_encoder_init_file(path.string().c_str(), &cfg, &encoder) == MA_SUCCESS, "open wav");
  ma_encoder_write_pcm_frames(&encoder, samples.data(), samples.size(), nullptr);
  ma_encoder_uninit(&encoder);
}

} // namespace

int main()
{
  try {
    const auto root = std::filesystem::temp_directory_path()
                    / ("ardor-reload-stress-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "irs");
    writeMonoWav(root / "irs/a.wav", {1.0f});
    writeMonoWav(root / "irs/b.wav", {0.5f});

    ardor::PresetStore store(root);
    for (int slot = 0; slot < 4; ++slot) {
      ardor::Preset preset;
      preset.name = "Slot " + std::to_string(slot);
      preset.blocks.push_back({"cab", "cab", true, slot % 2 == 0 ? "irs/a.wav" : "irs/b.wav", nlohmann::json::object()});
      store.save({0, slot}, preset);
    }

    ardor::EngineLoadOptions options{48000, 64, 8192};
    for (int i = 0; i < 200; ++i) {
      ardor::PedalEngine engine;
      std::string error;
      require(ardor::applyPresetSlot(engine, store, {0, i % 4}, root, options, error), error.c_str());
      float input[64] = {};
      float left[64] = {};
      float right[64] = {};
      input[0] = 1.0f;
      engine.processBlock(input, left, right, 64);
      require(left[0] != 0.0f, "processed output after reload");
    }

    std::filesystem::remove_all(root);
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "preset_reload_stress failed: " << e.what() << "\n";
    return 1;
  }
}
```

- [ ] **Step 2: Register test**

In `CMakeLists.txt`, add:

```cmake
add_executable(pedal-preset-reload-stress tests/preset_reload_stress.cpp)
target_include_directories(pedal-preset-reload-stress PRIVATE ${miniaudio_SOURCE_DIR})
target_link_libraries(pedal-preset-reload-stress PRIVATE ardor_audio ardor_dsp ardor_preset)
add_test(NAME pedal-preset-reload-stress COMMAND pedal-preset-reload-stress)
```

- [ ] **Step 3: Run stress test**

Run:

```bash
cmake --build build --target pedal-preset-reload-stress && ctest --test-dir build -R pedal-preset-reload-stress --output-on-failure
```

Expected: stress test passes.

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt tests/preset_reload_stress.cpp
git commit -m "test: stress preset reloads"
```

---

### Task 4: Document Robustness Baseline

**Files:**
- Modify: `docs/preset-runtime-testing.md`
- Modify: `README.md`

**Interfaces:**
- Consumes:
  - `pedal-preset-reload-stress`
  - Runtime telemetry format.
- Produces:
  - Manual robustness test instructions.

- [ ] **Step 1: Add stress test docs**

Add to `docs/preset-runtime-testing.md`:

````markdown
## Realtime Robustness Baseline

Known-good realtime baseline:

- sample rate: `48000`
- block size: `64`
- IR samples: `8192`

Automated reload stress:

```sh
cmake --build build --target pedal-preset-reload-stress
ctest --test-dir build -R pedal-preset-reload-stress --output-on-failure
```

Manual realtime soak:

```sh
./build/pedal-poc --realtime --data-root . --bank 0 --slot 0 \
  --capture-device 1 --playback-device 1 \
  --input-channel left --output-channel both \
  --block-size 64 --ir-samples 8192
```

Pass:

- Telemetry prints once per second.
- `over` does not grow continuously.
- UI shows `LIVE` during normal operation.
- UI shows `BYPASS` if overload bypass latches.
- Preset reloads do not crash and do not run inside the audio callback.
````

- [ ] **Step 2: Add README telemetry note**

Update the realtime status section in `README.md`:

```markdown
Realtime telemetry is shared between CLI and UI. The known-good baseline remains `--block-size 64 --ir-samples 8192`. If the overload bypass latches, the CLI prints `bypassed=1` and the UI shows `BYPASS`.
```

- [ ] **Step 3: Verify**

Run:

```bash
cmake --build build --target pedal-preset-smoke pedal-ui-model-smoke pedal-preset-reload-stress
ctest --test-dir build -R "pedal-preset-smoke|pedal-ui-model-smoke|pedal-preset-reload-stress" --output-on-failure
git diff --check
```

Expected: selected tests pass and diff check is clean.

- [ ] **Step 4: Commit**

```bash
git add docs/preset-runtime-testing.md README.md
git commit -m "docs: document realtime robustness baseline"
```

---

## Skipped For This Phase

- Realtime callback instrumentation beyond existing duration stats.
- Automatic recovery after latched bypass; current behavior stays latched until preset change or explicit clear.
- Hardware-in-the-loop stress automation; add after the Pi is available.
