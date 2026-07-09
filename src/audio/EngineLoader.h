#pragma once

#include "dsp/PedalEngine.h"
#include "preset/ChainPlan.h"
#include "preset/PresetStore.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace ardor {

struct EngineLoadOptions {
  uint32_t sampleRate = 48000;
  uint32_t blockSize = 64;
  size_t irSamples = 8192;
};

bool applyChainPlan(PedalEngine& engine, const ChainPlan& plan, const EngineLoadOptions& options, std::string& error);

bool applyPreset(PedalEngine& engine, const Preset& preset, const std::filesystem::path& dataRoot,
                 const EngineLoadOptions& options, std::string& error);
bool applyPresetSlot(PedalEngine& engine, const PresetStore& store, PresetSlot slot,
                     const std::filesystem::path& dataRoot, const EngineLoadOptions& options, std::string& error);

} // namespace ardor
