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
