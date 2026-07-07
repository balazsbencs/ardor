#include "dsp/IrConvolver.h"

#include <cassert>
#include <cmath>
#include <vector>

int main()
{
  ardor::IrConvolver ir;
  ir.loadImpulse({1.0f, 0.5f});

  std::vector<float> out;
  for (float x : {1.0f, 0.0f, 0.0f}) {
    out.push_back(ir.processSample(x));
  }

  assert(std::fabs(out[0] - 1.0f) < 0.0001f);
  assert(std::fabs(out[1] - 0.5f) < 0.0001f);
  assert(std::fabs(out[2]) < 0.0001f);
  return 0;
}
