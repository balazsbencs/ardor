#include "preset/ChainPlan.h"

#include "daisyfx/DaisyFxCatalog.h"
#include "equalizer/EqParameters.h"

#include <algorithm>
#include <cmath>

namespace ardor {

namespace {

bool isSupportedBlockType(const std::string& type)
{
  return type == "nam" || type == "cab";
}

bool isDaisyBlockType(const std::string& type)
{
  return type == "mod" || type == "delay" || type == "reverb";
}

bool isSupportedDaisyBlock(const std::string& type, const nlohmann::json& params)
{
  return isDaisyBlockType(type) && findDaisyFxDescriptor(type, params.value("mode", "")) != nullptr;
}

bool isSupportedDynamicsBlock(const std::string& type, const nlohmann::json& params)
{
  return type == "dynamics" && params.value("mode", "") == "compressor";
}

bool isSupportedEqBlock(const std::string& type, const nlohmann::json& params)
{
  return type == "eq" && isParametricEqMode(params);
}

float finiteNumberOr(const nlohmann::json& object, const char* key, float fallback)
{
  const auto it = object.find(key);
  if (it == object.end() || !it->is_number()) {
    return fallback;
  }
  const float value = it->get<float>();
  return std::isfinite(value) ? value : fallback;
}

} // namespace

float dbToGain(float db)
{
  return std::pow(10.0f, db / 20.0f);
}

ChainPlan buildChainPlan(const Preset& preset, const std::filesystem::path& dataRoot)
{
  ChainPlan plan;
  plan.inputGain = dbToGain(std::clamp(preset.global.inputGainDb, -60.0f, 24.0f));
  plan.outputGain = dbToGain(std::clamp(preset.global.outputGainDb, -60.0f, 24.0f));
  plan.safetyLimit = dbToGain(std::clamp(preset.global.safetyLimitDb, -60.0f, 0.0f));

  for (const auto& block : preset.blocks) {
    ChainBlockPlan blockPlan;
    blockPlan.id = block.id;
    blockPlan.type = block.type;
    blockPlan.params = block.params.is_null() ? nlohmann::json::object() : block.params;
    if (block.type == "cab") {
      blockPlan.level = dbToGain(std::clamp(finiteNumberOr(blockPlan.params, "levelDb", 0.0f), -60.0f, 24.0f));
      blockPlan.mix = std::clamp(finiteNumberOr(blockPlan.params, "mix", 1.0f), 0.0f, 1.0f);
    }
    if (isValidBlockAssetPath(block.asset)) {
      blockPlan.assetPath = dataRoot / block.asset;
    }

    if (!block.enabled) {
      blockPlan.status = ChainBlockStatus::Disabled;
    } else if (isDaisyBlockType(block.type)) {
      if (isSupportedDaisyBlock(block.type, blockPlan.params)) {
        blockPlan.status = ChainBlockStatus::Ready;
        ++plan.runnableBlockCount;
      } else {
        blockPlan.status = ChainBlockStatus::Unsupported;
      }
    } else if (block.type == "dynamics") {
      if (isSupportedDynamicsBlock(block.type, blockPlan.params)) {
        blockPlan.status = ChainBlockStatus::Ready;
        ++plan.runnableBlockCount;
      } else {
        blockPlan.status = ChainBlockStatus::Unsupported;
      }
    } else if (block.type == "eq") {
      if (isSupportedEqBlock(block.type, blockPlan.params)) {
        blockPlan.params = parametricEqParamsToJson(parametricEqParamsFromJson(blockPlan.params));
        blockPlan.status = ChainBlockStatus::Ready;
        ++plan.runnableBlockCount;
      } else {
        blockPlan.status = ChainBlockStatus::Unsupported;
      }
    } else if (!isSupportedBlockType(block.type)) {
      blockPlan.status = ChainBlockStatus::Unsupported;
    } else if (block.asset.empty()) {
      blockPlan.status = ChainBlockStatus::MissingAsset;
    } else if (!isValidBlockAssetPath(block.asset)) {
      blockPlan.status = ChainBlockStatus::MissingAsset;
    } else {
      std::error_code ec;
      if (!std::filesystem::exists(blockPlan.assetPath, ec) || ec) {
        blockPlan.status = ChainBlockStatus::MissingAsset;
      } else {
        blockPlan.status = ChainBlockStatus::Ready;
        ++plan.runnableBlockCount;
      }
    }

    plan.blocks.push_back(std::move(blockPlan));
  }
  return plan;
}

} // namespace ardor
