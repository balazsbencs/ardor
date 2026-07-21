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
    writeMonoWav(root / "irs/tail.wav", {0.5f, 0.25f});
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
    // IR {0.5} has a flat response at 0.5 < ceiling, so it is left unchanged:
    // 0.5 dry * 0.5 = 0.25.
    require(std::fabs(output[0] - 0.25f) < 0.0001f, "left sample");
    require(std::fabs(output[1] - 0.25f) < 0.0001f, "right sample");

    const auto legacyOutPath = root / "legacy-wet.wav";
    const std::string legacyCommand = "./pedal-poc --offline --bypass-nam --ir " + (root / "irs/test.wav").string()
                                    + " --input " + (root / "dry.wav").string()
                                    + " --output " + legacyOutPath.string();
    require(std::system(legacyCommand.c_str()) == 0, "legacy cli command");

    const auto legacyOutput = readStereoWav(legacyOutPath);
    require(legacyOutput.size() == 2, "legacy stereo frame count");
    require(std::fabs(legacyOutput[0] - 0.25f) < 0.0001f, "legacy left sample");
    require(std::fabs(legacyOutput[1] - 0.25f) < 0.0001f, "legacy right sample");

    const auto tailOutPath = root / "tail-wet.wav";
    const std::string tailCommand = "./pedal-poc --offline --bypass-nam --ir " + (root / "irs/tail.wav").string()
                                  + " --input " + (root / "dry.wav").string()
                                  + " --output " + tailOutPath.string();
    require(std::system(tailCommand.c_str()) == 0, "offline default tail command");
    const auto tailOutput = readStereoWav(tailOutPath);
    require(tailOutput.size() == 4, "default cabinet tail frame count");
    // IR {0.5,0.25} response peak 0.75 < ceiling, left unchanged: 0.5 * 0.25.
    require(std::fabs(tailOutput[2] - 0.125f) < 0.0001f, "default cabinet tail sample");

    const auto noTailOutPath = root / "no-tail-wet.wav";
    const std::string noTailCommand = "./pedal-poc --offline --no-tail --bypass-nam --ir "
                                    + (root / "irs/tail.wav").string()
                                    + " --input " + (root / "dry.wav").string()
                                    + " --output " + noTailOutPath.string();
    require(std::system(noTailCommand.c_str()) == 0, "offline no-tail command");
    require(readStereoWav(noTailOutPath).size() == 2, "no-tail frame count");

    const std::string zeroBlockCommand = "./pedal-poc --offline --block-size 0 --bypass-nam --ir "
                                       + (root / "irs/test.wav").string()
                                       + " --input " + (root / "dry.wav").string()
                                       + " --output " + (root / "zero-block.wav").string();
    require(std::system(zeroBlockCommand.c_str()) != 0,
            "offline block size zero must fail instead of entering a zero-step render loop");

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

    std::filesystem::remove_all(root);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "preset_cli_smoke failed: " << error.what() << '\n';
    return 1;
  }
}
