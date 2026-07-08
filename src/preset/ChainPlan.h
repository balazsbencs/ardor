#pragma once

#include "Preset.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace ardor {

enum class ChainBlockStatus {
  Ready,
  MissingAsset,
  Unsupported,
  Disabled
};

struct ChainBlockPlan {
  std::string id;
  std::string type;
  ChainBlockStatus status = ChainBlockStatus::Ready;
  std::filesystem::path assetPath;
  nlohmann::json params = nlohmann::json::object();
};

struct ChainPlan {
  std::vector<ChainBlockPlan> blocks;
  std::size_t runnableBlockCount = 0;
  float inputGain = 1.0f;
  float outputGain = 1.0f;
  float safetyLimit = 0.8912509f;
};

float dbToGain(float db);
ChainPlan buildChainPlan(const Preset& preset, const std::filesystem::path& dataRoot);

} // namespace ardor
