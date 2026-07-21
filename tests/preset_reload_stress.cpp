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
