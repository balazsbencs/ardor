#pragma once

#include "Preset.h"

#include <nlohmann/json.hpp>

#include <array>
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
  struct DualAmpLane {
    std::filesystem::path modelPath;
    std::filesystem::path cabPath;
  };

  std::string id;
  std::string type;
  ChainBlockStatus status = ChainBlockStatus::Ready;
  std::filesystem::path assetPath;
  nlohmann::json params = nlohmann::json::object();
  bool enabled = true;
  float level = 1.0f;
  float mix = 1.0f;
  std::array<DualAmpLane, 2> dualAmpLanes;
  std::array<std::vector<ChainBlockPlan>, 2> lanes;
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
