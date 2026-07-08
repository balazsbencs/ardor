# Preset Driven CLI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

> **STATUS: COMPLETE — DO NOT RE-EXECUTE.** All tasks landed on or before 2026-07-08; the unchecked boxes below are historical. Verify against the source tree, not the checkboxes. Listed under "Already Implemented" in the master roadmap.

**Goal:** Add `pedal-poc --preset` so offline and realtime runs can load the same preset JSON format the UI will save.

**Architecture:** Reuse `Preset`, `buildChainPlan`, and `applyChainPlan`; do not add another runtime format. Keep legacy `--model/--ir` flags working while adding a preset path that configures the engine before offline processing or realtime startup.

**Tech Stack:** C++20, CMake, miniaudio, nlohmann/json, existing smoke-test style.

## Global Constraints

- Keep one serial chain for v1.
- Relative asset paths only; absolute paths and `..` traversal stay rejected by existing preset validation.
- Realtime default target remains `48000 Hz`, `--block-size 64`, `--ir-samples 8192`.
- V1 runtime loads the first ready `nam` block and first ready `cab` block.
- Do not add new dependencies.
- Keep existing loose `--model` and `--ir` CLI flows working.

---

## File Structure

- `apps/pedal-poc/main.cpp`: add `--preset` and `--data-root`, then route preset mode through `Preset -> ChainPlan -> PedalEngine`.
- `tests/preset_cli_smoke.cpp`: create tiny WAV and preset fixtures, run `./pedal-poc --offline --preset`, and inspect the output WAV.
- `CMakeLists.txt`: build and register the new CLI smoke test.
- `README.md`: document the preset-driven commands after the smoke test is passing.

---

### Task 1: Add Offline `--preset` Mode

**Files:**
- Modify: `apps/pedal-poc/main.cpp`
- Create: `tests/preset_cli_smoke.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes:
  - `ardor::presetFromJson(const nlohmann::json&)`
  - `ardor::buildChainPlan(const Preset&, const std::filesystem::path&)`
  - `ardor::applyChainPlan(PedalEngine&, const ChainPlan&, const EngineLoadOptions&, std::string&)`
- Produces:
  - CLI flags `--preset FILE` and `--data-root DIR`
  - Offline command shape: `pedal-poc --offline --preset preset.json --data-root data --input dry.wav --output wet.wav`

- [ ] **Step 1: Write the failing test**

Create `tests/preset_cli_smoke.cpp`:

```cpp
#include "miniaudio.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
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
  require(ma_encoder_init_file(path.string().c_str(), &cfg, &encoder) == MA_SUCCESS, "open mono wav");
  ma_encoder_write_pcm_frames(&encoder, samples.data(), samples.size(), nullptr);
  ma_encoder_uninit(&encoder);
}

std::vector<float> readStereoWav(const std::filesystem::path& path)
{
  ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 2, 48000);
  ma_decoder decoder;
  require(ma_decoder_init_file(path.string().c_str(), &cfg, &decoder) == MA_SUCCESS, "open stereo wav");
  std::vector<float> samples;
  float chunk[128];
  for (;;) {
    ma_uint64 framesRead = 0;
    ma_decoder_read_pcm_frames(&decoder, chunk, 64, &framesRead);
    if (framesRead == 0) break;
    samples.insert(samples.end(), chunk, chunk + framesRead * 2);
  }
  ma_decoder_uninit(&decoder);
  return samples;
}

} // namespace

int main()
{
  try {
    const auto root = std::filesystem::temp_directory_path()
                    / ("ardor-preset-cli-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "irs");

    writeMonoWav(root / "irs/test.wav", {0.5f});
    writeMonoWav(root / "dry.wav", {0.5f});

    const auto presetPath = root / "preset.json";
    std::ofstream preset(presetPath);
    preset << R"({
  "version": 1,
  "name": "Cab Only",
  "routing": "serial",
  "global": {
    "inputGainDb": 0.0,
    "outputGainDb": 0.0,
    "safetyLimitDb": 0.0
  },
  "blocks": [
    {
      "id": "cab-1",
      "type": "cab",
      "enabled": true,
      "asset": "irs/test.wav",
      "params": {}
    }
  ]
})";
    preset.close();

    const auto outPath = root / "wet.wav";
    const std::string command = "./pedal-poc --offline --preset " + presetPath.string()
                              + " --data-root " + root.string()
                              + " --input " + (root / "dry.wav").string()
                              + " --output " + outPath.string();
    require(std::system(command.c_str()) == 0, "preset cli command");

    const auto output = readStereoWav(outPath);
    require(output.size() == 2, "stereo frame count");
    require(std::fabs(output[0] - 0.25f) < 0.0001f, "left sample");
    require(std::fabs(output[1] - 0.25f) < 0.0001f, "right sample");

    std::filesystem::remove_all(root);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "preset_cli_smoke failed: " << error.what() << '\n';
    return 1;
  }
}
```

Update `CMakeLists.txt`:

```cmake
add_executable(pedal-preset-cli-smoke tests/preset_cli_smoke.cpp)
target_include_directories(pedal-preset-cli-smoke PRIVATE ${miniaudio_SOURCE_DIR})
add_dependencies(pedal-preset-cli-smoke pedal-poc)
add_test(NAME pedal-preset-cli-smoke COMMAND pedal-preset-cli-smoke)
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
cmake --build build --target pedal-preset-cli-smoke && ctest --test-dir build -R pedal-preset-cli-smoke --output-on-failure
```

Expected: test fails because `pedal-poc` rejects `--preset` or prints usage.

- [ ] **Step 3: Write minimal implementation**

In `apps/pedal-poc/main.cpp`, add includes:

```cpp
#include "audio/EngineLoader.h"
#include "preset/ChainPlan.h"
#include "preset/Preset.h"

#include <fstream>
```

Extend `Args`:

```cpp
std::filesystem::path preset;
std::filesystem::path dataRoot = ".";
```

Add parse branches:

```cpp
} else if (a == "--preset") {
  const char* v = value();
  if (!v) return false;
  args.preset = v;
} else if (a == "--data-root") {
  const char* v = value();
  if (!v) return false;
  args.dataRoot = v;
```

Update validation:

```cpp
if (args.offline) {
  if (!args.preset.empty()) {
    return !args.input.empty() && !args.output.empty();
  }
  return !args.ir.empty() && !args.input.empty() && !args.output.empty() && (args.bypassNam || !args.model.empty());
}
if (args.realtime) {
  if (!args.preset.empty()) {
    return true;
  }
  return !args.ir.empty() && !args.model.empty();
}
```

Add this helper in the anonymous namespace:

```cpp
bool loadPresetIntoEngine(ardor::PedalEngine& engine, const Args& args, std::string& error)
{
  std::ifstream in(args.preset);
  if (!in) {
    error = "failed to open preset: " + args.preset.string();
    return false;
  }

  nlohmann::json json;
  in >> json;
  const ardor::Preset preset = ardor::presetFromJson(json);
  const ardor::ChainPlan plan = ardor::buildChainPlan(preset, args.dataRoot);
  const size_t irSamples = args.irSamples == 0 ? 8192 : args.irSamples;
  return ardor::applyChainPlan(engine, plan, {args.sampleRate, args.blockSize, irSamples}, error);
}
```

In `main`, create `ardor::PedalEngine engine;` before legacy IR loading and branch:

```cpp
ardor::PedalEngine engine;
if (!args.preset.empty()) {
  std::string error;
  if (!loadPresetIntoEngine(engine, args, error)) {
    std::cerr << error << "\n";
    return 1;
  }
} else {
  auto irWav = ardor::readMonoWav(args.ir);
  auto impulse = std::move(irWav.samples);
  const ma_uint32 irRate = irWav.sampleRate;
  if (irRate != args.sampleRate) {
    std::cerr << "Expected " << args.sampleRate << " Hz IR\n";
    return 1;
  }

  if (args.realtime) {
    if (args.irSamples > 0 && impulse.size() > args.irSamples) {
      impulse.resize(args.irSamples);
      std::cerr << "Trimmed realtime IR to " << args.irSamples << " samples\n";
    }
  } else if (args.irSamples > 0 && impulse.size() > args.irSamples) {
    impulse.resize(args.irSamples);
    std::cerr << "Trimmed IR to " << args.irSamples << " samples\n";
  }
  engine.loadIr(std::move(impulse));
  engine.setInputGain(dbToGain(args.inputGainDb));
  engine.setOutputGain(dbToGain(args.outputGainDb));
  engine.setSafetyLimiterEnabled(args.safetyLimiter);
  engine.setSafetyLimit(dbToGain(args.safetyLimitDb));
}
```

In the realtime branch, only load the loose NAM when no preset was used:

```cpp
if (args.preset.empty() && !engine.loadNam(args.model, args.sampleRate, static_cast<int>(args.blockSize))) {
  std::cerr << "Failed to load NAM model\n";
  return 1;
}
```

In the offline branch, only load the loose NAM when no preset was used:

```cpp
if (args.preset.empty() && !args.bypassNam && !engine.loadNam(args.model, args.sampleRate, 128)) {
  std::cerr << "Failed to load NAM model\n";
  return 1;
}
```

Update usage text with:

```cpp
<< "  pedal-poc --offline --preset preset.json --data-root data --input dry.wav --output wet.wav\n"
<< "  pedal-poc --realtime --preset preset.json --data-root data [--sample-rate 48000] [--block-size 64]\n"
```

- [ ] **Step 4: Run test to verify it passes**

Run:

```bash
cmake --build build --target pedal-preset-cli-smoke && ctest --test-dir build -R pedal-preset-cli-smoke --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt apps/pedal-poc/main.cpp tests/preset_cli_smoke.cpp
git commit -m "feat: run pedal poc from preset"
```

---

### Task 2: Preserve Legacy CLI Behavior

**Files:**
- Modify: `tests/preset_cli_smoke.cpp`

**Interfaces:**
- Consumes: existing loose offline command shape
- Produces: regression coverage that `--offline --ir --input --output --bypass-nam` still works

- [ ] **Step 1: Write the failing-or-passing regression test**

Append this check before removing `root` in `tests/preset_cli_smoke.cpp`:

```cpp
const auto legacyOutPath = root / "legacy-wet.wav";
const std::string legacyCommand = "./pedal-poc --offline --bypass-nam --ir " + (root / "irs/test.wav").string()
                                + " --input " + (root / "dry.wav").string()
                                + " --output " + legacyOutPath.string();
require(std::system(legacyCommand.c_str()) == 0, "legacy cli command");

const auto legacyOutput = readStereoWav(legacyOutPath);
require(legacyOutput.size() == 2, "legacy stereo frame count");
require(std::fabs(legacyOutput[0] - 0.25f) < 0.0001f, "legacy left sample");
require(std::fabs(legacyOutput[1] - 0.25f) < 0.0001f, "legacy right sample");
```

- [ ] **Step 2: Run test**

Run:

```bash
cmake --build build --target pedal-preset-cli-smoke && ctest --test-dir build -R pedal-preset-cli-smoke --output-on-failure
```

Expected: PASS. If this fails, fix `apps/pedal-poc/main.cpp` so the legacy path still uses the old `--ir`/gain/NAM setup.

- [ ] **Step 3: Commit**

```bash
git add tests/preset_cli_smoke.cpp apps/pedal-poc/main.cpp
git commit -m "test: cover legacy pedal poc cli"
```

---

### Task 3: Document Preset Runtime Commands

**Files:**
- Modify: `README.md`

**Interfaces:**
- Consumes: final CLI shape from Task 1
- Produces: developer-facing commands for offline and realtime preset testing

- [ ] **Step 1: Update README**

Add a short section:

````markdown
### Preset-driven runs

Preset files use relative asset paths under `--data-root`.

Offline:

```sh
./build/pedal-poc --offline \
  --preset ./presets/bank-000/preset-0.json \
  --data-root . \
  --input ./dryguitar.wav \
  --output ./wet.wav
```

Realtime:

```sh
./build/pedal-poc --realtime \
  --preset ./presets/bank-000/preset-0.json \
  --data-root . \
  --capture-device 1 \
  --playback-device 1 \
  --input-channel left \
  --output-channel both \
  --block-size 64 \
  --ir-samples 8192
```
````

- [ ] **Step 2: Run verification**

Run:

```bash
cmake --build build --target pedal-poc pedal-preset-cli-smoke && ctest --test-dir build -R pedal-preset-cli-smoke --output-on-failure
```

Expected: PASS.

- [ ] **Step 3: Commit**

```bash
git add README.md
git commit -m "docs: document preset driven pedal poc"
```

---

## Final Verification

Run:

```bash
cmake --build build --target pedal-poc pedal-ui-sim pedal-offline-smoke pedal-preset-smoke pedal-engine-contract-smoke pedal-ui-model-smoke pedal-preset-cli-smoke
ctest --test-dir build --output-on-failure
python3 - <<'PY'
import os, subprocess, time
env=os.environ.copy()
env['SDL_VIDEODRIVER']='dummy'
p=subprocess.Popen(['./build/pedal-ui-sim'], cwd='.', env=env)
time.sleep(1)
code=p.poll()
if code is not None:
    raise SystemExit(f'pedal-ui-sim exited early with code {code}')
p.terminate()
try:
    p.wait(timeout=3)
except subprocess.TimeoutExpired:
    p.kill()
    p.wait()
print('pedal-ui-sim launch smoke passed')
PY
```

Expected: all tests pass and simulator launch smoke passes.

## Self-Review

- Spec coverage: offline preset command, realtime preset command path, legacy CLI regression, and developer docs are covered.
- Placeholder scan: no unfilled implementation slots.
- Type consistency: `Args::preset`, `Args::dataRoot`, and `loadPresetIntoEngine` are introduced before use.
- Ponytail check: skipped a full preset-management CLI, preset slot addressing, and UI save buttons. Add those after one-file preset runtime is proven.
