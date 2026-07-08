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
  if (require(perSample.load(namPath, 48000.0, 64), "per-sample model should load")) return 1;
  if (require(perBlock.load(namPath, 48000.0, 64), "per-block model should load")) return 1;
  if (require(chunked.load(namPath, 48000.0, 64), "chunked model should load")) return 1;

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
