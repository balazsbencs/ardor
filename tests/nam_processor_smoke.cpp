#include "dsp/NamProcessor.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <vector>

namespace {

int require(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << message << '\n';
    return 1;
  }
  return 0;
}

} // namespace

int main()
{
  ardor::NamProcessor processor;
  const float input[] = {0.1f, -0.2f, 0.3f};
  float output[] = {0.0f, 0.0f, 0.0f};

  processor.processBlock(input, output, 3);

  for (size_t i = 0; i < 3; ++i) {
    if (require(std::fabs(input[i] - output[i]) < 0.0001f, "unloaded NAM block should pass through")) return 1;
  }

  // NAM's input_level_dbu metadata describes the analog level represented by
  // 0 dBFS during capture. A device reference 6.0206 dB below that must apply
  // exactly half-scale before the neural model.
  {
    const std::filesystem::path metadataModel = ARDOR_NAM_EXAMPLE_MODEL;
    ardor::NamProcessor metadataProbe;
    if (require(metadataProbe.load(metadataModel, 48000.0, 64),
                "metadata NAM model should load")) return 1;
    if (require(metadataProbe.modelInputLevelDbU().has_value(),
                "metadata NAM model should expose its input reference")) return 1;

    const float deviceReference = *metadataProbe.modelInputLevelDbU() - 6.0205999f;
    ardor::NamProcessor calibrated;
    ardor::NamProcessor manual;
    if (require(calibrated.load(metadataModel, 48000.0, 64, 1.0f, deviceReference),
                "calibrated NAM model should load")) return 1;
    if (require(manual.load(metadataModel, 48000.0, 64),
                "manual-reference NAM model should load")) return 1;
    if (require(std::fabs(calibrated.inputCalibrationGain() - 0.5f) < 0.0001f,
                "NAM device/model reference delta should become input gain")) return 1;

    std::vector<float> source(128);
    std::vector<float> calibratedOut(source.size());
    std::vector<float> manualOut(source.size());
    for (std::size_t i = 0; i < source.size(); ++i) {
      source[i] = 0.2f * std::sin(static_cast<float>(i) * 0.11f);
    }
    calibrated.processBlock(source.data(), calibratedOut.data(), source.size());
    for (auto& sample : source) sample *= 0.5f;
    manual.processBlock(source.data(), manualOut.data(), source.size());
    for (std::size_t i = 0; i < source.size(); ++i) {
      if (require(std::fabs(calibratedOut[i] - manualOut[i]) < 0.0001f,
                  "calibrated NAM drive should match explicit input scaling")) return 1;
    }
  }

  // Block-vs-sample equivalence with a real model. models/test.nam is a local
  // asset (not committed), so this section self-gates on its presence.
  const std::filesystem::path namPath = std::filesystem::path{ARDOR_SOURCE_DIR} / "models/test.nam";
  if (!std::filesystem::exists(namPath)) {
    std::cerr << "nam_processor_smoke: models/test.nam not present, skipping model equivalence\n";
    return 0;
  }

  ardor::NamProcessor perSample;
  ardor::NamProcessor perBlock;
  ardor::NamProcessor chunked;
  ardor::NamProcessor nanoTier;
  if (require(perSample.load(namPath, 48000.0, 64), "per-sample model should load")) return 1;
  if (require(perBlock.load(namPath, 48000.0, 64), "per-block model should load")) return 1;
  if (require(chunked.load(namPath, 48000.0, 64), "chunked model should load")) return 1;
  if (require(nanoTier.load(namPath, 48000.0, 64, 0.0f), "nano-tier model should load")) return 1;
  if (require(perBlock.slimmableSize() == 1.0f, "default NAM tier must be the full model")) return 1;
  if (require(nanoTier.slimmableSize() == 0.0f, "explicit NAM nano tier must be retained")) return 1;

  std::vector<float> namIn(256, 0.0f);
  for (size_t i = 0; i < namIn.size(); ++i) {
    namIn[i] = 0.5f * std::sin(static_cast<float>(i) * 0.13f);
  }

  std::vector<float> outSample(namIn.size(), 0.0f);
  std::vector<float> outBlock(namIn.size(), 0.0f);
  std::vector<float> outChunked(namIn.size(), 0.0f);
  for (size_t i = 0; i < namIn.size(); ++i) {
    outSample[i] = perSample.process(namIn[i]);
  }
  for (size_t offset = 0; offset < namIn.size(); offset += 64) {
    perBlock.processBlock(namIn.data() + offset, outBlock.data() + offset, 64);
  }
  // frames > maxBlockSize exercises the internal chunk loop.
  chunked.processBlock(namIn.data(), outChunked.data(), namIn.size());

  for (size_t i = 0; i < namIn.size(); ++i) {
    if (require(std::fabs(outSample[i] - outBlock[i]) < 0.0001f, "block output should match per-sample")) return 1;
    if (require(std::fabs(outSample[i] - outChunked[i]) < 0.0001f, "chunked output should match per-sample")) return 1;
  }
  return 0;
}
