#pragma once

#include "dsp/PedalEngine.h"
#include "preset/ChainPlan.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace ardor {

struct EngineLoadOptions {
  uint32_t sampleRate = 48000;
  uint32_t blockSize = 64;
  size_t irSamples = 8192;
};

bool applyChainPlan(PedalEngine& engine, const ChainPlan& plan, const EngineLoadOptions& options, std::string& error);

} // namespace ardor
