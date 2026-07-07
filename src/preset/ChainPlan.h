#pragma once

#include "Preset.h"

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
};

struct ChainPlan {
  std::vector<ChainBlockPlan> blocks;
  std::size_t runnableBlockCount = 0;
};

ChainPlan buildChainPlan(const Preset& preset, const std::filesystem::path& dataRoot);

} // namespace ardor
