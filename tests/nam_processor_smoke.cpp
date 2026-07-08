#include "dsp/NamProcessor.h"

#include <cmath>
#include <iostream>

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
  return 0;
}
