#include "dsp/IrConvolver.h"
#include "dsp/PedalEngine.h"

#include <cmath>
#include <filesystem>
#include <vector>

namespace {

int require(bool condition)
{
  return condition ? 0 : 1;
}

} // namespace

int main()
{
  ardor::IrConvolver ir;
  ir.loadImpulse({1.0f, 0.5f});

  if (require(std::fabs(ir.processSample(1.0f) - 1.0f) < 0.0001f)) return 1;
  if (require(std::fabs(ir.processSample(0.0f) - 0.5f) < 0.0001f)) return 1;
  if (require(std::fabs(ir.processSample(0.0f)) < 0.0001f)) return 1;

  ardor::PedalEngine engine;
  engine.loadIr({1.0f});
  engine.setInputGain(0.5f);
  engine.setOutputGain(2.0f);
  const auto [left, right] = engine.process(0.25f);
  if (require(std::fabs(left - 0.25f) < 0.0001f)) return 1;
  if (require(std::fabs(right - 0.25f) < 0.0001f)) return 1;
  if (require(!engine.loadNam(std::filesystem::path{"missing.nam"}, 48000.0, 128))) return 1;
  return 0;
}
