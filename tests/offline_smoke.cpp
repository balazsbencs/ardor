#include "audio/WavIo.h"
#include "audio/EngineLoader.h"
#include "audio/MiniaudioBackend.h"
#include "dsp/IrConvolver.h"
#include "dsp/PedalEngine.h"
#include "preset/ChainPlan.h"
#include "NAM/model_config.h"
#include "miniaudio.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

int require(bool condition)
{
  return condition ? 0 : 1;
}

} // namespace

int main()
{
  if (require(nam::ConfigParserRegistry::instance().has("SlimmableContainer"))) return 1;

  const auto wavPath = std::filesystem::temp_directory_path() / "ardor-wav-io-smoke.wav";
  {
    const float samples[] = {0.25f, -0.5f};
    ma_encoder_config cfg = ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32, 1, 48000);
    ma_encoder encoder;
    if (require(ma_encoder_init_file(wavPath.string().c_str(), &cfg, &encoder) == MA_SUCCESS)) return 1;
    ma_encoder_write_pcm_frames(&encoder, samples, 2, nullptr);
    ma_encoder_uninit(&encoder);
  }
  const auto wav = ardor::readMonoWav(wavPath);
  if (require(wav.sampleRate == 48000)) return 1;
  if (require(wav.samples.size() == 2)) return 1;
  if (require(std::fabs(wav.samples[0] - 0.25f) < 0.0001f)) return 1;

  const auto wav441Path = std::filesystem::temp_directory_path() / "ardor-wav-io-44100-smoke.wav";
  {
    const float samples[] = {0.25f, -0.5f};
    ma_encoder_config cfg = ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32, 1, 44100);
    ma_encoder encoder;
    if (require(ma_encoder_init_file(wav441Path.string().c_str(), &cfg, &encoder) == MA_SUCCESS)) return 1;
    ma_encoder_write_pcm_frames(&encoder, samples, 2, nullptr);
    ma_encoder_uninit(&encoder);
  }
  const auto wav441 = ardor::readMonoWav(wav441Path);
  if (require(wav441.sampleRate == 44100)) return 1;

  if (require(ardor::captureChannelCountForInput(0) == 2)) return 1;
  if (require(ardor::captureChannelCountForInput(1) == 2)) return 1;

  ardor::ChainPlan cabOnly;
  cabOnly.inputGain = 1.0f;
  cabOnly.outputGain = 0.5f;
  cabOnly.safetyLimit = 1.0f;
  cabOnly.blocks.push_back({"cab-1", "cab", ardor::ChainBlockStatus::Ready, wavPath, nlohmann::json::object()});

  ardor::PedalEngine loadedEngine;
  std::string loadError;
  if (require(ardor::applyChainPlan(loadedEngine, cabOnly, {48000, 64, 8192}, loadError))) return 1;
  auto [loadedLeft, loadedRight] = loadedEngine.process(0.5f);
  if (require(std::fabs(loadedLeft - 0.0625f) < 0.0001f)) return 1;
  if (require(std::fabs(loadedRight - 0.0625f) < 0.0001f)) return 1;

  ardor::ChainPlan wrongRateCab;
  wrongRateCab.blocks.push_back({"cab-441", "cab", ardor::ChainBlockStatus::Ready, wav441Path, nlohmann::json::object()});
  if (require(!ardor::applyChainPlan(loadedEngine, wrongRateCab, {48000, 64, 8192}, loadError))) return 1;
  if (require(loadError.find("sample rate mismatch") != std::string::npos)) return 1;
  std::filesystem::remove(wav441Path);

  ardor::ChainPlan missingCab;
  missingCab.blocks.push_back({"cab-missing", "cab", ardor::ChainBlockStatus::MissingAsset, {}, nlohmann::json::object()});
  if (require(!ardor::applyChainPlan(loadedEngine, missingCab, {48000, 64, 8192}, loadError))) return 1;
  if (require(loadError.find("cab-missing") != std::string::npos)) return 1;

  const auto badWavPath = std::filesystem::temp_directory_path() / "ardor-bad-wav-smoke.wav";
  {
    std::ofstream badWav(badWavPath);
    badWav << "not a wav";
  }
  ardor::ChainPlan badCab;
  badCab.blocks.push_back({"cab-bad", "cab", ardor::ChainBlockStatus::Ready, badWavPath, nlohmann::json::object()});
  if (require(!ardor::applyChainPlan(loadedEngine, badCab, {48000, 64, 8192}, loadError))) return 1;
  if (require(loadError.find("failed to load IR") != std::string::npos)) return 1;
  std::filesystem::remove(badWavPath);

  loadedEngine.loadIr({0.0f});
  ardor::ChainPlan dryPlan;
  dryPlan.inputGain = 1.0f;
  dryPlan.outputGain = 1.0f;
  dryPlan.safetyLimit = 1.0f;
  if (require(ardor::applyChainPlan(loadedEngine, dryPlan, {48000, 64, 8192}, loadError))) return 1;
  auto [dryLeft, dryRight] = loadedEngine.process(0.5f);
  if (require(std::fabs(dryLeft - 0.5f) < 0.0001f)) return 1;
  if (require(std::fabs(dryRight - 0.5f) < 0.0001f)) return 1;
  std::filesystem::remove(wavPath);

  ardor::IrConvolver ir;
  ir.loadImpulse({1.0f, 0.5f});

  if (require(std::fabs(ir.processSample(1.0f) - 1.0f) < 0.0001f)) return 1;
  if (require(std::fabs(ir.processSample(0.0f) - 0.5f) < 0.0001f)) return 1;
  if (require(std::fabs(ir.processSample(0.0f)) < 0.0001f)) return 1;

  std::vector<float> impulse(192, 0.0f);
  impulse[0] = 0.8f;
  impulse[65] = -0.25f;
  impulse[129] = 0.125f;
  std::vector<float> input(256, 0.0f);
  input[0] = 1.0f;
  input[9] = -0.5f;
  input[140] = 0.25f;

  ardor::IrConvolver naive;
  ardor::IrConvolver fast;
  naive.loadImpulse(impulse);
  fast.loadImpulse(impulse);
  fast.prepareBlockSize(64);

  std::vector<float> expected(input.size(), 0.0f);
  std::vector<float> actual(input.size(), 0.0f);
  for (size_t i = 0; i < input.size(); ++i) {
    expected[i] = naive.processSample(input[i]);
  }
  for (size_t offset = 0; offset < input.size(); offset += 64) {
    fast.processBlock(input.data() + offset, actual.data() + offset, 64);
  }
  for (size_t i = 0; i < input.size(); ++i) {
    if (require(std::fabs(expected[i] - actual[i]) < 0.0005f)) return 1;
  }

  ardor::PedalEngine engine;
  engine.loadIr({1.0f});
  engine.setInputGain(0.5f);
  engine.setOutputGain(2.0f);
  const auto [left, right] = engine.process(0.25f);
  if (require(std::fabs(left - 0.25f) < 0.0001f)) return 1;
  if (require(std::fabs(right - 0.25f) < 0.0001f)) return 1;
  engine.setSafetyLimit(0.5f);
  const auto [limitedLeft, limitedRight] = engine.process(1.0f);
  if (require(std::fabs(limitedLeft - 0.5f) < 0.0001f)) return 1;
  if (require(std::fabs(limitedRight - 0.5f) < 0.0001f)) return 1;

  ardor::PedalEngine blockEngine;
  blockEngine.prepareBlockSize(64);
  blockEngine.loadIr(impulse);
  blockEngine.setSafetyLimit(0.5f);
  std::vector<float> blockLeft(input.size(), 0.0f);
  std::vector<float> blockRight(input.size(), 0.0f);
  for (size_t offset = 0; offset < input.size(); offset += 64) {
    blockEngine.processBlock(input.data() + offset, blockLeft.data() + offset, blockRight.data() + offset, 64);
  }
  for (size_t i = 0; i < input.size(); ++i) {
    const float limited = std::fmax(-0.5f, std::fmin(0.5f, expected[i]));
    if (require(std::fabs(limited - blockLeft[i]) < 0.0005f)) return 1;
    if (require(std::fabs(limited - blockRight[i]) < 0.0005f)) return 1;
  }

  if (require(!engine.loadNam(std::filesystem::path{"missing.nam"}, 48000.0, 128))) return 1;
  return 0;
}
