#include "dsp/IrConvolver.h"
#include "dsp/PedalEngine.h"

#include <cassert>
#include <cmath>
#include <vector>

int main()
{
  ardor::IrConvolver ir;
  ir.loadImpulse({1.0f, 0.5f});

  assert(std::fabs(ir.processSample(1.0f) - 1.0f) < 0.0001f);
  assert(std::fabs(ir.processSample(0.0f) - 0.5f) < 0.0001f);
  assert(std::fabs(ir.processSample(0.0f)) < 0.0001f);

  ardor::PedalEngine engine;
  engine.loadIr({1.0f});
  engine.setInputGain(0.5f);
  engine.setOutputGain(2.0f);
  const auto [left, right] = engine.process(0.25f);
  assert(std::fabs(left - 0.25f) < 0.0001f);
  assert(std::fabs(right - 0.25f) < 0.0001f);
  return 0;
}
