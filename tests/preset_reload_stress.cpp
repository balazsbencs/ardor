#include "audio/EngineLoader.h"
#include "audio/PresetActivation.h"
#include "audio/WavIo.h"
#include "dsp/PedalEngine.h"
#include "preset/PresetStore.h"
#include "ui/UiModel.h"

#include "miniaudio.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
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

void requireFinite(ardor::PedalEngine& engine, const char* context)
{
  float input[64] = {};
  float left[64] = {};
  float right[64] = {};
  input[0] = 1.0f;
  engine.processBlock(input, left, right, 64);
  for (float sample : left) require(std::isfinite(sample), context);
  for (float sample : right) require(std::isfinite(sample), context);
}

double percentile(std::vector<double> values, double fraction)
{
  if (values.empty()) return 0.0;
  std::sort(values.begin(), values.end());
  const auto index = static_cast<std::size_t>(std::ceil(fraction * values.size())) - 1;
  return values[std::min(index, values.size() - 1)];
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

    // Deterministic control-thread preview stress: each edit prepares a new
    // engine while the previous one remains owned by liveEngine, then commits
    // only after the synthetic backend acknowledges the swap.
    ardor::Preset base;
    base.name = "Live edit base";
    base.blocks.push_back({"cab", "cab", true, "irs/a.wav", nlohmann::json::object()});
    auto liveEngine = std::make_unique<ardor::PedalEngine>();
    std::string error;
    require(ardor::applyPreset(*liveEngine, base, root, options, error), error.c_str());
    auto state = ardor::makeDemoUiState();
    ardor::replaceActivePreset(state, base);
    const auto eqAsset = std::find_if(state.assets.begin(), state.assets.end(), [](const ardor::UiAsset& asset) {
      return asset.name == "Five Band EQ";
    });
    require(eqAsset != state.assets.end(), "EQ asset is available for live edit stress");
    const auto eqAssetIndex = static_cast<std::size_t>(std::distance(state.assets.begin(), eqAsset));
    std::vector<double> preparationTimes;
    std::vector<double> activationTimes;
    std::vector<double> totalTimes;
    const auto activatePreview = [&] {
      require(ardor::beginApplyingPreview(state), "queued edit should enter applying state");
      const auto totalStart = std::chrono::steady_clock::now();
      std::chrono::steady_clock::time_point activationStart{};
      bool reachedBackend = false;
      const auto result = ardor::prepareAndActivateDraft(
        liveEngine, ardor::activePresetToPreset(state), root, options, 0.8f,
        [&](ardor::PedalEngine&) {
          activationStart = std::chrono::steady_clock::now();
          reachedBackend = true;
          return ardor::EngineReplaceResult::Activated;
        });
      const auto complete = std::chrono::steady_clock::now();
      require(result.activated(), "live edit stress activation should succeed");
      require(reachedBackend, "live edit stress target should reach backend");
      preparationTimes.push_back(std::chrono::duration<double, std::milli>(activationStart - totalStart).count());
      activationTimes.push_back(std::chrono::duration<double, std::milli>(complete - activationStart).count());
      totalTimes.push_back(std::chrono::duration<double, std::milli>(complete - totalStart).count());
      ardor::completeStructuralPreview(state);
      requireFinite(*liveEngine, "live edit stress finite output");
    };

    for (int iteration = 0; iteration < 100; ++iteration) {
      ardor::appendAssetBlock(state, eqAssetIndex);
      activatePreview();
      const auto eqId = state.bank.presets[state.activePreset].blocks[state.selectedBlock].id;
      auto eq = ardor::selectedParametricEqParams(state);
      eq.bands[0].gainDb = static_cast<float>((iteration % 13) - 6);
      require(ardor::setSelectedEqBand(state, 0, eq.bands[0]), "EQ edit should update the draft");
      require(liveEngine->setParametricEqBand(eqId, 0, ardor::selectedParametricEqParams(state).bands[0]),
              "EQ edit should update the active runtime directly");

      ardor::moveBlock(state, state.selectedBlock, 0);
      activatePreview();
      ardor::setSelectedBlockEnabled(state, false);
      activatePreview();
      require(ardor::deleteSelectedBlock(state), "EQ delete should queue a preview");
      activatePreview();
      require(ardor::undoLastBlockEdit(state), "EQ delete should be undoable");
      activatePreview();
      require(ardor::deleteSelectedBlock(state), "stress cleanup delete should queue a preview");
      activatePreview();
    }
    std::cout << "live_edit_preview_stress count=" << totalTimes.size()
              << " prepare_p95_ms=" << percentile(preparationTimes, 0.95)
              << " activate_p95_ms=" << percentile(activationTimes, 0.95)
              << " total_p95_ms=" << percentile(totalTimes, 0.95)
              << " total_max_ms=" << percentile(totalTimes, 1.0) << "\n";

    std::filesystem::remove_all(root);
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "preset_reload_stress failed: " << e.what() << "\n";
    return 1;
  }
}
