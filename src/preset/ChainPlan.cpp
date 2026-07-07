#include "preset/ChainPlan.h"

namespace ardor {

namespace {

bool isSupportedBlockType(const std::string& type)
{
  return type == "nam" || type == "cab";
}

} // namespace

ChainPlan buildChainPlan(const Preset& preset, const std::filesystem::path& dataRoot)
{
  ChainPlan plan;
  for (const auto& block : preset.blocks) {
    ChainBlockPlan blockPlan;
    blockPlan.id = block.id;
    blockPlan.type = block.type;

    if (!block.enabled) {
      blockPlan.status = ChainBlockStatus::Disabled;
    } else if (!isSupportedBlockType(block.type)) {
      blockPlan.status = ChainBlockStatus::Unsupported;
    } else if (!std::filesystem::exists(dataRoot / block.asset)) {
      blockPlan.status = ChainBlockStatus::MissingAsset;
    } else {
      blockPlan.status = ChainBlockStatus::Ready;
      ++plan.runnableBlockCount;
    }

    plan.blocks.push_back(std::move(blockPlan));
  }
  return plan;
}

} // namespace ardor
