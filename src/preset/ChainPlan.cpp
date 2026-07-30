#include "preset/ChainPlan.h"

#include "daisyfx/DaisyFxCatalog.h"
#include "equalizer/EqParameters.h"

#include <algorithm>
#include <cmath>

namespace ardor {

namespace {

bool isSupportedBlockType(const std::string& type)
{
  return type == "nam" || type == "cab" || type == "dualAmp" || type == "dualRig";
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
  if (type != "dynamics") return false;
  const auto mode = params.value("mode", "");
  return mode == "compressor" || mode == "noise_gate";
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

bool prepareDualAmpPlan(ChainBlockPlan& blockPlan, const std::filesystem::path& dataRoot)
{
  constexpr std::array<const char*, 2> modelKeys = {"leftNamAsset", "rightNamAsset"};
  constexpr std::array<const char*, 2> cabKeys = {"leftIrAsset", "rightIrAsset"};
  for (std::size_t lane = 0; lane < blockPlan.dualAmpLanes.size(); ++lane) {
    const auto model = blockPlan.params.find(modelKeys[lane]);
    const auto cab = blockPlan.params.find(cabKeys[lane]);
    if (model == blockPlan.params.end() || cab == blockPlan.params.end()
        || !model->is_string() || !cab->is_string()) {
      return false;
    }
    const auto modelAsset = model->get<std::string>();
    const auto cabAsset = cab->get<std::string>();
    if (modelAsset.empty() || cabAsset.empty()
        || !isValidBlockAssetPath(modelAsset) || !isValidBlockAssetPath(cabAsset)) {
      return false;
    }
    auto& output = blockPlan.dualAmpLanes[lane];
    output.modelPath = dataRoot / modelAsset;
    output.cabPath = dataRoot / cabAsset;
    std::error_code modelError;
    std::error_code cabError;
    if (!std::filesystem::exists(output.modelPath, modelError) || modelError
        || !std::filesystem::exists(output.cabPath, cabError) || cabError) {
      return false;
    }
  }
  return true;
}

ChainBlockPlan buildBlockPlan(const PresetBlock& block, const std::filesystem::path& dataRoot,
                              std::size_t& runnableBlockCount)
{
  ChainBlockPlan blockPlan;
  blockPlan.id = block.id;
  blockPlan.type = block.type;
  blockPlan.params = block.params.is_null() ? nlohmann::json::object() : block.params;
  if (block.type == "cab") {
    blockPlan.level = dbToGain(std::clamp(
      finiteNumberOr(blockPlan.params, "levelDb", 0.0f), -60.0f, 24.0f));
    blockPlan.mix = std::clamp(
      finiteNumberOr(blockPlan.params, "mix", 1.0f), 0.0f, 1.0f);
  }
  if (isValidBlockAssetPath(block.asset)) {
    blockPlan.assetPath = dataRoot / block.asset;
  }
  std::size_t childRunnableBlockCount = 0;
  if (block.type == "dualRig") {
    for (std::size_t lane = 0; lane < block.lanes.size(); ++lane) {
      for (const auto& child : block.lanes[lane]) {
        blockPlan.lanes[lane].push_back(buildBlockPlan(child, dataRoot, childRunnableBlockCount));
      }
    }
  }

  if (!block.enabled) {
    blockPlan.status = ChainBlockStatus::Disabled;
  } else if (block.type == "dualAmp") {
    if (prepareDualAmpPlan(blockPlan, dataRoot)) {
      blockPlan.status = ChainBlockStatus::Ready;
      ++runnableBlockCount;
    } else {
      blockPlan.status = ChainBlockStatus::MissingAsset;
    }
  } else if (block.type == "dualRig") {
    blockPlan.status = ChainBlockStatus::Ready;
    runnableBlockCount += 1 + childRunnableBlockCount;
  } else if (isDaisyBlockType(block.type)) {
    if (isSupportedDaisyBlock(block.type, blockPlan.params)) {
      blockPlan.status = ChainBlockStatus::Ready;
      ++runnableBlockCount;
    } else {
      blockPlan.status = ChainBlockStatus::Unsupported;
    }
  } else if (block.type == "dynamics") {
    if (isSupportedDynamicsBlock(block.type, blockPlan.params)) {
      blockPlan.status = ChainBlockStatus::Ready;
      ++runnableBlockCount;
    } else {
      blockPlan.status = ChainBlockStatus::Unsupported;
    }
  } else if (block.type == "eq") {
    if (isSupportedEqBlock(block.type, blockPlan.params)) {
      blockPlan.params = parametricEqParamsToJson(parametricEqParamsFromJson(blockPlan.params));
      blockPlan.status = ChainBlockStatus::Ready;
      ++runnableBlockCount;
    } else {
      blockPlan.status = ChainBlockStatus::Unsupported;
    }
  } else if (!isSupportedBlockType(block.type)) {
    blockPlan.status = ChainBlockStatus::Unsupported;
  } else if (block.asset.empty() || !isValidBlockAssetPath(block.asset)) {
    blockPlan.status = ChainBlockStatus::MissingAsset;
  } else {
    std::error_code ec;
    if (!std::filesystem::exists(blockPlan.assetPath, ec) || ec) {
      blockPlan.status = ChainBlockStatus::MissingAsset;
    } else {
      blockPlan.status = ChainBlockStatus::Ready;
      ++runnableBlockCount;
    }
  }
  return blockPlan;
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
    plan.blocks.push_back(buildBlockPlan(block, dataRoot, plan.runnableBlockCount));
  }
  return plan;
}

} // namespace ardor
